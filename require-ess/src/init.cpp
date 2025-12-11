// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright European Spallation Source ERIC
 */

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
  const char *modules_path = NULL;
  modules_path = getenv("REQUIRE_MODULE_PATH");
  if (modules_path == NULL)
    modules_path = ".";

  snprintf(filename, PATH_MAX, "%s" MODULE_PATH, modules_path);
  int directory_length = strnlen(filename, PATH_MAX);
  load_module_dbd(filename, MODULE_NAME, directory_length);
  /* Registration is usually done by <module>_registerRecordDeviceDriver.cpp.
   * However this must be done before iocsh calls the
   * <module>_registerRecordDeviceDriver command. So this needs to be
   * called here. */
#ifndef NO_REGISTRATION
  Registration();
  iocshCmd(REGISTER_RECORD);
#endif

  /* load_module_dbd changes this string, we set it back by seting the end of
   * string. */
  filename[directory_length] = '\0';
  register_module(MODULE_NAME, LIBVERSION, filename);

  if (!(TRY_FILE(directory_length, TEMPLATEDIR) &&
        setup_db_path(MODULE_NAME, filename) == 0)) {
    errlogPrintf("No template path found for %s. Skipping.\n", MODULE_NAME);
    return -1;
  }
  return 0;
}
} // extern "C"

static int done EPICS_UNUSED = __module_library_init();
