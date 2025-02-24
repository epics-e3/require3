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

char *realpathSeparator(const char *location) {
  size_t size = 0;
  char *buffer = realpath(location, NULL);
  if (!buffer) {
    debug("require: realpath(%s) failed\n", location);
    errlogPrintf("require: %s", strerror(errno));
    return NULL;
  }
  size = strnlen(buffer, PATH_MAX);
  /* linux realpath removes trailing slash */
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

int putenvprintf(const char *format, ...) {
  va_list ap;
  char *var = NULL;
  char *val = NULL;
  int status = 0;

  if (!format)
    return -1;
  va_start(ap, format);
  if (vasprintf(&var, format, ap) < 0) {
    errlogPrintf("require putenvprintf %s", strerror(errno));
    return errno;
  }
  va_end(ap);

  debug("require: putenv(\"%s\")\n", var);

  val = strchr(var, '=');
  if (!val) {
    fprintf(stderr, "putenvprintf: string contains no =: %s\n", var);
    status = -1;
  } else {
    *val++ = 0;
    if (setenv(var, val, 1) != 0) {
      errlogPrintf("require putenvprintf: setenv failed %s", strerror(errno));
      status = errno;
    }
  }
  free(var);
  return status;
}

void pathAdd(const char *varname, const char *dirname) {
  char *old_path = NULL;

  if (!varname || !dirname) {
    errlogPrintf("usage: pathAdd \"ENVIRONMENT_VARIABLE\",\"directory\"\n");
    errlogPrintf("       Adds or moves the directory to the front of the "
                 "ENVIRONMENT_VARIABLE\n");
    errlogPrintf("       but after a leading \".\".\n");
    return;
  }

  /* add directory to front */
  old_path = getenv(varname);
  if (old_path == NULL) {
    putenvprintf("%s=." OSI_PATH_LIST_SEPARATOR "%s", varname, dirname);
  } else {
    size_t len = strnlen(dirname, PATH_MAX);
    char *p = NULL;

    /* skip over "." at the beginning */
    if (old_path[0] == '.' && old_path[1] == OSI_PATH_LIST_SEPARATOR[0])
      old_path += 2;

    /* If directory is already in path, move it to front */
    p = old_path;
    while ((p = strstr(p, dirname)) != NULL) {
      if ((p == old_path || *(p - 1) == OSI_PATH_LIST_SEPARATOR[0]) &&
          (p[len] == 0 || p[len] == OSI_PATH_LIST_SEPARATOR[0])) {
        if (p == old_path)
          break; /* already at front, nothing to do */
        memmove(old_path + len + 1, old_path, p - old_path - 1);
        strcpy(old_path, dirname);
        old_path[len] = OSI_PATH_LIST_SEPARATOR[0];
        debug("require: modified %s=%s\n", varname, old_path);
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
