#ifndef TOBSERVMODULE_H
#define TOBSERVMODULE_H

#include <stdio.h>
#include <malloc.h>
#include <pthread.h>
#include <string.h>
#include "functions.h"


typedef struct _tobServ_SessionVariable
{
    char name[128];
    char value[1024];
    char module[128];
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
} tobServ_file;

typedef struct _tobServ_TemplateItem
{
    char *name;
    char *replace;
} tobServ_TemplateItem;

typedef struct _tobServ_TemplateRow
{
    int num;
    tobServ_TemplateItem *items;
} tobServ_TemplateRow;

typedef struct _tobServ_TemplateSection
{
    char *name;
    int num;
    tobServ_TemplateRow *rows;
} tobServ_TemplateSection;

typedef struct _tobServ_Template
{
    int num;
    tobServ_TemplateItem *items;
    int numsections;
    tobServ_TemplateSection *sections;
} tobServ_template;

typedef tobServ_response (*module_QUERRY_function)(header, tobServ_Querry, char*);
tobServ_file get_file(char *);
int free_file(tobServ_file);
int get_file_type(char *type, int size, char *path);
int SetSessionVariable(tobServ_Querry *querry, char *name, char *value);
char *GetSessionVariable(tobServ_Querry *querry, char *name);

int InitializeTemplate(tobServ_template*);
int FreeTemplate(tobServ_template*);
int AddTemplateItem(tobServ_template*, char *, char *);
int AddTemplateSection(tobServ_template*, char *name); //returns SectionID
int AddTemplateSectionRow(tobServ_template*, int); //returns RowID
int AddTemplateRowItem(tobServ_template*, int sectionID, int rowID, char *name, char *replace);

tobServ_file *TemplateReplace(tobServ_template*, tobServ_file*);
char *GetPostVariable(header*, char*);
int GetPostVariableInt(header*, char*);
int IsPostVariableSet(header*, char*);

#endif
