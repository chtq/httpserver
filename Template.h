#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <tobFUNC.h>
#include <stdint.h>

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
    int32_t type; //-1 means not parsed
    uint32_t numparts;
    struct _tobServ_parsedFile *parts;
    tobString name;
} tobServ_parsedFile;

typedef struct _tobServ_file
{
    char *content; //NULL if not existant
    char *type;
    uint32_t size;
    
    struct _tobServ_parsedFile parsedFile; //parsedFile.type -1 if not parsed

    int32_t cacheID; //-1 if not cached
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
    uint32_t numrows;
    struct _tobServ_template **rows;
} tobServ_TemplateSection;

typedef struct _tobServ_template
{
    uint32_t numvariables;
    tobServ_TemplateVariable *variables;
    uint32_t numsections;
    tobServ_TemplateSection *sections;
    uint32_t numswitches;
    tobServ_TemplateSwitch *switches;
} tobServ_template;

tobServ_parsedFile ParseFile(tobServ_file *file);
tobServ_parsedFile ParseFileSubString(char *string, int size);
int FreeParsed(tobServ_parsedFile *parsed);

int32_t InitializeTemplate(tobServ_template *template);
int32_t FreeTemplate(tobServ_template *template);
int32_t AddTemplateVariable(tobServ_template *template, char *name, char *replace);
int32_t SetTemplateSwitch(tobServ_template *template, char *name);
int32_t AddTemplateSection(tobServ_template*, char *name); //returns SectionID
tobServ_template *AddTemplateSectionRow(tobServ_template *templatehandle, int sectionID);

tobString TemplateReplace(tobServ_template *templatehandle, tobServ_parsedFile *parsed);

#endif
