#ifndef TOBSERVMODULE_H
#define TOBSERVMODULE_H

//max number of chars a section name, switch name or replace name can have
#define MAX_VARIABLE_LENGTH 100
#define SESSION_NAME_SIZE 128
#define SESSION_VALUE_SIZE 1024
#define MODULE_NAME_SIZE 128

#include <stdio.h>
#include <malloc.h>
#include <pthread.h>
#include <string.h>
#include "tobFUNC.h"


typedef struct _tobServ_SessionVariable
{
    char name[SESSION_NAME_SIZE];
    char value[SESSION_VALUE_SIZE];
    char module[MODULE_NAME_SIZE];
} tobServ_SessionVariable;

typedef struct _tobServ_Session
{
    char IP[20];
    int code;
    int num;
    int expire;
    tobServ_SessionVariable *variables;
} tobServ_Session;

typedef struct _tobServ_SessionList
{
    int num;
    tobServ_Session *sessions;
    pthread_mutex_t *mutex_session;
} tobServ_SessionList;

typedef struct _tobServ_PostData
{
    char *name;
    char *value;
} tobServ_PostData;

typedef struct _tobServ_HeaderInfo
{
    char name[64];
    char value[256];
} tobServ_HeaderInfo;

typedef struct _header
{
    int success;
    char method[32];
    char path[256];
    char version[128];
    unsigned int numinfos;
    tobServ_HeaderInfo *infos;
    unsigned int numpostdata;
    tobServ_PostData *postdata;
} header;

typedef struct _tobServ_Querry
{
    int time;
    char module[128];
    char IP[20];
    int type;
    tobServ_SessionList *sessionlist;
    int code;
} tobServ_Querry;

typedef struct _tobServ_response
{
    char *response;
    char *type;
    int usecache;
    int length;
} tobServ_response;

typedef struct _tobServ_file
{
    char *content;
    char *type;
    int size;
    tobServ_parsedFile *parsedFile;
} tobServ_file;

#define PARSEDFILE_ROOT 0
#define PARSEDFILE_TEXT 1
#define PARSEDFILE_SWITCH 2
#define PARSEDFILE_SECTION 3
#define PARSEDFILE_VARIABLE 4
typedef struct _tobServ_parsedFile
{
    int type;
    int numparts;
    struct _tobServ_parsedFile *parts;
    tobString name;
} tobServ_parsedFile;

typedef struct _tobServ_TemplateVariable
{
    tobString name;
    tobString replace;
} tobServ_TemplateVariable;

typedef struct _tobServ_TemplateSwitch
{
    tobString name;
}

typedef struct _tobServ_TemplateSection
{
    tobString name;
    int numrows;
    tobServ_Template **rows;
} tobServ_TemplateSection;

typedef struct _tobServ_Template
{
    int numvariables;
    tobServ_TemplateVariable *variables;
    int numsections;
    tobServ_TemplateSection *sections;
    int numswitches;
    tobServ_TemplateSwitch *switches;
} tobServ_template;

typedef tobServ_response (*module_QUERRY_function)(header, tobServ_Querry, char*);
tobServ_file get_file(char *);
int free_file(tobServ_file);
int get_file_type(char *type, int size, char *path);
int SetSessionVariable(tobServ_Querry *querry, char *name, char *value);
char *GetSessionVariable(tobServ_Querry *querry, char *name);

tobServ_parsedFile ParseFile(tobServ_file *file);
tobServ_parsedFile ParseFileSubString(char *string);

int InitializeTemplate(tobServ_template *template);
int FreeTemplate(tobServ_template *template);
int AddTemplateVariable(tobServ_template *template, char *name, char *replace);
int SetTemplateSwitch(tobServ_template *template, char *name);
int AddTemplateSection(tobServ_template*, char *name); //returns SectionID
int AddTemplateSectionRow(tobServ_template *template, tobServ_template *rowtemplate);

//adds the rows to result
int TemplateReplaceSectionRows(tobServ_template templatehandle, int sectionID, tobString *result, tobString sectioncontent);

tobString TemplateReplace(tobServ_template *templatehandle, tobServ_parsedFile *parsed);
char *GetPostVariable(header *, char *);
int GetPostVariableInt(header*, char*);
int IsPostVariableSet(header*, char*);

#endif
