/* Copyright (C) 2020 Dirk Zimoch */
/* Copyright (C) 2020-2023 European Spallation Source, ERIC */

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

#define fileExists(filename) (fileSize(filename) >= 0)
#define fileNotEmpty(filename) (fileSize(filename) > 0)
#define TRY_FILE(offs, ...)                                                    \
  (snprintf(filename + offs, PATH_MAX - offs, __VA_ARGS__) &&                  \
   fileExists(filename))

#define TRY_NONEMPTY_FILE(offs, ...)                                           \
  (snprintf(filename + offs, PATH_MAX - offs, __VA_ARGS__) &&                  \
   fileNotEmpty(filename))

off_t fileSize(const char *filename);
int setupDbPath(const char *module, const char *dbdir);
int load_module_dbd(char *filename, const char *module, int filesize);
int require(const char *libname);
int libversionShow(const char *outfile);
int putenvprintf(const char *format, ...)
    __attribute__((__format__(__printf__, 1, 2)));
void pathAdd(const char *varname, const char *dirname);

#ifdef __cplusplus
}
#endif // __cplusplus
