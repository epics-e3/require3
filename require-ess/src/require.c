/* Copyright (C) 2020 Dirk Zimoch */
/* Copyright (C) 2020-2022 European Spallation Source, ERIC */

#include "require.h"

#include <ctype.h>
#include <dbAccess.h>
#include <dlfcn.h>
#include <dirent.h>
#include <epicsExit.h>
#include <epicsExport.h>
#include <epicsStdio.h>
#include <epicsVersion.h>
#include <errlog.h>
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

int requireDebug;

#if defined(__unix) || defined(UNIX)
#ifndef OS_CLASS
#if defined(__linux) || defined(linux)
#define OS_CLASS "Linux"
#endif // __linux
#ifdef __MACH__
#define OS_CLASS "Darwin"
#define PATH_MAX 1024
#endif // darwin
#else
#error Only support Unix based distros
#endif

#define PREFIX "lib"
#ifdef _MACH__
#define EXT ".dylib"
#else
#define EXT ".so"
#endif

#define E3_REQUIRE_LOCATION "E3_REQUIRE_LOCATION"
#define E3_REQUIRE_VERSION "E3_REQUIRE_VERSION"
#define E3_SYMBOL "__module_lib_version"

#ifndef OS_CLASS
#error OS_CLASS not defined
#endif // OS_CLASS

const char osClass[] = OS_CLASS;
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

int setupDbPath(const char *module, const char *dbdir) {
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
  if (isModuleLoaded("stream")) {
    pathAdd("STREAM_PROTOCOL_PATH", absdir);
  }
  pathAdd("EPICS_DB_INCLUDE_PATH", absdir);
  free(absdir);
  return 0;
}

/* require (module)
Look if module is already loaded.
If module is not yet loaded load the library with ld,
load <module>.dbd with dbLoadDatabase (if file exists)
and call <module>_registerRecordDeviceDriver function.

If require is called from the iocsh before iocInit and fails,
it calls epicsExit to abort the application.
*/

/* wrapper to abort statup script */
static int require_priv(const char *module);

int require(const char *module) {
  int status = 0;

  if (module == NULL) {
    printf("Usage: require \"<module>\"\n");
    printf("Loads " PREFIX "<module>" EXT " and <libname>.dbd\n");
    printf("And calls <module>_registerRecordDeviceDriver\n");
    return -1;
  }

  if (interruptAccept) {
    errlogPrintf("Error! Modules can only be loaded before iocIint!\n");
    return -1;
  }

  status = require_priv(module);

  if (status == 0)
    return 0;
  if (status != -1)
    perror("require");
  if (interruptAccept)
    return status;

  /* require failed in startup script before iocInit */
  errlogPrintf("Aborting startup script\n");
  epicsExit(1);
  return status;
}

off_t fileSize(const char *filename) {
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

/*
 * Loads the module .dbd file and runs registerRecordDeviceDriver.
 */
int load_module_dbd(char *filename, const char *module, int filesize) {
  /* load dbd file */
  if (TRY_FILE(filesize, "dbd" OSI_PATH_SEPARATOR "%s.dbd", module)) {
    printf("Loading dbd file %s\n", filename);
    if (dbLoadDatabase(filename, NULL, NULL) != 0) {
      errlogPrintf("Error loading %s\n", filename);
      return -1;
    }
  } else {
    /* no dbd file, but that might be OK */
    printf("%s has no dbd file\n", module);
  }
  return 0;
}

static int require_priv(const char *module) {
  void *lib_handle = NULL;
  char lib[PATH_MAX] = {0};
  void *symbol_address = NULL;
  char *dlsym_error = NULL;

  debug("require: module=\"%s\"\n", module);
  /* Load required librarie */
  debug("require: Load the library if file exists\n");
  snprintf(lib, PATH_MAX, PREFIX "%s" EXT, module);
  lib_handle = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
  if (lib_handle == NULL) {
    debug("require: Module not found\n");
    return -1;
  }
  symbol_address = dlsym(lib_handle, E3_SYMBOL);
  dlsym_error = dlerror();
  if (dlsym_error != NULL || symbol_address == NULL) {
    dlclose(lib_handle);
    errlogPrintf(PREFIX "%s" EXT " is not an EPICS module.", module);
    return -1;
  }
  return 0;
}

static const iocshFuncDef requireDef = {
    "require", 1,
    (const iocshArg *[]){
        &(iocshArg){"module", iocshArgString},
    }};

static void requireFunc(const iocshArgBuf *args) { require(args[0].sval); }

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
    iocshRegister(&pathAddDef, pathAddFunc);

    set_require_env();
    initHookRegister(fillModuleListRecord);
  }
}

epicsExportRegistrar(requireRegister);
epicsExportAddress(int, requireDebug);
