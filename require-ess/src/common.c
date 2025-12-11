// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright European Spallation Source ERIC
 */

#include <dbAccess.h>
#include <errlog.h>
#include <errno.h>
#ifdef __MACH__
#include <mach/error.h>
#else
#include <error.h>
#endif
#include <limits.h>
#include <osiFileName.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

char *real_path_separator(const char *location) {
  size_t size = 0;
  char *buffer = realpath(location, NULL);
  if (!buffer) {
    debug("realpath(%s) failed.\n", location);
    errlogPrintf("%s\n", strerror(errno));
    return NULL;
  }
  size = strnlen(buffer, PATH_MAX);
  if (buffer[size - 1] != OSI_PATH_SEPARATOR[0]) {
    char *tmp = realloc(buffer, size + sizeof(OSI_PATH_SEPARATOR));
    if (!tmp) {
      free(buffer);
      return NULL;
    }
    buffer = tmp;
    strcpy(buffer + size, OSI_PATH_SEPARATOR);
  }
  return buffer;
}

int put_env_printf(const char *format, ...) {
  va_list ap;
  char *var = NULL;
  char *val = NULL;
  int status = 0;

  if (!format)
    return -1;
  va_start(ap, format);
  if (vasprintf(&var, format, ap) < 0) {
    errlogPrintf("require put_env_printf %s\n", strerror(errno));
    return errno;
  }
  va_end(ap);

  debug("put_env_printf(\"%s\").\n", var);

  val = strchr(var, '=');
  if (!val) {
    fprintf(stderr, "put_env_printf: string contains no =: %s\n", var);
    status = -1;
  } else {
    *val++ = 0;
    if (setenv(var, val, 1) != 0) {
      errlogPrintf("require put_env_printf: setenv failed %s\n",
                   strerror(errno));
      status = errno;
    }
  }
  free(var);
  return status;
}

void path_add(const char *varname, const char *dirname) {
  char *old_path = NULL;

  if (!varname || !dirname) {
    return;
  }

  old_path = getenv(varname);
  if (old_path == NULL) {
    put_env_printf("%s=." OSI_PATH_LIST_SEPARATOR "%s", varname, dirname);
  } else {
    size_t len = strnlen(dirname, PATH_MAX);
    char *p = NULL;

    if (old_path[0] == '.' && old_path[1] == OSI_PATH_LIST_SEPARATOR[0])
      old_path += 2;

    p = old_path;
    while ((p = strstr(p, dirname)) != NULL) {
      if ((p == old_path || *(p - 1) == OSI_PATH_LIST_SEPARATOR[0]) &&
          (p[len] == 0 || p[len] == OSI_PATH_LIST_SEPARATOR[0])) {
        if (p == old_path)
          break; /* already at front, nothing to do */
        memmove(old_path + len + 1, old_path, p - old_path - 1);
        strcpy(old_path, dirname);
        old_path[len] = OSI_PATH_LIST_SEPARATOR[0];
        debug("modified %s=%s.\n", varname, old_path);
        break;
      }
      p += len;
    }
    if (p == NULL)
      put_env_printf("%s=." OSI_PATH_LIST_SEPARATOR "%s" OSI_PATH_LIST_SEPARATOR
                     "%s",
                     varname, dirname, old_path);
  }
}
