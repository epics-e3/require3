#ifndef __MODULE_H__
#define __MODULE_H__
#include <initHooks.h>

struct module {
  struct module *next;
  char *name;
  char *version;
  char *path;
};

struct linkedList {
  struct module *head;
  struct module *tail;
  unsigned int size;
};

void fillModuleListRecord(initHookState state);
const char *getLibVersion(const char *libname);
const char *getLibLocation(const char *libname);
int isModuleLoaded(const char *libname);
int registerModule(const char *module, const char *version,
                   const char *location);

#endif /*__MODULE_H__*/
