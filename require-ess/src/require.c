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
/* This prototype is missing in older EPICS versions */
DBCORE_API int epicsStdCall iocshCmd(const char *cmd);
#include <epicsExit.h>
#include <epicsExport.h>
#include <epicsStdio.h>
#include <osiFileName.h>

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
  return;
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

typedef struct moduleitem {
  struct moduleitem *next;
  char content[0];
} moduleitem;

static moduleitem *loadedModules = NULL;
static unsigned long moduleCount = 0;
static unsigned long moduleListBufferSize = 1;
static unsigned long maxModuleNameLength = 0;

int putenvprintf(const char *format, ...) {
  va_list ap;
  char *var;
  char *val;
  int status = 0;

  if (!format) return -1;
  va_start(ap, format);
  if (vasprintf(&var, format, ap) < 0) {
    perror("require putenvprintf");
    return errno;
  }
  va_end(ap);

  if (requireDebug) printf("require: putenv(\"%s\")\n", var);

  val = strchr(var, '=');
  if (!val) {
    fprintf(stderr, "putenvprintf: string contains no =: %s\n", var);
    status = -1;
  } else {
    *val++ = 0;
    if (setenv(var, val, 1) != 0) {
      perror("require putenvprintf: setenv failed");
      status = errno;
    }
  }
  free(var);
  return status;
}

void pathAdd(const char *varname, const char *dirname) {
  char *old_path;

  if (!varname || !dirname) {
    fprintf(stderr, "usage: pathAdd \"ENVIRONMENT_VARIABLE\",\"directory\"\n");
    fprintf(stderr,
            "       Adds or moves the directory to the front of the "
            "ENVIRONMENT_VARIABLE\n");
    fprintf(stderr, "       but after a leading \".\".\n");
    return;
  }

  /* add directory to front */
  old_path = getenv(varname);
  if (old_path == NULL) {
    putenvprintf("%s=." OSI_PATH_LIST_SEPARATOR "%s", varname, dirname);
  } else {
    size_t len = strlen(dirname);
    char *p;

    /* skip over "." at the beginning */
    if (old_path[0] == '.' && old_path[1] == OSI_PATH_LIST_SEPARATOR[0])
      old_path += 2;

    /* If directory is already in path, move it to front */
    p = old_path;
    while ((p = strstr(p, dirname)) != NULL) {
      if ((p == old_path || *(p - 1) == OSI_PATH_LIST_SEPARATOR[0]) &&
          (p[len] == 0 || p[len] == OSI_PATH_LIST_SEPARATOR[0])) {
        if (p == old_path) break; /* already at front, nothing to do */
        memmove(old_path + len + 1, old_path, p - old_path - 1);
        strcpy(old_path, dirname);
        old_path[len] = OSI_PATH_LIST_SEPARATOR[0];
        if (requireDebug)
          printf("require: modified %s=%s\n", varname, old_path);
        break;
      }
      p += len;
    }
    if (p == NULL) /* add new directory to the front (after "." )*/
      putenvprintf("%s=." OSI_PATH_LIST_SEPARATOR "%s" OSI_PATH_LIST_SEPARATOR
                   "%s",
                   varname, dirname, old_path);
  }
}

char *realpathSeparator(const char *location) {
  size_t ll;
  char *buffer = malloc(PATH_MAX + strlen(OSI_PATH_SEPARATOR));
  buffer = realpath(location, buffer);
  if (!buffer) {
    if (requireDebug) printf("require: realpath(%s) failed\n", location);
    return NULL;
  }
  ll = strlen(buffer);
  /* linux realpath removes trailing slash */
  if (buffer[ll - strlen(OSI_PATH_SEPARATOR)] != OSI_PATH_SEPARATOR[0]) {
    strcpy(buffer + ll + 1 - strlen(OSI_PATH_SEPARATOR), OSI_PATH_SEPARATOR);
  }
  return buffer;
}

int isModuleLoaded(const char *libname);

static int setupDbPath(const char *module, const char *dbdir) {
  char *absdir =
      realpathSeparator(dbdir); /* so we can change directory later safely */
  if (absdir == NULL) {
    if (requireDebug) printf("require: cannot resolve %s\n", dbdir);
    return -1;
  }

  if (requireDebug) printf("require: found template directory %s\n", absdir);

  /* set up db search path environment variables
    <module>_DB             template path of <module>
    TEMPLATES               template path of the current module (overwritten)
    EPICS_DB_INCLUDE_PATH   template path of all loaded modules (last in front
    after ".")
  */

  putenvprintf("%s_DB=%s", module, absdir);
  putenvprintf("TEMPLATES=%s", absdir);
  if (isModuleLoaded("stream")) {
    pathAdd("STREAM_PROTOCOL_PATH", absdir);
  }
  pathAdd("EPICS_DB_INCLUDE_PATH", absdir);
  free(absdir);
  return 0;
}

static int getRecordHandle(const char *namepart, short type, long minsize,
                           DBADDR *paddr) {
  char recordname[PVNAME_STRINGSZ] = "";
  long dummy = 0L;
  long offset = 0L;

  sprintf(recordname, "%.*s%s", (int)(PVNAME_STRINGSZ - strlen(namepart) - 1),
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
  /* MODULES record exists and has allocated memory */
  if (state == initHookAfterFinishDevSup) {
    DBADDR modules, versions, modver;
    int have_modules, have_versions, have_modver;
    moduleitem *m;
    int i = 0;
    long c = 0;

    if (requireDebug) printf("require: fillModuleListRecord\n");

    have_modules =
        (getRecordHandle(":Modules", DBF_STRING, moduleCount, &modules) == 0);
    have_versions =
        (getRecordHandle(":Versions", DBF_STRING, moduleCount, &versions) == 0);

    moduleListBufferSize += moduleCount * maxModuleNameLength;
    have_modver = (getRecordHandle(":ModuleVersions", DBF_CHAR,
                                   moduleListBufferSize, &modver) == 0);

    for (m = loadedModules, i = 0; m; m = m->next, i++) {
      size_t lm = strlen(m->content) + 1;
      if (have_modules) {
        if (requireDebug)
          printf("require: %s[%d] = \"%.*s\"\n", modules.precord->name, i,
                 MAX_STRING_SIZE - 1, m->content);
        sprintf((char *)(modules.pfield) + i * MAX_STRING_SIZE, "%.*s",
                MAX_STRING_SIZE - 1, m->content);
      }
      if (have_versions) {
        if (requireDebug)
          printf("require: %s[%d] = \"%.*s\"\n", versions.precord->name, i,
                 MAX_STRING_SIZE - 1, m->content + lm);
        sprintf((char *)(versions.pfield) + i * MAX_STRING_SIZE, "%.*s",
                MAX_STRING_SIZE - 1, m->content + lm);
      }
      if (have_modver) {
        if (requireDebug)
          printf("require: %s+=\"%-*s%s\"\n", modver.precord->name,
                 (int)maxModuleNameLength, m->content, m->content + lm);
        c += sprintf((char *)(modver.pfield) + c, "%-*s%s\n",
                     (int)maxModuleNameLength, m->content, m->content + lm);
      }
    }
    if (have_modules) dbGetRset(&modules)->put_array_info(&modules, i);
    if (have_versions) dbGetRset(&versions)->put_array_info(&versions, i);
    if (have_modver) dbGetRset(&modver)->put_array_info(&modver, c + 1);
  }
}

void registerModule(const char *module, const char *version,
                    const char *location) {
  moduleitem *m, **pm;
  size_t lm = strlen(module) + 1;
  size_t lv = (version ? strlen(version) : 0) + 1;
  size_t ll = 1;
  char *absLocation = NULL;
  char *absLocationRequire = NULL;
  char *argstring = NULL;
  const char *mylocation;
  static int firstTime = 1;

  if (requireDebug)
    printf("require: registerModule(%s,%s,%s)\n", module, version, location);

  if (firstTime) {
    initHookRegister(fillModuleListRecord);
    if (requireDebug) printf("require: initHookRegister\n");
    firstTime = 0;
  }

  if (!version) version = "";

  if (location) {
    absLocation = realpathSeparator(location);
    ll = strlen(absLocation) + 1;
  }
  m = (moduleitem *)malloc(sizeof(moduleitem) + lm + lv + ll);
  if (m == NULL) {
    fprintf(stderr, "require: out of memory\n");
    return;
  }

  m->next = NULL;

  strcpy(m->content, module);
  strcpy(m->content + lm, version);
  strcpy(m->content + lm + lv, absLocation ? absLocation : "");

  free(absLocation);
  for (pm = &loadedModules; *pm != NULL; pm = &(*pm)->next) {
  }
  *pm = m;
  if (lm > maxModuleNameLength) maxModuleNameLength = lm;
  moduleListBufferSize += lv;
  moduleCount++;

  putenvprintf("MODULE=%s", module);
  putenvprintf("%s_VERSION=%s", module, version);
  if (location) {
    putenvprintf("%s_DIR=%s", module, m->content + lm + lv);
    pathAdd("SCRIPT_PATH", m->content + lm + lv);
  }

  /* only do registration register stuff at init */
  if (interruptAccept) return;

  /* create a record with the version string */
  mylocation = getenv("require_DIR");
  if (mylocation == NULL) return;
  if (asprintf(&absLocationRequire,
               "%s" OSI_PATH_SEPARATOR "db" OSI_PATH_SEPARATOR
               "moduleversion.template",
               mylocation) < 0)
    return;
  /*
     Require DB has the following four PVs:
     - $(REQUIRE_IOC):$(MODULE)_VER
     - $(REQUIRE_IOC):MOD_VER
     - $(REQUIRE_IOC):VERSIONS
     - $(REQUIRE_IOC):MODULES
     We reserved 30 chars for :$(MODULE)_VER, so MODULE has the maximum 24
     chars. And we've reserved for 30 chars for $(REQUIRE_IOC). So, the whole PV
     and record name in moduleversion.template has 59 + 1.
   */
  if (asprintf(&argstring,
               "REQUIRE_IOC=%.30s, MODULE=%.24s, VERSION=%.39s, "
               "MODULE_COUNT=%lu, BUFFER_SIZE=%lu",
               getenv("REQUIRE_IOC"), module, version, moduleCount,
               moduleListBufferSize + maxModuleNameLength * moduleCount) < 0)
    return;
  printf("Loading module info records for %s\n", module);
  dbLoadRecords(absLocationRequire, argstring);
  free(argstring);
  free(absLocationRequire);
}

#if defined(__linux)
/* This is the Linux link.h, not the EPICS link.h ! */
#include <link.h>

static int findLibRelease(struct dl_phdr_info *info, /* shared library info */
                          size_t size, /* size of info structure */
                          void *data   /* user-supplied arg */
) {
  void *handle;
  char *location = NULL;
  char *p;
  char *version;
  char *symname;
  /* get space for library path + "LibRelease" */
  char name[PATH_MAX + 11];

  (void)data; /* unused */
  if (size < sizeof(struct dl_phdr_info))
    return 0; /* wrong version of struct dl_phdr_info */

  /* find a symbol with a name like "_<module>LibRelease"
     where <module> is from the library name "<location>/lib<module>.so" */

  /* no library name */
  if (info->dlpi_name == NULL || info->dlpi_name[0] == 0) return 0;
  /* get a modifiable copy of the library name */
  strcpy(name, info->dlpi_name);
  /* re-open already loaded library */
  handle = dlopen(info->dlpi_name, RTLD_LAZY);
  /* find file name part in "<location>/lib<module>.so" */
  p = strrchr(name, '/');
  if (p) {
    location = name;
    *++p = 0;
  } else {
    /* terminate "<location>/" (if exists) */
    p = name;
  }
  *(symname = p + 2) = '_';                     /* replace "lib" with "_" */
  p = strchr(symname, '.');                     /* find ".so" extension */
  if (p == NULL) p = symname + strlen(symname); /* no file extension ? */
  strcpy(p, "LibRelease");          /* append "LibRelease" to module name */
  version = dlsym(handle, symname); /* find symbol "_<module>LibRelease" */
  if (version) {
    *p = 0;
    symname++; /* get "<module>" from "_<module>LibRelease" */
    if ((p = strstr(name, "/" LIBDIR)) != NULL)
      p[1] = 0; /* cut "<location>" before LIBDIR */
    if (getLibVersion(symname) == NULL)
      registerModule(symname, version, location);
  }
  dlclose(handle);
  return 0;
}

static void registerExternalModules() {
  /* iterate over all loaded libraries */
  dl_iterate_phdr(findLibRelease, NULL);
}

#else
static void registerExternalModules() { ; }
#endif

size_t foreachLoadedLib(size_t (*func)(const char *name, const char *version,
                                       const char *path, void *arg),
                        void *arg) {
  moduleitem *m;
  int result;

  for (m = loadedModules; m; m = m->next) {
    const char *name = m->content;
    const char *version = name + strlen(name) + 1;
    const char *path = version + strlen(version) + 1;
    result = func(name, version, path, arg);
    if (result) return result;
  }
  return 0;
}

const char *getLibVersion(const char *libname) {
  moduleitem *m;

  for (m = loadedModules; m; m = m->next) {
    if (strcmp(m->content, libname) == 0) {
      return m->content + strlen(m->content) + 1;
    }
  }
  return NULL;
}

const char *getLibLocation(const char *libname) {
  moduleitem *m;
  char *v;

  for (m = loadedModules; m; m = m->next) {
    if (strcmp(m->content, libname) == 0) {
      v = m->content + strlen(m->content) + 1;
      return v + strlen(v) + 1;
    }
  }
  return NULL;
}

int isModuleLoaded(const char *libname) {
  moduleitem *m;

  for (m = loadedModules; m; m = m->next) {
    if (strcmp(m->content, libname) == 0) return TRUE;
  }
  return FALSE;
}

int libversionShow(const char *outfile) {
  moduleitem *m;
  size_t lm, lv;

  FILE *out = epicsGetStdout();

  if (outfile) {
    out = fopen(outfile, "w");
    if (out == NULL) {
      fprintf(stderr, "can't open %s: %s\n", outfile, strerror(errno));
      return -1;
    }
  }
  for (m = loadedModules; m; m = m->next) {
    lm = strlen(m->content) + 1;
    lv = strlen(m->content + lm) + 1;
    fprintf(out, "%-*s%-20s %s\n", (int)maxModuleNameLength, m->content,
            m->content + lm, m->content + lm + lv);
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

#define debug(...) \
  if (requireDebug) printf(__VA_ARGS__)

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
  int match;

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
  semver_t *sv_found, *sv_request;
  int match;

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
static int require_priv(const char *module, const char *version,
                        const char *args);

int require(const char *module, const char *version, const char *args) {
  int status;
  static int firstTime = 1;

  if (firstTime) {
    firstTime = 0;

    set_require_env();

    putenvprintf("T_A=%s", targetArch);
    putenvprintf("EPICS_HOST_ARCH=%s", targetArch);
    putenvprintf("EPICS_RELEASE=%s", epicsRelease);
    putenvprintf("OS_CLASS=%s", osClass);
  }

  if (module == NULL) {
    printf(
        "Usage: require \"<module>\" [, \"<version>\" | \"ifexists\"] [, "
        "\"<args>\"]\n");
    printf("Loads " PREFIX "<module>" INFIX EXT " and <libname>.dbd\n");
    printf("And calls <module>_registerRecordDeviceDriver\n");
    printf("If available, runs startup script snippet (only before iocInit)\n");
    return -1;
  }

  if (interruptAccept) {
    fprintf(stderr, "Error! Modules can only be loaded before iocIint!\n");
    return -1;
  }

  /* either order for version and args, either may be empty or NULL */
  if (version && strchr(version, '=')) {
    const char *v = version;
    version = args;
    args = v;
    if (requireDebug) printf("require: swap version and args\n");
  }

  if (version && version[0] == 0) version = NULL;

  if (version && strcmp(version, "none") == 0) {
    if (requireDebug) printf("require: skip version=none\n");
    return 0;
  }

  status = require_priv(module, version, args);

  if (status == 0) return 0;
  if (status != -1) perror("require");
  if (interruptAccept) return status;

  /* require failed in startup script before iocInit */
  fprintf(stderr, "Aborting startup script\n");
  epicsExit(1);
  return status;
}

static off_t fileSize(const char *filename) {
  struct stat filestat;
  if (stat(filename, &filestat) != 0) {
    if (requireDebug) printf("require: %s does not exist\n", filename);
    return -1;
  }
  switch (filestat.st_mode & S_IFMT) {
    case S_IFREG:
      if (requireDebug)
        printf("require: file %s exists, size %lld bytes\n", filename,
               (unsigned long long)filestat.st_size);
      return filestat.st_size;
    case S_IFDIR:
      if (requireDebug) printf("require: directory %s exists\n", filename);
      return 0;
#ifdef S_IFBLK
    case S_IFBLK:
      if (requireDebug) printf("require: %s is a block device\n", filename);
      return -1;
#endif
#ifdef S_IFCHR
    case S_IFCHR:
      if (requireDebug) printf("require: %s is a character device\n", filename);
      return -1;
#endif
#ifdef S_IFIFO
    case S_IFIFO:
      if (requireDebug) printf("require: %s is a FIFO/pipe\n", filename);
      return -1;
#endif
#ifdef S_IFSOCK
    case S_IFSOCK:
      if (requireDebug) printf("require: %s is a socket\n", filename);
      return -1;
#endif
    default:
      if (requireDebug)
        printf("require: %s is an unknown type of special file\n", filename);
      return -1;
  }
}
#define fileExists(filename) (fileSize(filename) >= 0)
#define fileNotEmpty(filename) (fileSize(filename) > 0)

static int handleDependencies(const char *module, char *depfilename) {
  FILE *depfile;
  char buffer[40];
  char *end;      /* end of string */
  char *rmodule;  /* required module */
  char *rversion; /* required version */

  if (requireDebug)
    printf("require: parsing dependency file %s\n", depfilename);
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
    if (require(rmodule, rversion, NULL) != 0) {
      fclose(depfile);
      return -1;
    }
  }
  fclose(depfile);
  return 0;
}

static int require_priv(const char *module, const char *version,
                        const char *args) {
  int status;
  int returnvalue = 0;
  const char *loaded = NULL;
  const char *found = NULL;
  HMODULE libhandle;
  int ifexists = 0;
  const char *driverpath;
  const char *dirname;
  const char *end;

  int releasediroffs;
  int libdiroffs;
  int extoffs;
  char *founddir = NULL;
  char *symbolname;
  char filename[PATH_MAX];

  int someVersionFound = 0;
  int someArchFound = 0;

  static char *globalTemplates = NULL;

  if (requireDebug)
    printf("require: module=\"%s\" version=\"%s\" args=\"%s\"\n", module,
           version, args);

#if defined __GNUC__ && __GNUC__ < 3
#define TRY_FILE(offs, args...)                                \
  (snprintf(filename + offs, sizeof(filename) - offs, args) && \
   fileExists(filename))

#define TRY_NONEMPTY_FILE(offs, args...)                       \
  (snprintf(filename + offs, sizeof(filename) - offs, args) && \
   fileNotEmpty(filename))
#else
#define TRY_FILE(offs, ...)                                           \
  (snprintf(filename + offs, sizeof(filename) - offs, __VA_ARGS__) && \
   fileExists(filename))

#define TRY_NONEMPTY_FILE(offs, ...)                                  \
  (snprintf(filename + offs, sizeof(filename) - offs, __VA_ARGS__) && \
   fileNotEmpty(filename))
#endif

  driverpath = getenv("EPICS_DRIVER_PATH");
  if (!globalTemplates) {
    char *t = getenv("TEMPLATES");
    if (t) globalTemplates = strdup(t);
  }

  if (driverpath == NULL) driverpath = ".";
  if (requireDebug) printf("require: searchpath=%s\n", driverpath);

  if (version && strcmp(version, "ifexists") == 0) {
    ifexists = 1;
    version = NULL;
  }

  /* check already loaded verion */
  loaded = getLibVersion(module);
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
    dirname = getLibLocation(module);
    if (dirname[0] == 0) return 0;
    if (requireDebug) printf("require: library found in %s\n", dirname);
    snprintf(filename, sizeof(filename), "%s%n", dirname, &releasediroffs);
    putenvprintf("MODULE=%s", module);
    pathAdd("SCRIPT_PATH", dirname);
  } else {
    if (requireDebug) printf("require: no %s version loaded yet\n", module);

    /* Search for module in driverpath */
    for (dirname = driverpath; dirname != NULL; dirname = end) {
      /* get one directory from driverpath */
      int dirlen = 0;
      int modulediroffs = 0;
      DIR_HANDLE dir;
      DIR_ENTRY direntry;

      end = strchr(dirname, OSI_PATH_LIST_SEPARATOR[0]);
      if (end && end[1] == OSI_PATH_SEPARATOR[0] &&
          end[2] == OSI_PATH_SEPARATOR[0]) /* "http://..." and friends */
        end = strchr(end + 2, OSI_PATH_LIST_SEPARATOR[0]);
      if (end)
        dirlen = (int)(end++ - dirname);
      else
        dirlen = (int)strlen(dirname);
      if (dirlen == 0) continue; /* ignore empty driverpath elements */

      if (requireDebug) printf("require: trying %.*s\n", dirlen, dirname);

      snprintf(filename, sizeof(filename),
               "%.*s" OSI_PATH_SEPARATOR "%s" OSI_PATH_SEPARATOR "%n", dirlen,
               dirname, module, &modulediroffs);
      dirlen++;
      /* filename = "<dirname>/[dirlen]<module>/[modulediroffs]" */

      /* Does the module directory exist? */
      IF_OPEN_DIR(filename) {
        if (requireDebug) printf("require: found directory %s\n", filename);

        /* Now look for versions. */
        START_DIR_LOOP {
          char *currentFilename = FILENAME(direntry);

          SKIP_NON_DIR(direntry)
          if (currentFilename[0] == '.')
            continue; /* ignore hidden directories */

          someVersionFound = 1;

          /* Look for highest matching version. */
          if (requireDebug)
            printf("require: checking version %s against required %s\n",
                   currentFilename, version ? version : "");

          switch ((status = compareVersions(currentFilename, version, FALSE))) {
            case MATCH: /* all given numbers match. */
            {
              someArchFound = 1;

              if (requireDebug)
                printf("require: %s %s may match %s\n", module, currentFilename,
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
                if (requireDebug)
                  printf("require: %s %s has no support for %s %s\n", module,
                         currentFilename, epicsRelease, targetArch);
                continue;
              }

              /* Is it higher than the one we found before? */
              if (found && requireDebug)
                printf(
                    "require: %s %s support for %s %s found, compare against "
                    "previously found %s\n",
                    module, currentFilename, epicsRelease, targetArch, found);
              if (!found ||
                  compareVersions(currentFilename, found, TRUE) == HIGHER) {
                if (requireDebug)
                  printf("require: %s %s looks promising\n", module,
                         currentFilename);
                break;
              }
              if (requireDebug)
                printf("require: version %s is lower than %s \n",
                       currentFilename, found);
              continue;
            }
            default: {
              if (requireDebug)
                printf("require: %s %s does not match %s\n", module,
                       currentFilename, version);
              continue;
            }
          }
          /* we have found something */
          free(founddir);
          /* filename = "<dirname>/[dirlen]<module>/[modulediroffs]..." */
          if (asprintf(&founddir, "%.*s%s", modulediroffs, filename,
                       currentFilename) < 0)
            return errno;
          /* founddir = "<dirname>/[dirlen]<module>/[modulediroffs]<version>" */
          found = founddir + modulediroffs; /* version part in the path */
        }
        END_DIR_LOOP
      }
      /* filename = "<dirname>/[dirlen]..." */
      if (!found && requireDebug)
        printf("require: no matching version in %.*s\n", dirlen, filename);
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
      return ifexists ? 0 : -1;
    }

    /* founddir = "<dirname>/[dirlen]<module>/<version>" */
    printf("Module %s version %s found in %s" OSI_PATH_SEPARATOR "\n", module,
           found, founddir);

    /* Step 2 : Looking for  Dep file */
    if (requireDebug) printf("require: looking for dependency file\n");

    if (!TRY_FILE(0,
                  "%s" OSI_PATH_SEPARATOR "%n" LIBDIR "%s" OSI_PATH_SEPARATOR
                  "%n%s.dep",
                  founddir, &releasediroffs, targetArch, &libdiroffs, module)) {
      /* filename =
         "<dirname>/[dirlen]<module>/<version>/R<epicsRelease>/[releasediroffs]/lib/<targetArch>/[libdiroffs]/module.dep"
       */
      fprintf(stderr, "Dependency file %s not found\n", filename);
    } else {
      /* filename =
       * "<dirname>/[dirlen]<module>/<version>/R<epicsRelease>/[releasediroffs]/lib/<targetArch>/[libdiroffs]/module.dep"
       */
      if (handleDependencies(module, filename) == -1) return -1;
    }

    if (requireDebug) printf("require: looking for library file\n");

    if (!(TRY_FILE(libdiroffs, PREFIX "%s" INFIX "%n" EXT, module, &extoffs))) {
      /* filename =
         "<dirname>/[dirlen]<module>/<version>/R<epicsRelease>/[releasediroffs]/lib/<targetArch>/[libdiroffs]/PREFIX<module>INFIX[extoffs](EXT)?"
       */
      printf("Module %s has no library\n", module);
    } else {
      /* filename =
       * "<dirname>/[dirlen]<module>/<version>/R<epicsRelease>/[releasediroffs]/lib/<targetArch>/[libdiroffs]/PREFIX<module>INFIX[extoffs]EXT"
       */
      printf("Loading library %s\n", filename);
      if ((libhandle = loadlib(filename)) == NULL) return -1;

      /* now check what version we really got (with compiled-in version number)
       */
      if (asprintf(&symbolname, "_%sLibRelease", module) < 0) return errno;

      found = (const char *)getAddress(libhandle, symbolname);
      free(symbolname);
      printf("Loaded %s version %s\n", module, found);

      /* check what we got */
      if (requireDebug)
        printf("require: compare requested version %s with loaded version %s\n",
               version, found);
      if (compareVersions(found, version, FALSE) == MISMATCH) {
        fprintf(stderr,
                "Requested %s version %s not available, found only %s.\n",
                module, version, found);
        returnvalue = -1;
        goto require_priv_error;
      }

      /* load dbd file */
      if (TRY_NONEMPTY_FILE(releasediroffs, "dbd" OSI_PATH_SEPARATOR "%s.dbd",
                            module) ||
          TRY_NONEMPTY_FILE(releasediroffs, "%s.dbd", module) ||
          TRY_NONEMPTY_FILE(releasediroffs,
                            ".." OSI_PATH_SEPARATOR "dbd" OSI_PATH_SEPARATOR
                            "%s.dbd",
                            module) ||
          TRY_NONEMPTY_FILE(releasediroffs, ".." OSI_PATH_SEPARATOR "%s.dbd",
                            module) ||
          TRY_NONEMPTY_FILE(releasediroffs,
                            ".." OSI_PATH_SEPARATOR ".." OSI_PATH_SEPARATOR
                            "dbd" OSI_PATH_SEPARATOR "%s.dbd",
                            module)) /* org EPICSbase */ {
        printf("Loading dbd file %s\n", filename);
        if (dbLoadDatabase(filename, NULL, NULL) != 0) {
          fprintf(stderr, "Error loading %s\n", filename);
          returnvalue = -1;
          goto require_priv_error;
        }

        /* when dbd is loaded call register function */
        if (asprintf(&symbolname, "%s_registerRecordDeviceDriver", module) < 0)
          return errno;

        printf("Calling function %s\n", symbolname);
        iocshCmd(symbolname);
        free(symbolname);
      } else {
        /* no dbd file, but that might be OK */
        printf("%s has no dbd file\n", module);
      }
    }
    /* register module with path */
    filename[releasediroffs] = 0;
    registerModule(module, found, filename);
  }

  status = 0;

  if (requireDebug) printf("require: looking for template directory\n");
  /* filename =
   * "<dirname>/[dirlen]<module>/<version>/R<epicsRelease>/[releasediroffs]..."
   */
  if (!((TRY_FILE(releasediroffs, TEMPLATEDIR) ||
         TRY_FILE(releasediroffs, ".." OSI_PATH_SEPARATOR TEMPLATEDIR)) &&
        setupDbPath(module, filename) == 0)) {
    /* if no template directory found, restore TEMPLATES to initial value */
    char *t;
    t = getenv("TEMPLATES");
    if (globalTemplates && (!t || strcmp(globalTemplates, t) != 0))
      putenvprintf("TEMPLATES=%s", globalTemplates);
  }

  if (founddir) free(founddir);

  /* no need to execute startup script twice if not with new arguments */
  if (loaded && args == NULL) {
    return 0;
  }

  return status;

require_priv_error:
  if (founddir) free(founddir);
  return returnvalue;
}

static const iocshFuncDef requireDef = {
    "require", 3,
    (const iocshArg *[]){
        &(iocshArg){"module", iocshArgString},
        &(iocshArg){"[version]", iocshArgString},
        &(iocshArg){"[substitutions]", iocshArgString},
    }};

static void requireFunc(const iocshArgBuf *args) {
  require(args[0].sval, args[1].sval, args[2].sval);
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
    registerExternalModules();
  }
}

epicsExportRegistrar(requireRegister);
epicsExportAddress(int, requireDebug);
