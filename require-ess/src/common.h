// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright European Spallation Source ERIC
 */

#ifndef __COMMON_H__
#define __COMMON_H__
#include <stdio.h>

extern int requireDebug;

#define debug(fmt, ...)                                                        \
  if (requireDebug)                                                            \
  printf("%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

char *real_path_separator(const char *location);
int put_env_printf(const char *format, ...);
void path_add(const char *varname, const char *dirname);
#endif /*__COMMON_H_*/
