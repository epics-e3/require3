#include <dbAccess.h>
#include <errlog.h>
#ifdef __MACH__
#include <mach/error.h>
#else
#include <error.h>
#endif
#include <epicsStdio.h>
#include <errno.h>
#include <iocsh.h>
#include <limits.h>
#include <osiFileName.h>
#include <recSup.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "module.h"

#define MAX_MODULE_SIZE 256
#define RUNTIME_COMPONENTS 2

/* Function pointer type */
typedef const char *(*name_getter_t)(int index);
typedef const char *(*version_getter_t)(int index);

static const ComponentInfo runtimeComponents[] = {
    {"epics-base", "EPICS_VERSION_FULL"}, {"pvxs", "PVXS_VERSION"}};

struct linkedList linked_list = {0};

static int get_record_handle(const char *namepart, short type, DBADDR *paddr) {
  char record_name[PVNAME_STRINGSZ] = {0};

  sprintf(record_name, "%.*s%s",
          (int)(PVNAME_STRINGSZ - strnlen(namepart, PVNAME_STRINGSZ - 1) - 1),
          getenv("REQUIRE_IOC"), namepart);

  if (dbNameToAddr(record_name, paddr) != 0) {
    errlogPrintf("Record %s not found.\n", record_name);
    return -1;
  }
  if (paddr->field_type != type) {
    errlogPrintf("Record %s has wrong type %s instead of %s.\n", record_name,
                 pamapdbfType[paddr->field_type].strvalue,
                 pamapdbfType[type].strvalue);
    return -1;
  }
  if (paddr->pfield == NULL) {
    errlogPrintf("Record %s has not yet allocated memory.\n", record_name);
    return -1;
  }

  return 0;
}

static void fill_record_list(const char *pv_name, const char *pv_version,
                             name_getter_t get_name,
                             version_getter_t get_version, int count) {

  struct dbAddr modules = {0}, versions = {0};
  char *bufferModules, *bufferVersions;

  get_record_handle(pv_name, DBF_STRING, &modules);
  get_record_handle(pv_version, DBF_STRING, &versions);

  bufferModules = (char *)calloc(MAX_STRING_SIZE * count, sizeof(char));
  bufferVersions = (char *)calloc(MAX_STRING_SIZE * count, sizeof(char));

  for (int i = 0; i < count; i++) {
    const char *name = get_name(i);
    const char *version = get_version(i);

    debug("%s[%d] = \"%.*s\"\n", modules.precord->name, i, MAX_STRING_SIZE - 1,
          name);
    sprintf(bufferModules + i * MAX_STRING_SIZE, "%.*s", MAX_STRING_SIZE - 1,
            name);

    debug("%s[%d] = \"%.*s\"\n", versions.precord->name, i, MAX_STRING_SIZE - 1,
          version);
    sprintf(bufferVersions + i * MAX_STRING_SIZE, "%.*s", MAX_STRING_SIZE - 1,
            version);
  }

  if (dbPut(&modules, DBF_STRING, bufferModules, count) != 0)
    errlogPrintf("Error to put %s.\n", modules.precord->name);
  if (dbPut(&versions, DBF_STRING, bufferVersions, count) != 0)
    errlogPrintf("Error to put %s.\n", versions.precord->name);

  free(bufferModules);
  free(bufferVersions);
}

static const char *get_module_name(int index) {
  struct module *m = NULL;
  int i;

  for (m = linked_list.head, i = 0; m != NULL && i < index; m = m->next, i++)
    ;

  return (m && m->name) ? m->name : NULL;
}

static const char *get_module_version(int index) {
  struct module *m = NULL;
  int i;
  for (m = linked_list.head, i = 0; m != NULL && i < index; m = m->next, i++)
    ;

  return (m && m->version) ? m->version : NULL;
}

static const char *get_runtime_name(int index) {
  return runtimeComponents[index].component;
}

static const char *get_runtime_version(int index) {
  const char *ver = getenv(runtimeComponents[index].env_var);
  return ver ? ver : NULL;
}

void fill_module_list_record(initHookState state) {
  if (state != initHookAfterFinishDevSup)
    return;

  fill_record_list(":#Modules", ":#Versions", get_module_name,
                   get_module_version, linked_list.size);
}

void fill_runtime_components_list_record(initHookState state) {
  if (state != initHookAfterFinishDevSup)
    return;

  fill_record_list(":#Components", ":#ComponentsVersions", get_runtime_name,
                   get_runtime_version, RUNTIME_COMPONENTS);
}

const char *get_lib_version(const char *libname) {
  struct module *m = NULL;
  for (m = linked_list.head; m; m = m->next) {
    if (strcmp(m->name, libname) == 0) {
      return m->version;
    }
  }
  return NULL;
}

const char *get_lib_location(const char *libname) {
  struct module *m = NULL;
  for (m = linked_list.head; m; m = m->next) {
    if (strcmp(m->name, libname) == 0) {
      return m->path;
    }
  }
  return NULL;
}

int is_module_loaded(const char *libname) {
  struct module *m = NULL;
  for (m = linked_list.head; m; m = m->next) {
    if (strcmp(m->name, libname) == 0)
      return TRUE;
  }
  return FALSE;
}

int register_module(const char *moduleName, const char *version,
                    const char *location) {
  char *abslute_path = NULL;
  char *require_absolute_path = NULL;
  char *template_arguments = NULL;
  const char *require_custom_path = NULL;

  /* require should be called only before iocInit. */
  if (interruptAccept)
    return 0;

  debug("registerModule(%s,%s,%s)\n", moduleName, version, location);

  if (!moduleName)
    return -1;
  if (!version)
    version = "";

  if (location) {
    abslute_path = real_path_separator(location);
  }
  if (!abslute_path) {
    return -1;
  }
  struct module *module = NULL;
  if (!(module = (struct module *)calloc(sizeof(struct module), 1))) {
    goto out_of_memory;
  }

  /* Check if string is well formated, there is a \0 in the next MAX_MODULE_SIZE
     bytes.  */
  int nameSize = strnlen(moduleName, MAX_MODULE_SIZE) + 1;
  if (nameSize > MAX_MODULE_SIZE)
    return -1;
  if (!(module->name = calloc(nameSize, sizeof(char)))) {
    goto free_module;
  }
  strcpy(module->name, moduleName);

  int versionSize = strnlen(version, MAX_MODULE_SIZE) + 1;
  if (versionSize > MAX_MODULE_SIZE)
    return -1;
  if (!(module->version = calloc(versionSize, sizeof(char)))) {
    goto free_name;
  }
  strcpy(module->version, version);

  if (!(module->path =
            calloc(strnlen(abslute_path, PATH_MAX) + 1, sizeof(char)))) {
    goto free_version;
  }
  strcpy(module->path, abslute_path ? abslute_path : "");
  free(abslute_path);

  put_env_printf("MODULE=%s", module->name);
  put_env_printf("%s_VERSION=%s", module->name, module->version);
  if (location) {
    put_env_printf("%s_DIR=%s", module->name, module->path);
    path_add("SCRIPT_PATH", module->path);
  }

  /* Does not add require on the "modules list" */
  if (strcmp(moduleName, "require") != 0) {
    if (linked_list.size == 0) {
      linked_list.head = module;
    } else {
      linked_list.tail->next = module;
    }
    linked_list.tail = module;
    linked_list.size++;
  }

  require_custom_path = getenv("require_DIR");
  if (require_custom_path == NULL)
    return 0;
  if (asprintf(&require_absolute_path,
               "%s" OSI_PATH_SEPARATOR "db" OSI_PATH_SEPARATOR
               "moduleversion.template",
               require_custom_path) < 0)
    return 0;
  /*
   * Require DB has the following two PVs:
   * - $(REQUIRE_IOC):Versions
   * - $(REQUIRE_IOC):Modules
   * We've reserved for 30 chars for $(REQUIRE_IOC).
   */
  if (asprintf(&template_arguments, "REQUIRE_IOC=%.30s,MODULE_COUNT=%u",
               getenv("REQUIRE_IOC"), linked_list.size) < 0) {
    errlogPrintf("Error asprintf failed\n");
    return 0;
  }
  printf("Loading module info records for %s.\n", module->name);
  dbLoadRecords(require_absolute_path, template_arguments);
  free(template_arguments);
  free(require_absolute_path);
  return 0;

free_version:
  free(module->version);
free_name:
  free(module->name);
free_module:
  free(module);
out_of_memory:
  errlogPrintf("Out of memory.\n");
  return -1;
}
