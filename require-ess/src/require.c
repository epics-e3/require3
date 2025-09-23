/* Copyright (C) 2020 Dirk Zimoch */
/* Copyright (C) 2020-2022 European Spallation Source, ERIC */

#include "require.h"

#include <ctype.h>
#include <dbAccess.h>
#include <dirent.h>
#include <dlfcn.h>
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
#error Distro not supported
#endif

#define PREFIX "lib"
#ifdef __MACH__
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
#else
#error Only unix systems are supported
#endif

const char os_class[] = OS_CLASS;
char epics_release[80];
char *target_arch;

void set_require_env() {
  char *epics_version_major = getenv("EPICS_VERSION_MAJOR");
  char *epics_version_middle = getenv("EPICS_VERSION_MIDDLE");
  char *epics_version_minor = getenv("EPICS_VERSION_MINOR");

  sprintf(epics_release, "%s.%s.%s", epics_version_major, epics_version_middle,
          epics_version_minor);
  target_arch = getenv("EPICS_HOST_ARCH");

  put_env_printf("T_A=%s", target_arch);
  put_env_printf("EPICS_HOST_ARCH=%s", target_arch);
  put_env_printf("EPICS_RELEASE=%s", epics_release);
  put_env_printf("OS_CLASS=%s", os_class);
}

/* Set up db search path environment variables
 * <module>_DB             template path of <module>
 * TEMPLATES               template path of the current module (overwritten)
 * EPICS_DB_INCLUDE_PATH   template path of all loaded modules (last in front
 * after ".")
 */
int setup_db_path(const char *module, const char *db_directory) {
  char *absolute_path = real_path_separator(
      db_directory); /* so we can change directory later safely */
  if (absolute_path == NULL) {
    debug("require: cannot resolve %s\n", db_directory);
    return -1;
  }

  debug("require: found template directory %s\n", absolute_path);

  put_env_printf("%s_DB=%s", module, absolute_path);
  put_env_printf("TEMPLATES=%s", absolute_path);
  if (is_module_loaded("stream")) {
    path_add("STREAM_PROTOCOL_PATH", absolute_path);
  }
  path_add("EPICS_DB_INCLUDE_PATH", absolute_path);
  free(absolute_path);
  return 0;
}

/* require (module)
 * Look if module is already loaded.
 * If module is not yet loaded load the library with ld
 * and check if module was build with init.cpp.

 * If require is called from the iocsh before iocInit and fails,
 * it calls epicsExit to abort the application.
*/
static int require_priv(const char *module);

int require(const char *module) {
  int status = 0;

  if (module == NULL) {
    printf("Usage: require \"<module>\"\n");
    printf("Loads " PREFIX "<module>" EXT " and <libname>.dbd\n");
    printf("And calls <module>_registerRecordDeviceDriver\n");
    return -1;
  }

  /* interruptAccept is a global variable from EPICS Base*/
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

  errlogPrintf("Aborting startup script\n");
  epicsExit(1);
  return status;
}

off_t file_size(const char *filename) {
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
 * Loads the module.dbd file.
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
    errlogPrintf(PREFIX "%s" EXT " is not an EPICS module.\n", module);
    return -1;
  }
  return 0;
}

static const iocshFuncDef require_def = {
    "require", 1, (const iocshArg *[]){&(iocshArg){"module", iocshArgString}},
    "Usage: require <module>\n"
    "Load the specified module and register it.\n"
    "Must be used before iocInit.\n"};

static void require_func(const iocshArgBuf *args) { require(args[0].sval); }

static void requireRegister(void) {
  static int first_time = 1;
  if (first_time) {
    first_time = 0;
    iocshRegister(&require_def, require_func);

    set_require_env();
    initHookRegister(fill_module_list_record);
  }
}

epicsExportRegistrar(requireRegister);
epicsExportAddress(int, requireDebug);
