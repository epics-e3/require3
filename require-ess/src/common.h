#ifndef __COMMON_H__
#define __COMMON_H__
#include <stdio.h>

extern int requireDebug;

#define debug(...)                                                             \
  if (requireDebug)                                                            \
  printf(__VA_ARGS__)

char *realpathSeparator(const char *location);
int putenvprintf(const char *format, ...);
void pathAdd(const char *varname, const char *dirname);
#endif /*__COMMON_H_*/
