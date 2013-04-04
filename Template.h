#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <tobFUNC.h>

//max number of chars a section name, switch name or replace name can have
#define MAX_VARIABLE_LENGTH 100

#define MAX_FILETYPE_SIZE 100

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

typedef struct _tobServ_file
{
    char *content;
    char *type;
    int size;
    
    struct _tobServ_parsedFile parsedFile; //parsedFile.type -1 if not parsed

    int cacheID; //-1 if not cached
} tobServ_file;

typedef struct _tobServ_TemplateVariable
{
    tobString name;
    tobString replace;
} tobServ_TemplateVariable;

typedef struct _tobServ_TemplateSwitch
{
    tobString name;
} tobServ_TemplateSwitch;

struct _tobServ_template;

typedef struct _tobServ_TemplateSection
{
    tobString name;
    int numrows;
    struct _tobServ_template **rows;
} tobServ_TemplateSection;

typedef struct _tobServ_template
{
    int numvariables;
    tobServ_TemplateVariable *variables;
    int numsections;
    tobServ_TemplateSection *sections;
    int numswitches;
    tobServ_TemplateSwitch *switches;
} tobServ_template;

#include "FileCache.h" //for tobServ_file

tobServ_parsedFile ParseFile(tobServ_file *file);
tobServ_parsedFile ParseFileSubString(char *string, int size);
int FreeParsed(tobServ_parsedFile *parsed);

int InitializeTemplate(tobServ_template *template);
int FreeTemplate(tobServ_template *template);
int AddTemplateVariable(tobServ_template *template, char *name, char *replace);
int SetTemplateSwitch(tobServ_template *template, char *name);
int AddTemplateSection(tobServ_template*, char *name); //returns SectionID
tobServ_template *AddTemplateSectionRow(tobServ_template *templatehandle, int sectionID);

tobString TemplateReplace(tobServ_template *templatehandle, tobServ_parsedFile *parsed);

#endif
