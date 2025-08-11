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

unsigned long int bufferSize = 0;

struct linkedList linkedlist = {0};

static int getRecordHandle(const char *namepart, short type, DBADDR *paddr) {
  char recordname[PVNAME_STRINGSZ] = {0};

  sprintf(recordname, "%.*s%s",
          (int)(PVNAME_STRINGSZ - strnlen(namepart, PVNAME_STRINGSZ - 1) - 1),
          getenv("REQUIRE_IOC"), namepart);

  if (dbNameToAddr(recordname, paddr) != 0) {
    errlogPrintf("require:getRecordHandle : record %s not found\n", recordname);
    return -1;
  }
  if (paddr->field_type != type) {
    errlogPrintf(
        "require:getRecordHandle : record %s has wrong type %s instead of %s\n",
        recordname, pamapdbfType[paddr->field_type].strvalue,
        pamapdbfType[type].strvalue);
    return -1;
  }
  if (paddr->pfield == NULL) {
    errlogPrintf(
        "require:getRecordHandle : record %s has not yet allocated memory\n",
        recordname);
    return -1;
  }

  return 0;
}

int libversionShow(const char *outfile) {
  struct module *m = NULL;

  FILE *out = epicsGetStdout();

  if (outfile) {
    out = fopen(outfile, "w");
    if (out == NULL) {
      errlogPrintf("can't open %s: %s\n", outfile, strerror(errno));
      return -1;
    }
  }
  for (m = linkedlist.head; m; m = m->next) {
    fprintf(out, "%s-%20s %s\n", m->name, m->version, m->path);
  }
  if (fflush(out) < 0 && outfile) {
    errlogPrintf("can't write to %s: %s\n", outfile, strerror(errno));
    return -1;
  }
  if (outfile)
    fclose(out);
  return 0;
}

/*
We can fill the records only after they have been initialized, at
initHookAfterFinishDevSup. But use double indirection here because in 3.13 we
must wait until initHooks is loaded before we can register the hook.
*/
void fillModuleListRecord(initHookState state) {
  if (state != initHookAfterFinishDevSup)
    return;

  struct dbAddr modules = {0}, versions = {0}, modver = {0};
  char *bufferModules, *bufferVersions, *bufferModver;
  struct module *m = NULL;
  int i = 0;
  int c = 0;

  getRecordHandle(":Modules", DBF_STRING, &modules);
  getRecordHandle(":Versions", DBF_STRING, &versions);
  getRecordHandle(":ModuleVersions", DBF_CHAR, &modver);

  bufferModules =
      (char *)calloc(MAX_STRING_SIZE * linkedlist.size, sizeof(char));
  bufferVersions =
      (char *)calloc(MAX_STRING_SIZE * linkedlist.size, sizeof(char));
  bufferModver =
      (char *)calloc(MAX_STRING_SIZE * linkedlist.size, sizeof(char));

  for (m = linkedlist.head, i = 0; m != NULL; m = m->next, i++) {
    debug("require: %s[%d] = \"%.*s\"\n", modules.precord->name, i,
          MAX_STRING_SIZE - 1, m->name);
    sprintf((char *)(bufferModules) + i * MAX_STRING_SIZE, "%.*s",
            MAX_STRING_SIZE - 1, m->name);
    debug("require: %s[%d] = \"%.*s\"\n", versions.precord->name, i,
          MAX_STRING_SIZE - 1, m->version);
    sprintf((char *)(bufferVersions) + i * MAX_STRING_SIZE, "%.*s",
            MAX_STRING_SIZE - 1, m->version);
    debug("require: %s+=\"%s %s\"\n", modver.precord->name, m->name,
          m->version);
    c += sprintf((char *)(bufferModver) + c, "%s %s\n", m->name, m->version);
  }

  if (dbPut(&modules, DBF_STRING, bufferModules, linkedlist.size) != 0) {
    errlogPrintf("require: Error to put Modules\n");
  }
  if (dbPut(&versions, DBF_STRING, bufferVersions, linkedlist.size) != 0) {
    errlogPrintf("require: Error to put Versions\n");
  }
  if (dbPut(&modver, DBF_CHAR, bufferModver, strlen(bufferModver)) != 0) {
    errlogPrintf("require: Error to put ModuleVersions\n");
  }

  free(bufferModules);
  free(bufferVersions);
  free(bufferModver);
}

const char *getLibVersion(const char *libname) {
  struct module *m = NULL;
  for (m = linkedlist.head; m; m = m->next) {
    if (strcmp(m->name, libname) == 0) {
      return m->version;
    }
  }
  return NULL;
}

const char *getLibLocation(const char *libname) {
  struct module *m = NULL;
  for (m = linkedlist.head; m; m = m->next) {
    if (strcmp(m->name, libname) == 0) {
      return m->path;
    }
  }
  return NULL;
}

int isModuleLoaded(const char *libname) {
  struct module *m = NULL;
  for (m = linkedlist.head; m; m = m->next) {
    if (strcmp(m->name, libname) == 0)
      return TRUE;
  }
  return FALSE;
}

int registerModule(const char *moduleName, const char *version,
                   const char *location) {
  char *absLocation = NULL;
  char *absLocationRequire = NULL;
  char *argstring = NULL;
  const char *mylocation = NULL;

  /* require should be called only before iocInit. */
  if (interruptAccept)
    return 0;

  debug("require: registerModule(%s,%s,%s)\n", moduleName, version, location);

  if (!moduleName)
    return -1;
  if (!version)
    version = "";

  if (location) {
    absLocation = realpathSeparator(location);
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
    goto out_of_memory;
  }
  strcpy(module->name, moduleName);

  int versionSize = strnlen(version, MAX_MODULE_SIZE) + 1;
  if (versionSize > MAX_MODULE_SIZE)
    return -1;
  if (!(module->version = calloc(versionSize, sizeof(char)))) {
    goto out_of_memory;
  }
  strcpy(module->version, version);

  if (!(module->path =
            calloc(strnlen(absLocation, PATH_MAX) + 1, sizeof(char)))) {
    goto out_of_memory;
  }
  strcpy(module->path, absLocation ? absLocation : "");
  free(absLocation);

  /* This bufferSize is used to calculate the ModuleVersions buffer size.  It
   * will be updated every time we call dbLoadRecords at the end of this
   * function.  The size here will be calculated based on the string that is
   * being written in fillModuleListRecord.  So the magic number here is related
   * to that string format.*/
  bufferSize += nameSize + versionSize + 2;
  if (linkedlist.size == 0) {
    linkedlist.head = module;
  } else {
    linkedlist.tail->next = module;
  }
  linkedlist.tail = module;
  linkedlist.size++;

  putenvprintf("MODULE=%s", module->name);
  putenvprintf("%s_VERSION=%s", module->name, module->version);
  if (location) {
    putenvprintf("%s_DIR=%s", module->name, module->path);
    pathAdd("SCRIPT_PATH", module->path);
  }

  /* create a record with the version string */
  mylocation = getenv("require_DIR");
  if (mylocation == NULL)
    return 0;
  if (asprintf(&absLocationRequire,
               "%s" OSI_PATH_SEPARATOR "db" OSI_PATH_SEPARATOR
               "moduleversion.template",
               mylocation) < 0)
    return 0;
  /*
     Require DB has the following four PVs:
     - $(REQUIRE_IOC):$(MODULE)Version
     - $(REQUIRE_IOC):ModuleVersions
     - $(REQUIRE_IOC):Versions
     - $(REQUIRE_IOC):Modules
     We reserved 30 chars for :$(MODULE)Version, so MODULE has the maximum 24
     chars. And we've reserved for 30 chars for $(REQUIRE_IOC). So, the whole PV
     and record name in moduleversion.template has 59 + 1.
   */
  if (asprintf(&argstring,
               "REQUIRE_IOC=%.30s, MODULE=%.24s, VERSION=%.39s, "
               "MODULE_COUNT=%u, BUFFER_SIZE=%lu",
               getenv("REQUIRE_IOC"), module->name, module->version,
               linkedlist.size, bufferSize) < 0) {
    errlogPrintf("Error asprintf failed\n");
    return 0;
  }
  printf("Loading module info records for %s\n", module->name);
  dbLoadRecords(absLocationRequire, argstring);
  free(argstring);
  free(absLocationRequire);
  return 0;

out_of_memory:
  errlogPrintf("require: out of memory\n");
  return -1;
}
