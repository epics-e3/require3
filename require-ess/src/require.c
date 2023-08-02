/* Copyright (C) 2020 Dirk Zimoch */
/* Copyright (C) 2020-2022 European Spallation Source, ERIC */

#ifdef __unix
/* for vasprintf and dl_iterate_phdr */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif  // _GNU_SOURCE
#endif  // __unix

/* for 64 bit (NFS) file systems */
#define _FILE_OFFSET_BITS 64

#include "require.h"

#include <ctype.h>
#include <dbAccess.h>
#include <epicsExit.h>
#include <epicsExport.h>
#include <epicsStdio.h>
#include <epicsVersion.h>
#include <errno.h>
#include <initHooks.h>
#include <iocsh.h>
#include <osiFileName.h>
#include <recSup.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "common.h"
#include "module.h"
#include "version.h"

int requireDebug;

#if defined(__unix) || defined(UNIX)

#ifndef OS_CLASS
#if defined(__linux) || defined(linux)
#define OS_CLASS "Linux"
#endif  // __linux

#ifdef SOLARIS
#define OS_CLASS "solaris"
#endif  // SOLARIS

#ifdef __rtems__
#define OS_CLASS "RTEMS"
#endif  // __rtems__

#ifdef freebsd
#define OS_CLASS "freebsd"
#endif  // freebsd

#ifdef darwin
#define OS_CLASS "Darwin"
#endif  // darwin

#ifdef _AIX32
#define OS_CLASS "AIX"
#endif  // _AIX32
#endif  // OS_CLASS

#else
#error Only support Unix based distros
#endif

#include <dlfcn.h>
#define HMODULE void *

#define getAddress(module, name) dlsym(module, name)

#define PREFIX "lib"
#define INFIX
#define EXT ".so"
#include <dirent.h>
#define DIR_HANDLE DIR *
#define IF_OPEN_DIR(f) if ((dir = opendir(f)))
#define DIR_ENTRY struct dirent *
#define START_DIR_LOOP while ((errno = 0, direntry = readdir(dir)) != NULL)
#define END_DIR_LOOP                                              \
  if (!direntry && errno)                                         \
    fprintf(stderr, "error reading directory %s: %s\n", filename, \
            strerror(errno));                                     \
  if (dir) closedir(dir);
#ifdef _DIRENT_HAVE_D_TYPE
#define SKIP_NON_DIR(e) \
  if (e->d_type != DT_DIR && e->d_type != DT_UNKNOWN) continue;
#else  // _DIRENT_HAVE_D_TYPE
#define SKIP_NON_DIR(e)
#endif  // _DIRENT_HAVE_D_TYPE
#define FILENAME(e) e->d_name

#define LIBDIR "lib" OSI_PATH_SEPARATOR
#define TEMPLATEDIR "db"
#define LIBRELEASE "LibRelease"

#define E3_REQUIRE_LOCATION "E3_REQUIRE_LOCATION"
#define E3_REQUIRE_VERSION "E3_REQUIRE_VERSION"

#ifndef OS_CLASS
#error OS_CLASS not defined: Try to compile with USR_CFLAGS += -DOS_CLASS='"${OS_CLASS}"'
#endif  // OS_CLASS

const char osClass[] = OS_CLASS;

/* loadlib (library)
Find a loadable library by name and load it.
*/

char epicsRelease[80];
char *targetArch;

void set_require_env() {
  char *epics_version_major = getenv("EPICS_VERSION_MAJOR");
  char *epics_version_middle = getenv("EPICS_VERSION_MIDDLE");
  char *epics_version_minor = getenv("EPICS_VERSION_MINOR");

  sprintf(epicsRelease, "%s.%s.%s", epics_version_major, epics_version_middle,
          epics_version_minor);
  targetArch = getenv("EPICS_HOST_ARCH");

  putenvprintf("T_A=%s", targetArch);
  putenvprintf("EPICS_HOST_ARCH=%s", targetArch);
  putenvprintf("EPICS_RELEASE=%s", epicsRelease);
  putenvprintf("OS_CLASS=%s", osClass);
}

static HMODULE loadlib(const char *libname) {
  HMODULE libhandle = NULL;

  if (libname == NULL) {
    fprintf(stderr, "missing library name\n");
    return NULL;
  }

  if ((libhandle = dlopen(libname, RTLD_NOW | RTLD_GLOBAL)) == NULL) {
    fprintf(stderr, "Loading %s library failed: %s\n", libname, dlerror());
  }
  return libhandle;
}

struct linkedList loadedModules = {0};

static int setupDbPath(const char *module, const char *dbdir) {
  char *absdir =
      realpathSeparator(dbdir); /* so we can change directory later safely */
  if (absdir == NULL) {
    debug("require: cannot resolve %s\n", dbdir);
    return -1;
  }

  debug("require: found template directory %s\n", absdir);

  /* set up db search path environment variables
    <module>_DB             template path of <module>
    TEMPLATES               template path of the current module (overwritten)
    EPICS_DB_INCLUDE_PATH   template path of all loaded modules (last in front
    after ".")
  */

  putenvprintf("%s_DB=%s", module, absdir);
  putenvprintf("TEMPLATES=%s", absdir);
  if (isModuleLoaded(&loadedModules, "stream")) {
    pathAdd("STREAM_PROTOCOL_PATH", absdir);
  }
  pathAdd("EPICS_DB_INCLUDE_PATH", absdir);
  free(absdir);
  return 0;
}

static int getRecordHandle(const char *namepart, short type, long minsize,
                           DBADDR *paddr) {
  char recordname[PVNAME_STRINGSZ] = {0};
  long dummy = 0L;
  long offset = 0L;

  sprintf(recordname, "%.*s%s", (int)(PVNAME_STRINGSZ - strnlen(namepart, PVNAME_STRINGSZ-1) - 1),
          getenv("REQUIRE_IOC"), namepart);

  if (dbNameToAddr(recordname, paddr) != 0) {
    fprintf(stderr, "require:getRecordHandle : record %s not found\n",
            recordname);
    return -1;
  }
  if (paddr->field_type != type) {
    fprintf(
        stderr,
        "require:getRecordHandle : record %s has wrong type %s instead of %s\n",
        recordname, pamapdbfType[paddr->field_type].strvalue,
        pamapdbfType[type].strvalue);
    return -1;
  }
  if (paddr->no_elements < minsize) {
    fprintf(stderr,
            "require:getRecordHandle : record %s has not enough elements: %lu "
            "instead of %lu\n",
            recordname, paddr->no_elements, minsize);
    return -1;
  }
  if (paddr->pfield == NULL) {
    fprintf(
        stderr,
        "require:getRecordHandle : record %s has not yet allocated memory\n",
        recordname);
    return -1;
  }

  /* update array information */
  dbGetRset(paddr)->get_array_info(paddr, &dummy, &offset);

  return 0;
}

/*
We can fill the records only after they have been initialized, at
initHookAfterFinishDevSup. But use double indirection here because in 3.13 we
must wait until initHooks is loaded before we can register the hook.
*/
static void fillModuleListRecord(initHookState state) {
  if (state != initHookAfterFinishDevSup)
    return;

  struct dbAddr modules = {0}, versions = {0}, modver = {0};
  char *bufferModules, *bufferVersions, *bufferModver;
  struct module *m = NULL;
  int i = 0;
  int c = 0;

  getRecordHandle(":Modules", DBF_STRING, 0, &modules);
  getRecordHandle(":Versions", DBF_STRING, 0, &versions);
  getRecordHandle(":ModuleVersions", DBF_CHAR, 0, &modver);

  bufferModules = (char *)calloc(MAX_STRING_SIZE*loadedModules.size, sizeof(char));
  bufferVersions = (char *)calloc(MAX_STRING_SIZE*loadedModules.size, sizeof(char));
  bufferModver = (char *)calloc(MAX_STRING_SIZE*loadedModules.size, sizeof(char));

  for (m = loadedModules.head, i = 0; m != NULL ; m = m->next, i++){
    debug("require: %s[%d] = \"%.*s\"\n", modules.precord->name, i,
          MAX_STRING_SIZE - 1, m->name);
    sprintf((char *)(bufferModules) + i * MAX_STRING_SIZE, "%.*s",
            MAX_STRING_SIZE - 1, m->name);
    debug("require: %s[%d] = \"%.*s\"\n", versions.precord->name, i,
          MAX_STRING_SIZE - 1, m->version);
    sprintf((char *)(bufferVersions) + i * MAX_STRING_SIZE, "%.*s",
            MAX_STRING_SIZE - 1, m->version);
    debug("require: %s+=\"%s %s\"\n", modver.precord->name,
          m->name, m->version);
    c += sprintf((char *)(bufferModver) + c, "%s %s\n",
                 m->name, m->version);
  }

  if (dbPut(&modules, DBF_STRING, bufferModules, loadedModules.size) != 0){
    errlogPrintf("require: Error to put Modules\n");
  }
  if (dbPut(&versions, DBF_STRING, bufferVersions, loadedModules.size) != 0){
    errlogPrintf("require: Error to put Versions\n");
  }
  if (dbPut(&modver, DBF_CHAR, bufferModver, strlen(bufferModver)) != 0){
    printf("require: Error to put ModuleVersions");
  }
}

static int registerRequire(){
  char *requireLocation = NULL;
  char *requireVersion = NULL;

  requireLocation = getenv(E3_REQUIRE_LOCATION);
  if (!requireLocation){
    printf("require: Failed to get " E3_REQUIRE_LOCATION "\n");
    return -1;
  }
  requireVersion = getenv(E3_REQUIRE_VERSION);
  if (!requireVersion){
    printf("require: Failed to get " E3_REQUIRE_VERSION "\n");
    return -1;
  }
  registerModule(&loadedModules, "require", requireVersion, requireLocation);
  return 0;
}

int libversionShow(const char *outfile) {
  struct module *m = NULL;

  FILE *out = epicsGetStdout();

  if (outfile) {
    out = fopen(outfile, "w");
    if (out == NULL) {
      fprintf(stderr, "can't open %s: %s\n", outfile, strerror(errno));
      return -1;
    }
  }
  for (m = loadedModules.head; m; m = m->next) {
    fprintf(out, "%s-%20s %s\n", m->name,
            m->version, m->path);
  }
  if (fflush(out) < 0 && outfile) {
    fprintf(stderr, "can't write to %s: %s\n", outfile, strerror(errno));
    return -1;
  }
  if (outfile) fclose(out);
  return 0;
}

#define MISMATCH -1
#define MATCH 1
#define HIGHER 3

static int compareDigit(int found, int requested, const char *name) {
  debug("require: compareDigit: found %d, requested %d for digit %s\n", found,
        requested, name);
  if (found < requested) {
    debug("require: compareVersions: MISMATCH too low %s number\n", name);
    return MISMATCH;
  }
  if (found > requested) {
    debug("require: compareVersions: HIGHER %s number\n", name);
    return HIGHER;
  }

  return MATCH;
}

static int compareNumericVersion(semver_t *sv_found, semver_t *sv_request) {
  int match = 0;

  match = compareDigit(sv_found->major, sv_request->major, "major");
  if (match != MATCH) {
    return match;
  }
  match = compareDigit(sv_found->minor, sv_request->minor, "minor");
  if (match != MATCH) {
    return match;
  }
  return compareDigit(sv_found->patch, sv_request->patch, "patch");
}

/*
 * Returns if the version <found> is higher than <request>.
 */
static int compareVersions(const char *found, const char *request,
                           int already_matched) {
  semver_t *sv_found = NULL, *sv_request = NULL;
  int match = 0;

  debug("require: compareVersions(found=%s, request=%s)\n", found,
        request ? request : "");

  if (request == NULL || request[0] == 0) {
    debug("require: compareVersions: MATCH empty version requested\n");
    return MATCH;
  }
  if (found == NULL || found[0] == 0) {
    debug("require: compareVersions: MISMATCH empty version found\n");
    return MISMATCH;
  }

  sv_found = parse_semver(found);
  sv_request = parse_semver(request);
  if (sv_found == NULL || sv_request == NULL) {
    debug("require: compareVersion: failed to allocate semver_t\n");
    return MISMATCH;
  }

  // TODO: maybe don't do this. This is only in the case that
  // we have found an installed version with no revision number.
  if (already_matched && sv_request->revision == -1) sv_request->revision = 0;

  // test version, look for exact.
  if (strlen(sv_request->test_str) > 0) {
    if (strcmp(sv_found->test_str, sv_request->test_str) == 0) {
      debug(
          "require: compareVersions: Test version requested and found, "
          "matches\n");
      match = MATCH;
    } else if (strlen(sv_found->test_str) > 0) {
      debug(
          "require: compareVersions: Test versions requested and found, no "
          "match\n");
      match = MISMATCH;
    } else {
      debug(
          "require: compareVersions: found numeric version, higher than "
          "test\n");
      match = HIGHER;
    }
  } else if (strlen(sv_found->test_str) > 0) {
    debug(
        "require: compareVersions: Numeric version requested, test version "
        "found\n");
    match = MISMATCH;
  } else {
    match = compareNumericVersion(sv_found, sv_request);
  }

  // Finally, check revision numbers
  if (match == MATCH) {
    if (sv_request->revision == -1) {
      if (already_matched) {
        debug(
            "require: compareVersions: No revision number for already found "
            "version. Returning HIGHER\n");
        match = HIGHER;
      } else {
        debug(
            "require: compareVersions: No revision number requested. Returning "
            "MATCH\n");
        match = MATCH;
      }
    } else {
      match =
          compareDigit(sv_found->revision, sv_request->revision, "revision");
    }
  }
  cleanup_semver(sv_found);
  cleanup_semver(sv_request);
  return match;
}

/* require (module)
Look if module is already loaded.
If module is already loaded check for version mismatch.
If module is not yet loaded load the library with ld,
load <module>.dbd with dbLoadDatabase (if file exists)
and call <module>_registerRecordDeviceDriver function.

If require is called from the iocsh before iocInit and fails,
it calls epicsExit to abort the application.
*/

/* wrapper to abort statup script */
static int require_priv(const char *module, const char *version);

int require(const char *module, const char *version) {
  int status = 0;

  if (module == NULL) {
    printf("Usage: require \"<module>\" [, \"<version>\" ]\n");
    printf("Loads " PREFIX "<module>" INFIX EXT " and <libname>.dbd\n");
    printf("And calls <module>_registerRecordDeviceDriver\n");
    return -1;
  }

  if (interruptAccept) {
    fprintf(stderr, "Error! Modules can only be loaded before iocIint!\n");
    return -1;
  }

  if (version && version[0] == 0) version = NULL;

  if (version && strcmp(version, "none") == 0) {
    debug("require: skip version=none\n");
    return 0;
  }

  status = require_priv(module, version);

  if (status == 0) return 0;
  if (status != -1) perror("require");
  if (interruptAccept) return status;

  /* require failed in startup script before iocInit */
  fprintf(stderr, "Aborting startup script\n");
  epicsExit(1);
  return status;
}

static off_t fileSize(const char *filename) {
  struct stat filestat = {0};
  if (stat(filename, &filestat) != 0) {
    debug("require: %s does not exist\n", filename);
    return -1;
  }
  switch (filestat.st_mode & S_IFMT) {
    case S_IFREG:
      debug("require: file %s exists, size %lld bytes\n", filename,
            (unsigned long long)filestat.st_size);
      return filestat.st_size;
    case S_IFDIR:
      debug("require: directory %s exists\n", filename);
      return 0;
#ifdef S_IFBLK
    case S_IFBLK:
      debug("require: %s is a block device\n", filename);
      return -1;
#endif
#ifdef S_IFCHR
    case S_IFCHR:
      debug("require: %s is a character device\n", filename);
      return -1;
#endif
#ifdef S_IFIFO
    case S_IFIFO:
      debug("require: %s is a FIFO/pipe\n", filename);
      return -1;
#endif
#ifdef S_IFSOCK
    case S_IFSOCK:
      debug("require: %s is a socket\n", filename);
      return -1;
#endif
    default:
      debug("require: %s is an unknown type of special file\n", filename);
      return -1;
  }
}
#define fileExists(filename) (fileSize(filename) >= 0)
#define fileNotEmpty(filename) (fileSize(filename) > 0)
#define TRY_FILE(offs, ...)                                           \
  (snprintf(filename + offs, PATH_MAX - offs, __VA_ARGS__) && \
   fileExists(filename))

#define TRY_NONEMPTY_FILE(offs, ...)                                  \
  (snprintf(filename + offs, PATH_MAX - offs, __VA_ARGS__) && \
   fileNotEmpty(filename))

static int handleDependencies(const char *module, char *depfilename) {
  FILE *depfile = NULL;
  char buffer[40] = {0};
  char *end = NULL;      /* end of string */
  char *rmodule = NULL;  /* required module */
  char *rversion = NULL; /* required version */

  debug("require: parsing dependency file %s\n", depfilename);
  depfile = fopen(depfilename, "r");
  while (fgets(buffer, sizeof(buffer) - 1, depfile)) {
    rmodule = buffer;
    /* ignore leading spaces */
    while (isspace((unsigned char)*rmodule)) rmodule++;
    /* ignore empty lines and comment lines */
    if (*rmodule == 0 || *rmodule == '#') continue;
    /* rmodule at start of module name */
    rversion = rmodule;
    /* find end of module name */
    while (*rversion && !isspace((unsigned char)*rversion)) rversion++;
    /* terminate module name */
    *rversion++ = 0;
    /* ignore spaces */
    while (isspace((unsigned char)*rversion)) rversion++;
    /* rversion at start of version */

    if (*rversion) {
      end = rversion;
      /* find end of version */
      while (*end && !isspace((unsigned char)*end)) end++;
      /* terminate version */
      *end = 0;
    }
    printf("Module %s depends on %s %s\n", module, rmodule, rversion);
    if (require(rmodule, rversion) != 0) {
      fclose(depfile);
      return -1;
    }
  }
  fclose(depfile);
  return 0;
}

/*
 * Fetches the correct module version based on the requested version by
 * searching through EPICS_DRIVER_PATH until it finds a matching version.
 *
 * Sets <filename> to be the path the the underlying module.
 */
static char* fetch_module_version(char *filename, size_t max_file_len,
                                const char *module, const char *version) {
  const char *dirname = NULL;
  const char *driverpath = NULL;
  const char *end = NULL;
  const char *found = NULL;
  int versionLength = 0;
  char *selectedVersion = NULL;
  char *founddir = NULL;

  int someVersionFound = 0;
  int someArchFound = 0;

  driverpath = getenv("EPICS_DRIVER_PATH");
  if (driverpath == NULL) driverpath = ".";
  debug("require: searchpath=%s\n", driverpath);

  for (dirname = driverpath; dirname != NULL; dirname = end) {
    /* get one directory from driverpath */
    int dirlen = 0;
    int modulediroffs = 0;
    DIR_HANDLE dir = NULL;
    DIR_ENTRY direntry = NULL;

    end = strchr(dirname, OSI_PATH_LIST_SEPARATOR[0]);
    if (end && end[1] == OSI_PATH_SEPARATOR[0] &&
        end[2] == OSI_PATH_SEPARATOR[0]) /* "http://..." and friends */
      end = strchr(end + 2, OSI_PATH_LIST_SEPARATOR[0]);
    if (end)
      dirlen = (int)(end++ - dirname);
    else
      dirlen = (int)strnlen(dirname, PATH_MAX);
    if (dirlen == 0) continue; /* ignore empty driverpath elements */

    debug("require: trying %.*s\n", dirlen, dirname);

    snprintf(filename, max_file_len,
             "%.*s" OSI_PATH_SEPARATOR "%s" OSI_PATH_SEPARATOR "%n", dirlen,
             dirname, module, &modulediroffs);
    dirlen++;
    /* filename = "<dirname>/[dirlen]<module>/[modulediroffs]" */

    /* Does the module directory exist? */
    IF_OPEN_DIR(filename) {
      debug("require: found directory %s\n", filename);

      /* Now look for versions. */
      START_DIR_LOOP {
        char *currentFilename = FILENAME(direntry);

        SKIP_NON_DIR(direntry)
        if (currentFilename[0] == '.') continue; /* ignore hidden directories */

        someVersionFound = 1;

        /* Look for highest matching version. */
        debug("require: checking version %s against required %s\n",
              currentFilename, version ? version : "");

        switch (compareVersions(currentFilename, version, FALSE)) {
          case MATCH: /* all given numbers match. */
          {
            someArchFound = 1;

            debug("require: %s %s may match %s\n", module, currentFilename,
                  version ? version : "");

            /* Check if it has our EPICS version and architecture. */
            /* Even if it has no library, at least it has a dep file in the
             * lib dir */

            /* Step 1 : library file location */
            /* filename = "<dirname>/[dirlen]<module>/[modulediroffs]" */
            if (!TRY_FILE(modulediroffs,
                          "%s" OSI_PATH_SEPARATOR LIBDIR
                          "%s" OSI_PATH_SEPARATOR,
                          currentFilename, targetArch)) {
              /* filename =
               * "<dirname>/[dirlen]<module>/[modulediroffs]<version>/lib/<targetArch>/"
               */
              debug("require: %s %s has no support for %s %s\n", module,
                    currentFilename, epicsRelease, targetArch);
              continue;
            }

            /* Is it higher than the one we found before? */
            if (found)
              debug(
                  "require: %s %s support for %s %s found, compare against "
                  "previously found %s\n",
                  module, currentFilename, epicsRelease, targetArch, found);
            if (!found ||
                compareVersions(currentFilename, found, TRUE) == HIGHER) {
              debug("require: %s %s looks promising\n", module,
                    currentFilename);
              break;
            }
            debug("require: version %s is lower than %s \n", currentFilename,
                  found);
            continue;
          }
          default: {
            debug("require: %s %s does not match %s\n", module, currentFilename,
                  version);
            continue;
          }
        }
        /* we have found something */
        if (founddir) free(founddir);
        /* filename = "<dirname>/[dirlen]<module>/[modulediroffs]..." */
        if (asprintf(&founddir, "%.*s%s", modulediroffs, filename,
                     currentFilename) < 0)
          return NULL;
        /* founddir = "<dirname>/[dirlen]<module>/[modulediroffs]<version>" */
        found = founddir + modulediroffs; /* version part in the path */
      }
      END_DIR_LOOP
    }
    /* filename = "<dirname>/[dirlen]..." */
    if (!found)
      debug("require: no matching version in %.*s\n", dirlen, filename);
  }

  if (!found) {
    if (someArchFound)
      fprintf(stderr,
              "Module %s%s%s not available for %s\n(but maybe for other "
              "EPICS versions or architectures)\n",
              module, version ? " version " : "", version ? version : "",
              targetArch);
    else if (someVersionFound)
      fprintf(
          stderr,
          "Module %s%s%s not available (but other versions are available)\n",
          module, version ? " version " : "", version ? version : "");
    else
      fprintf(stderr, "Module %s%s%s not available\n", module,
              version ? " version " : "", version ? version : "");
    if (founddir) free(founddir);
    return NULL;
  }

  /* founddir = "<dirname>/[dirlen]<module>/<version>" */
  printf("Module %s version %s found in %s" OSI_PATH_SEPARATOR "\n", module,
         found, founddir);

  snprintf(filename, max_file_len, "%s" OSI_PATH_SEPARATOR, founddir);
  versionLength = strlen(found)+1;
  selectedVersion = calloc(versionLength, sizeof(char));
  memcpy(selectedVersion, found, versionLength);
  free(founddir);
  return selectedVersion;
}

/*
 * Loads the shared library if available.
 *
 * Returns the actual version string for the module, and NULL if there is a
 * mismatch.
 */
static const char *compare_module_version(char *filename, const char *module,
                                          const char *version, int libdiroffs) {
  HMODULE libhandle = NULL;
  char *symbolname = NULL;
  const char *found = NULL;
  /* filename =
     "<dirname>/[dirlen]<module>/<version>/[releasediroffs]/lib/<targetArch>/[libdiroffs]/PREFIX<module>INFIX(EXT)?"
   */
  if (!(TRY_FILE(libdiroffs, PREFIX "%s" INFIX EXT, module))) {
    printf("Module %s has no library\n", module);
    found = version;
  } else {
    printf("Loading library %s\n", filename);
    if ((libhandle = loadlib(filename)) == NULL) {
      return NULL;
    }

    /* now check what version we really got (with compiled-in version number)
     */
    if (asprintf(&symbolname, "_%sLibRelease", module) < 0) {
      return NULL;
    }

    found = (const char *)getAddress(libhandle, symbolname);
    free(symbolname);
    printf("Loaded %s version %s\n", module, found);

    /* check what we got */
    debug("require: compare requested version %s with loaded version %s\n",
          version, found);
    if (compareVersions(found, version, FALSE) == MISMATCH) {
      fprintf(stderr, "Requested %s version %s not available, found only %s.\n",
              module, version, found);
      return NULL;
    }
  }
  return found;
}

/*
 * Loads the module .dbd file and runs registerRecordDeviceDriver.
 */
static int load_module_data(char *filename, const char *module,
                            const char *version, int releasediroffs) {
  int returnvalue = 0;
  char *symbolname = NULL;

  /* load dbd file */
  if (TRY_NONEMPTY_FILE(releasediroffs, "dbd" OSI_PATH_SEPARATOR "%s.dbd",
                        module)) {
    printf("Loading dbd file %s\n", filename);
    if (dbLoadDatabase(filename, NULL, NULL) != 0) {
      fprintf(stderr, "Error loading %s\n", filename);
      return -1;
    }

    /* when dbd is loaded call register function */
    if (asprintf(&symbolname, "%s_registerRecordDeviceDriver", module) < 0) {
      return -1;
    }

    printf("Calling function %s\n", symbolname);
    returnvalue = iocshCmd(symbolname);
    free(symbolname);
    if (returnvalue) return -1;
  } else {
    /* no dbd file, but that might be OK */
    printf("%s has no dbd file\n", module);
  }
  return 0;
}

static int require_priv(const char *module, const char *version) {
  int returnvalue = 0;
  const char *loaded = NULL;
  const char *found = NULL;
  const char *dirname = NULL;
  char *selectedVersion = NULL;

  int dirlen = 0;
  int releasediroffs = 0;
  int libdiroffs = 0;
  char filename[PATH_MAX] = {0};

  static char *globalTemplates = NULL;
  if (!globalTemplates) {
    char *t = getenv("TEMPLATES");
    if (t) globalTemplates = strdup(t);
  }

  debug("require: module=\"%s\" version=\"%s\"\n", module, version);

  /* check already loaded verion */
  loaded = getLibVersion(&loadedModules,module);
  if (loaded) {
    /* Library already loaded. Check Version. */
    switch (compareVersions(loaded, version, FALSE)) {
      case MATCH:
        printf("Module %s version %s already loaded\n", module, loaded);
        break;
      default:
        printf(
            "Conflict between requested %s version %s and already loaded "
            "version %s.\n",
            module, version, loaded);
        return -1;
    }
    dirname = getLibLocation(&loadedModules, module);
    if (dirname[0] == 0) return 0;
    debug("require: library found in %s\n", dirname);
    snprintf(filename, sizeof(filename), "%s%n", dirname, &releasediroffs);
    putenvprintf("MODULE=%s", module);
    pathAdd("SCRIPT_PATH", dirname);
  } else {
    debug("require: no %s version loaded yet\n", module);

    /* Step 1: Search for module in driverpath */
    selectedVersion =
        fetch_module_version(filename, sizeof(filename), module, version);
    if (!selectedVersion){
      returnvalue = -1;
      goto require_priv_end;
    }
    /* Step 2 : Looking for .dep file */
    debug("require: looking for dependency file\n");

    dirlen = strnlen(filename, PATH_MAX);
    if (!TRY_FILE(dirlen,
                  OSI_PATH_SEPARATOR "%n" LIBDIR "%s" OSI_PATH_SEPARATOR
                                     "%n%s.dep",
                  &releasediroffs, targetArch, &libdiroffs, module)) {
      /* filename =
         "<dirname>/[dirlen]<module>/<version>/[releasediroffs]/lib/<targetArch>/[libdiroffs]/module.dep"
       */
      fprintf(stderr, "Dependency file %s not found\n", filename);
    } else {
      /* filename =
       * "<dirname>/[dirlen]<module>/<version>/[releasediroffs]/lib/<targetArch>/[libdiroffs]/module.dep"
       */
      if (handleDependencies(module, filename) == -1) {
        returnvalue = -1;
        goto require_priv_end;
      }
    }
    releasediroffs += dirlen;
    libdiroffs += dirlen;

    /* Step 3: Ensure that we have loaded the correct version */
    debug("require: Check that the loaded and requested versions match\n");
    found = compare_module_version(filename, module, selectedVersion, libdiroffs);
    if (!found) {
      returnvalue = -1;
      goto require_priv_end;
    }

    /* Step 4: Load module data */
    debug("require: Load module data\n");
    returnvalue = load_module_data(filename, module, found, releasediroffs);
    if (returnvalue) {
      goto require_priv_end;
    }

    /* register module with path */
    filename[releasediroffs] = 0;
    registerModule(&loadedModules, module, found, filename);
  }

  debug("require: looking for template directory\n");
  /* filename =
   * "<dirname>/[dirlen]<module>/<version>/[releasediroffs]..."
   */
  if (!(TRY_FILE(releasediroffs, TEMPLATEDIR) &&
        setupDbPath(module, filename) == 0)) {
    /* if no template directory found, restore TEMPLATES to initial value */
    char *t;
    t = getenv("TEMPLATES");
    if (globalTemplates && (!t || strcmp(globalTemplates, t) != 0))
      putenvprintf("TEMPLATES=%s", globalTemplates);
  }

require_priv_end:
  free(selectedVersion);
  return returnvalue;
}

static const iocshFuncDef requireDef = {
    "require", 2,
    (const iocshArg *[]){
        &(iocshArg){"module", iocshArgString},
        &(iocshArg){"[version]", iocshArgString},
    }};

static void requireFunc(const iocshArgBuf *args) {
  require(args[0].sval, args[1].sval);
}

static const iocshFuncDef libversionShowDef = {
    "libversionShow", 1,
    (const iocshArg *[]){
        &(iocshArg){"outputfile", iocshArgString},
    }};

static void libversionShowFunc(const iocshArgBuf *args) {
  libversionShow(args[0].sval);
}

static const iocshFuncDef ldDef = {"ld", 1,
                                   (const iocshArg *[]){
                                       &(iocshArg){"library", iocshArgString},
                                   }};

static void ldFunc(const iocshArgBuf *args) { loadlib(args[0].sval); }

static const iocshFuncDef pathAddDef = {
    "pathAdd", 2,
    (const iocshArg *[]){
        &(iocshArg){"ENV_VARIABLE", iocshArgString},
        &(iocshArg){"directory", iocshArgString},
    }};

static void pathAddFunc(const iocshArgBuf *args) {
  pathAdd(args[0].sval, args[1].sval);
}

static void requireRegister(void) {
  static int firstTime = 1;
  if (firstTime) {
    firstTime = 0;
    iocshRegister(&requireDef, requireFunc);
    iocshRegister(&libversionShowDef, libversionShowFunc);
    iocshRegister(&ldDef, ldFunc);
    iocshRegister(&pathAddDef, pathAddFunc);
    if(registerRequire() != 0){
      printf("require: Could not register require.\n");
    }

    set_require_env();
    initHookRegister(fillModuleListRecord);
  }
}

epicsExportRegistrar(requireRegister);
epicsExportAddress(int, requireDebug);
