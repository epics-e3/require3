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

void fill_module_list_record(initHookState state);
const char *get_lib_version(const char *libname);
const char *get_lib_location(const char *libname);
int is_module_loaded(const char *libname);
int register_module(const char *module, const char *version,
                    const char *location);

#endif /*__MODULE_H__*/
