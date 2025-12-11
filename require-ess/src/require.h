// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Based on original work by Dirk Zimoch, Paul Scherrer Institute
 * Modified by European Spallation Source ERIC
 */

#pragma once

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#ifndef __GNUC__
#define __attribute__(dummy)
#endif // __GNUC__

#define LIBDIR "lib" OSI_PATH_SEPARATOR
#define DEPDIR "dep" OSI_PATH_SEPARATOR
#define TEMPLATEDIR "db"
#define LIBRELEASE "LibRelease"

#define FILE_EXISTS(filename) (file_size(filename) >= 0)
#define FILE_NOT_EXISTS(filename) (file_size(filename) > 0)
#define TRY_FILE(offs, ...)                                                    \
  (snprintf(filename + offs, PATH_MAX - offs, __VA_ARGS__) &&                  \
   FILE_EXISTS(filename))

#define TRY_NONEMPTY_FILE(offs, ...)                                           \
  (snprintf(filename + offs, PATH_MAX - offs, __VA_ARGS__) &&                  \
   FILE_NOT_EXISTS(filename))

off_t file_size(const char *filename);
int setup_db_path(const char *module, const char *dbdir);
int load_module_dbd(char *filename, const char *module, int filesize);
int require(const char *libname);
int put_env_printf(const char *format, ...)
    __attribute__((__format__(__printf__, 1, 2)));
void path_add(const char *varname, const char *dirname);

#ifdef __cplusplus
}
#endif // __cplusplus
