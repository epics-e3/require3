#ifndef __MODULE_H__
#define __MODULE_H__

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

const char *getLibVersion(struct linkedList *linkedlist, const char *libname);
const char *getLibLocation(struct linkedList *linkedlist, const char *libname);
int isModuleLoaded(struct linkedList *linkedlist, const char *libname);
int registerModule(struct linkedList *linkedlist, const char *module,
                   const char *version, const char *location);

#endif /*__MODULE_H__*/
