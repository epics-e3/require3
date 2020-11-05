#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <macLib.h>
#include <dbAccess.h>
#include <initHooks.h>
#include <epicsVersion.h>

#define EPICSVER EPICS_VERSION*10000+EPICS_REVISION*100+EPICS_MODIFICATION

#if defined (_WIN32)
#include "asprintf.h"
#endif

#include <osiFileName.h>
#include <iocsh.h>
epicsShareFunc int epicsShareAPI iocshCmd(const char *cmd);
#include <epicsExport.h>

#define IS_ABS_PATH(filename) (filename[0] == OSI_PATH_SEPARATOR[0])  /* may be different for other OS ? */

#include "expr.h"
#include "require.h"

#define SAVEENV(var) do { old_##var = getenv(#var); if (old_##var) old_##var=strdup(old_##var); } while(0)
#define RESTOREENV(var) do { if(old_##var) { putenvprintf("%s=%s", #var, old_##var); free(old_##var); }} while(0)

int runScriptDebug=0;

int runScript(const char* filename, const char* args)
{
    MAC_HANDLE *mac = NULL;
    FILE* file = NULL;
    char* line_raw = NULL;
    char* line_exp = NULL;
    long line_raw_size = 256;
    long line_exp_size = line_raw_size;
    long len;
    char** pairs;
    int status = 0;
    char* old_MODULE = NULL;
    char* old_MODULE_DIR = NULL;
    
    if (!filename)
    {
        fprintf(stderr, "Usage: runScript filename [macro=value,...]\n");
        return -1;
    }
    
    if (macCreateHandle(&mac,(const char*[]){ "", "environ", NULL, NULL }) != 0) goto error;
    macSuppressWarning(mac, 1);

    if ((line_exp = malloc(line_exp_size)) == NULL) goto error;
    if ((line_raw = malloc(line_raw_size)) == NULL) goto error;

    /* add args to macro definitions */
    if (args)
    {
        if (runScriptDebug)
            printf("runScript: macParseDefns \"%s\"\n", args);
        macParseDefns(mac, (char*)args, &pairs);
        macInstallMacros(mac, pairs);
        free(pairs);
    }

    if (IS_ABS_PATH(filename))
    {
        file = fopen(filename, "r");
    }
    else
    {
        const char* dirname;
        const char* end;
        char* fullname;
        const char* path = getenv("SCRIPT_PATH");
        int dirlen;
        
        for (dirname = path; dirname != NULL; dirname = end)
        {
            end = strchr(dirname, OSI_PATH_LIST_SEPARATOR[0]);
            if (end && end[1] == OSI_PATH_SEPARATOR[0] && end[2] == OSI_PATH_SEPARATOR[0])   /* "http://..." and friends */
                end = strchr(end+2, OSI_PATH_LIST_SEPARATOR[0]);
            if (end) dirlen = (int)(end++ - dirname);
            else dirlen = (int)strlen(dirname);
            if (dirlen == 0) continue; /* ignore empty path elements */
            if (dirname[dirlen-1] == OSI_PATH_SEPARATOR[0]) dirlen--;
            asprintf(&fullname, "%.*s" OSI_PATH_SEPARATOR "%s", 
                dirlen, dirname, filename);
            if (runScriptDebug)
                printf("runScript: trying %s\n", fullname);
            file = fopen(fullname, "r");
            if (!file && (errno & 0xffff) != ENOENT) perror(fullname);
            free(fullname);
            if (file) break;
        }
    }
    if (file == NULL) { perror(filename); return errno; }
    
    /* save some environments variables */
    SAVEENV(MODULE);
    SAVEENV(MODULE_DIR);

    /* execute script line by line after expanding macros with arguments or environment */
    while (fgets(line_raw, line_raw_size, file))
    {
        char* p, *x;

        /* check if we have a line longer than the buffer size */
        while (line_raw[(len = (long)strlen(line_raw))-1] != '\n' && !feof(file))
        {
            if (runScriptDebug)
                    printf("runScript partial line: \"%s\"\n", line_raw);
            if ((line_raw = realloc(line_raw, line_raw_size *= 2)) == NULL) goto error;
            if (fgets(line_raw + len, line_raw_size - len, file) == NULL) break;
        }
        while (len > 0 && isspace((unsigned char)line_raw[len-1])) line_raw[--len] = 0; /* get rid of '\n' and friends */
        if (runScriptDebug)
                printf("runScript raw line (%ld chars): '%s'\n", len, line_raw);
        /* expand and check the buffer size (different epics versions write different may number of bytes)*/
        while ((len = labs(macExpandString(mac, line_raw, line_exp, line_exp_size-1))) >= line_exp_size-2)
        {
            if (runScriptDebug)
                printf("runScript: grow expand buffer: len=%ld size=%ld\n", len, line_exp_size);
            free(line_exp);
            if ((line_exp = malloc(line_exp_size *= 2)) == NULL) goto error;
        }
        if (runScriptDebug)
                printf("runScript expanded line (%ld chars): '%s'\n", len, line_exp);
        printf("%s\n", line_exp);
        p = line_exp;
        while (isspace((unsigned char)*p)) p++;
        if (*p == 0 || *p == '#') continue;
        
        /* find local variable assignments */
        if ((x = strpbrk(p, "=(, \t\n\r")) != NULL && *x=='=')
        {
            *x++ = 0;
            replaceExpressions(x, line_raw, line_raw_size);
            if (runScriptDebug)
                printf("runScript: assign %s=%s\n", p, line_raw);
            macPutValue(mac, p, line_raw);
            continue;
        }
        if (runScriptDebug)
            printf("runScript: iocshCmd: '%s'\n", line_exp);
        status = iocshCmd(line_exp);
        if (status != 0) break;
    }
    goto end;
error:
    if (errno)
    {
        status = errno;
        perror("runScript");
    }
end:
    free(line_raw);
    free(line_exp);
    if (mac) macDeleteHandle(mac);
    if (file) fclose(file);

    /* restore environment */
    RESTOREENV(MODULE);
    RESTOREENV(MODULE_DIR);

    return status;
}

struct cmditem
{
    struct cmditem* next;
    int type;
    union {
        char* a[12];
        char cmd[256];
    } x;
} *cmdlist, **cmdlast=&cmdlist;

void afterInitHook(initHookState state)
{
    struct cmditem *item;

    if (state != 
#ifdef INCinitHooksh
        /* old: without iocPause etc */
        initHookAfterInterruptAccept
#else
        /* new: with iocPause etc */
        initHookAfterIocRunning
#endif
        ) return;
    for (item = cmdlist; item != NULL; item = item->next)
    {
        if (item->type == 1)
        {
            printf("%s\n", item->x.cmd);
            iocshCmd(item->x.cmd);
        }
        else
        ((void (*)())item->x.a[0])(item->x.a[1], item->x.a[2], item->x.a[3], item->x.a[4], item->x.a[5],
            item->x.a[6], item->x.a[7], item->x.a[8], item->x.a[9], item->x.a[10], item->x.a[11]);
    }
}


static int first_time = 1;

static struct cmditem *newItem(char* cmd, int type)
{
    struct cmditem *item;
    if (!cmd)
    {
        fprintf(stderr, "usage: afterInit command, args...\n");
        return NULL;
    }
    if (interruptAccept)
    {
        fprintf(stderr, "afterInit can only be used before iocInit\n");
        return NULL;
    } 
    if (first_time)
    {
        first_time = 0;
        initHookRegister(afterInitHook);
    }
    item = malloc(sizeof(struct cmditem));
    if (item == NULL)
    {
        perror("afterInit");
        return NULL;
    }
    item->type = type;
    item->next = NULL;
    *cmdlast = item;
    cmdlast = &item->next;
    return item;
}

int afterInit(char* cmd, char* a1, char* a2, char* a3, char* a4, char* a5, char* a6, char* a7, char* a8, char* a9, char* a10, char* a11)
{
    struct cmditem *item = newItem(cmd, 0);
    if (!item) return -1;
    
    item->x.a[0] = cmd;
    item->x.a[1] = a1;
    item->x.a[2] = a2;
    item->x.a[3] = a3;
    item->x.a[4] = a4;
    item->x.a[5] = a5;
    item->x.a[6] = a6;
    item->x.a[7] = a7;
    item->x.a[8] = a8;
    item->x.a[9] = a9;
    item->x.a[10] = a10;
    item->x.a[11] = a11;

    return 0;
}

epicsExportAddress(int, runScriptDebug);
epicsExportAddress(int, exprDebug);

static const iocshFuncDef runScriptDef = {
    "runScript", 2, (const iocshArg *[]) {
        &(iocshArg) { "filename", iocshArgString },
        &(iocshArg) { "substitutions", iocshArgString },
}};
    
static void runScriptFunc(const iocshArgBuf *args)
{
    runScript(args[0].sval, args[1].sval);
}

static const iocshFuncDef afterInitDef = {
    "afterInit", 1, (const iocshArg *[]) {
        &(iocshArg) { "commandline", iocshArgArgv },
}};
    
static void afterInitFunc(const iocshArgBuf *args)
{
    int i, n;
    struct cmditem *item = newItem(args[0].aval.av[1], 1);
    if (!item) return;

    n = sprintf(item->x.cmd, "%.255s", args[0].aval.av[1]);
    for (i = 2; i < args[0].aval.ac; i++)
    {
        if (strpbrk(args[0].aval.av[i], " ,\"\\"))
            n += sprintf(item->x.cmd+n, " '%.*s'", 255-3-n, args[0].aval.av[i]);
        else
            n += sprintf(item->x.cmd+n, " %.*s", 255-1-n, args[0].aval.av[i]);
    }
}

static void runScriptRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister (&runScriptDef, runScriptFunc);
        iocshRegister (&afterInitDef, afterInitFunc);
    }
}
epicsExportRegistrar(runScriptRegister);
