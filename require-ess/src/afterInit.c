/* Copyright (C) 2020 Dirk Zimoch */
/* Copyright (C) 2020-2023 European Spallation Source, ERIC */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dbAccess.h>
#include <epicsExport.h>
#include <epicsStdio.h>
#include <errlog.h>
#include <errno.h>
#include <initHooks.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iocsh.h>
DBCORE_API int epicsStdCall iocshCmd(const char *cmd);

struct cmditem {
  struct cmditem *next;
  char cmd[256];
} *cmdlist, **cmdlast = &cmdlist;

void afterInitHook(initHookState state) {
  struct cmditem *item;

  if (state != initHookAfterIocRunning) return;
  for (item = cmdlist; item != NULL; item = item->next) {
    printf("%s\n", item->cmd);
    iocshCmd(item->cmd);
  }
}

static struct cmditem *newItem(char *cmd) {
  static int first_time = 1;
  struct cmditem *item;
  if (!cmd) {
    errlogPrintf("usage: afterInit command, args...\n");
    return NULL;
  }
  if (interruptAccept) {
    errlogPrintf("afterInit can only be used before iocInit\n");
    return NULL;
  }
  if (first_time) {
    first_time = 0;
    initHookRegister(afterInitHook);
  }
  item = malloc(sizeof(struct cmditem));
  if (item == NULL) {
    errlogPrintf("afterInit %s", strerror(errno));
    return NULL;
  }
  item->next = NULL;
  *cmdlast = item;
  cmdlast = &item->next;
  return item;
}

static const iocshFuncDef afterInitDef = {
    "afterInit", 1,
    (const iocshArg *[]){
        &(iocshArg){"commandline", iocshArgArgv},
    }};

static void afterInitFunc(const iocshArgBuf *args) {
  struct cmditem *item = newItem(args[0].aval.av[1]);
  if (!item) return;

  int n = sprintf(item->cmd, "%.255s", args[0].aval.av[1]);
  for (int i = 2; i < args[0].aval.ac; i++) {
    if (strpbrk(args[0].aval.av[i], " ,\"\\"))
      n += sprintf(item->cmd + n, " '%.*s'", 255 - 3 - n, args[0].aval.av[i]);
    else
      n += sprintf(item->cmd + n, " %.*s", 255 - 1 - n, args[0].aval.av[i]);
  }
}

static void afterInitRegister(void) {
  static int firstTime = 1;
  if (firstTime) {
    firstTime = 0;
    iocshRegister(&afterInitDef, afterInitFunc);
  }
}
epicsExportRegistrar(afterInitRegister);
