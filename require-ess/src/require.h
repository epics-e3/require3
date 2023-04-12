/* Copyright (C) 2020 Dirk Zimoch */
/* Copyright (C) 2020-2023 European Spallation Source, ERIC */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#ifndef __GNUC__
#define __attribute__(dummy)
#endif  // __GNUC__

int require(const char *libname, const char *version, const char *args);
size_t foreachLoadedLib(size_t (*func)(const char *name, const char *version,
                                       const char *path, void *arg),
                        void *arg);
const char *getLibVersion(const char *libname);
const char *getLibLocation(const char *libname);
int libversionShow(const char *outfile);
int putenvprintf(const char *format, ...)
    __attribute__((__format__(__printf__, 1, 2)));
void pathAdd(const char *varname, const char *dirname);

#ifdef __cplusplus
}
#endif  // __cplusplus
