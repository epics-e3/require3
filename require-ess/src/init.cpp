extern "C" {
#include <errlog.h>
#include <iocsh.h>
#include <limits.h>
#include <module.h>
#include <osiFileName.h>
#include <require.h>
#include <stdlib.h>
#include <string.h>

#ifndef MODULE_NAME
#error MODULE_NAME is undefined
#endif

#ifndef LIBVERSION
#error LIBVERSION is undefined
#endif

#define REGISTER_RECORD MODULE_NAME "_registerRecordDeviceDriver"
#define MODULE_PATH OSI_PATH_SEPARATOR MODULE_NAME OSI_PATH_SEPARATOR

/* This function will automatically run after module is loaded
 *
 * It will load the dbd file and register itself on require's linked list.
 */
__attribute__((visibility("default"), used)) char __module_lib_version[] =
    LIBVERSION;
static int __module_library_init() {
  char filename[PATH_MAX] = {0};
  const char *driverpath = NULL;
  driverpath = getenv("REQUIRE_MODULE_PATH");
  if (driverpath == NULL)
    driverpath = ".";

  snprintf(filename, PATH_MAX, "%s" MODULE_PATH, driverpath);
  int dirlen = strnlen(filename, PATH_MAX);
  load_module_dbd(filename, MODULE_NAME, dirlen);
  /* Registration is usually done by <module>_registerRecordDeviceDriver.cpp.
   * However this must be done before iocsh calls the
   * <module>_registerRecordDeviceDriver command. So this needs to be
   * called here. */
  Registration();
  iocshCmd(REGISTER_RECORD);

  /* load_module_dbd changes this string, we set it back by seting the end of
   * string. */
  filename[dirlen] = '\0';
  registerModule(MODULE_NAME, LIBVERSION, filename);

  if (!(TRY_FILE(dirlen, TEMPLATEDIR) &&
        setupDbPath(MODULE_NAME, filename) == 0)) {
    errlogPrintf("%s could not load templates", MODULE_NAME);
    return -1;
  }
  return 0;
}
} // extern "C"

static int done EPICS_UNUSED = __module_library_init();
