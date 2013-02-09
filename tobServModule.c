#include "tobServModule.h"
#include <stdlib.h>

char *GetPostVariable(header* headerstruct, char *name)
{
    int i;
    char *result;

    for(i=0;i<headerstruct->numpostdata;i++)
    {
        if(!strcmp(headerstruct->postdata[i].name, name))
        {
            result = malloc(strlen(headerstruct->postdata[i].value)+1);
            stringcpy(result, headerstruct->postdata[i].value, strlen(headerstruct->postdata[i].value)+1);
            return result;
        }
    }

    return NULL;
}

int IsPostVariableSet(header *headerstruct, char *name)
{
    int i;

    for(i=0;i<headerstruct->numpostdata;i++)
    {
        if(!strcmp(headerstruct->postdata[i].name, name))
        {
            return 1;
        }
    }

    return 0;
}

int InitializeTemplate(tobServ_template *templatehandle)
{
    templatehandle->num = 0;
    templatehandle->numsections = 0;
    templatehandle->sections = NULL;
    templatehandle->items = NULL;

    return 0;
}

int FreeTemplate(tobServ_template *templatehandle)
{
    int i,a,b;

    for(i=0;i<templatehandle->num;i++)
    {
        free(templatehandle->items[i].name);
        free(templatehandle->items[i].replace);
    }

    if(templatehandle->items)
        free(templatehandle->items);

    templatehandle->items = NULL;
    templatehandle->num = 0;

    for(i=0;i<templatehandle->numsections;i++)
    {
        for(a=0;a<templatehandle->sections[i].num;a++)
        {
            for(b=0;b<templatehandle->sections[i].rows[a].num;b++)
            {
                free(templatehandle->sections[i].rows[a].items[b].name);
                free(templatehandle->sections[i].rows[a].items[b].replace);
            }
            if(templatehandle->sections[i].rows[a].items)
                free(templatehandle->sections[i].rows[a].items);
        }
        if(templatehandle->sections[i].name)
            free(templatehandle->sections[i].name);
        if(templatehandle->sections[i].rows)
            free(templatehandle->sections[i].rows);
    }
    if(templatehandle->sections)
        free(templatehandle->sections);

    templatehandle->sections = NULL;
    templatehandle->numsections = 0;

    return 0;
}

int AddTemplateItem(tobServ_template *templatehandle, char *name, char *replace)
{
    int newnum;

    newnum = templatehandle->num+1;
    templatehandle->items = realloc(templatehandle->items, sizeof(tobServ_TemplateItem)*newnum);

    templatehandle->items[templatehandle->num].name = malloc(strlen(name)+1);
    strcpy(templatehandle->items[templatehandle->num].name, name);

    templatehandle->items[templatehandle->num].replace = malloc(strlen(replace)+1);
    strcpy(templatehandle->items[templatehandle->num].replace, replace);

    templatehandle->num = newnum;

    return 0;
}

int AddTemplateSection(tobServ_template *templatehandle, char *name)
{
    int newnum;

    newnum = templatehandle->numsections+1;
    templatehandle->sections = realloc(templatehandle->sections, sizeof(tobServ_TemplateSection)*newnum);

    templatehandle->sections[templatehandle->numsections].name = malloc(strlen(name)+1);
    strcpy(templatehandle->sections[templatehandle->numsections].name, name);

    templatehandle->sections[templatehandle->numsections].num = 0;
    templatehandle->sections[templatehandle->numsections].rows = NULL;

    templatehandle->numsections = newnum;

    return (newnum-1);
}

int AddTemplateSectionRow(tobServ_template *templatehandle, int sectionID)
{
    int newnum;

    newnum = templatehandle->sections[sectionID].num+1;
    templatehandle->sections[sectionID].rows = realloc(templatehandle->sections[sectionID].rows, sizeof(tobServ_TemplateRow)*newnum);

    templatehandle->sections[sectionID].rows[templatehandle->sections[sectionID].num].num = 0;
    templatehandle->sections[sectionID].rows[templatehandle->sections[sectionID].num].items = NULL;

    templatehandle->sections[sectionID].num = newnum;

    return (templatehandle->sections[sectionID].num-1);
}

int AddTemplateRowItem(tobServ_template *templatehandle, int sectionID, int rowID, char *name, char *replace)
{
    int newnum;

    newnum = templatehandle->sections[sectionID].rows[rowID].num+1;
    templatehandle->sections[sectionID].rows[rowID].items = realloc(templatehandle->sections[sectionID].rows[rowID].items, sizeof(tobServ_TemplateItem)*newnum);

    templatehandle->sections[sectionID].rows[rowID].items[templatehandle->sections[sectionID].rows[rowID].num].name = malloc(strlen(name)+1);
    strcpy(templatehandle->sections[sectionID].rows[rowID].items[templatehandle->sections[sectionID].rows[rowID].num].name, name);

    templatehandle->sections[sectionID].rows[rowID].items[templatehandle->sections[sectionID].rows[rowID].num].replace = malloc(strlen(replace)+1);
    strcpy(templatehandle->sections[sectionID].rows[rowID].items[templatehandle->sections[sectionID].rows[rowID].num].replace, replace);

    templatehandle->sections[sectionID].rows[rowID].num = newnum;

    return 0;
}

tobServ_file *TemplateReplace(tobServ_template *templatehandle, tobServ_file *original)
{
    int i, a, b;
    char *sectionbeginning, *sectionending, *currentposition;
    char *sectioncontent;
    int endingindicatorlength;
    int sectioncontentsize;
    char *temp;
    char buffer[512];
    tobString result;

    //add null termination but don't increase the size. The null character isn't supposed to be sent to the client
    original->content = realloc(original->content, original->size+1);
    original->content[original->size] = '\0';

    //parse sections
    for(i=0;i<templatehandle->numsections;i++)
    {
        snprintf(buffer, 512, "[%s]", templatehandle->sections[i].name);
        while( (sectionbeginning = strstr(original->content, buffer)) )
        {
            tobString_Init(&result);

            //Add everything to the result until the section begins
            tobString_Add(&result, original->content, (int)(sectionbeginning-original->content));

            //current position is right after the [sectionname]
            currentposition = sectionbeginning+strlen(buffer);

            //get the ending
            snprintf(buffer, 512, "[/%s]", templatehandle->sections[i].name);
            sectionending = strstr(original->content, buffer);
            if(!sectionending || sectionending<currentposition) //if the sectionending wasn't found or is before the start then quit
            {
                tobString_Free(&result);
                continue;
            }
            endingindicatorlength = strlen(buffer);

            //get the content of the section
            sectioncontentsize = (int)(sectionending-currentposition)+1;
            sectioncontent = malloc(sectioncontentsize);
            stringcpy(sectioncontent, currentposition, sectioncontentsize);

            //add the rows to the tobString object
            for(a=0;a<templatehandle->sections[i].num;a++)
            {
                temp = malloc(sectioncontentsize);
                strcpy(temp, sectioncontent);

                for(b=0;b<templatehandle->sections[i].rows[a].num;b++)
                    temp = stringreplace(temp, templatehandle->sections[i].rows[a].items[b].name, templatehandle->sections[i].rows[a].items[b].replace);

                tobString_Add(&result, temp, strlen(temp));
                free(temp);
            }

            //add the ending (part after the parsed section)
            sectionending += endingindicatorlength;
            tobString_Add(&result, sectionending, strlen(sectionending));

            //copy it over from the tobString container to the content
            free(original->content);
            original->content = malloc(result.len + 1);
            strcpy(original->content, result.str);

            //free the tobString object
            tobString_Free(&result);
        }
    }

    for(i=0;i<templatehandle->num;i++)
        original->content = stringreplace(original->content, templatehandle->items[i].name, templatehandle->items[i].replace);

    original->size = strlen(original->content); //size is without the null terminator

    return original;
}

int SetSessionVariable(tobServ_Querry *querry, char *name, char *value)
{
    int i, a;

    pthread_mutex_lock(querry->sessionlist->mutex_session);

    for(i=0;i<querry->sessionlist->num;i++)
        if((querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;

    if(querry->sessionlist->num == i)
    {
        pthread_mutex_unlock(querry->sessionlist->mutex_session);
        return -1;
    }

    for(a=0;a<querry->sessionlist->sessions[i].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    if(a==querry->sessionlist->sessions[i].num)
    {
        querry->sessionlist->sessions[i].num++;

        querry->sessionlist->sessions[i].variables = realloc(querry->sessionlist->sessions[i].variables, sizeof(tobServ_SessionVariable)*querry->sessionlist->sessions[i].num);
    }

    stringcpy(querry->sessionlist->sessions[i].variables[a].name, name, 128);
    stringcpy(querry->sessionlist->sessions[i].variables[a].value, value, 1024);
    stringcpy(querry->sessionlist->sessions[i].variables[a].module, querry->module, 128);

    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return 0;
}

char *GetSessionVariable(tobServ_Querry *querry, char *name)
{
    int i,a, length;
    char *result;

    pthread_mutex_lock(querry->sessionlist->mutex_session);

    for(i=0;i<querry->sessionlist->num;i++)
        if( (querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;

    if(querry->sessionlist->num == i)
    {
        pthread_mutex_unlock(querry->sessionlist->mutex_session);
        return NULL;
    }

    for(a=0;a<querry->sessionlist->sessions[i].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    if(a==querry->sessionlist->sessions[i].num)
    {
        pthread_mutex_unlock(querry->sessionlist->mutex_session);
        return NULL;
    }

    length = strlen(querry->sessionlist->sessions[i].variables[a].value) + 1;

    result = malloc(length);
    stringcpy(result, querry->sessionlist->sessions[i].variables[a].value, length);

    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return result;
}

tobServ_file get_file(char *path)
{
    FILE *handle;
    char *buffer;
    char *type;
    size_t result;
    tobServ_file file;
    int size;

    type = malloc(100);

    handle = fopen(path, "rb");
    if (handle==NULL)
    {
        file.content = NULL;
        file.size = 0;
        file.type = NULL;

        free(type);

        return file;
    }

    fseek(handle, 0, SEEK_END);
    size = ftell(handle);
    rewind (handle);


    buffer = malloc(size);

    result = fread(buffer,1,size,handle);
    if(result != size)
    {
        file.content = NULL;
        file.size = 0;
        file.type = NULL;

        free(type);

        free(buffer);

        return file;
    }

    get_file_type(type, 100, path);

    fclose(handle);

    file.size = size;
    file.content = buffer;
    file.type = type;

    return file;
}

int free_file(tobServ_file file)
{
    if(file.content)
        free(file.content);
    if(file.type)
        free(file.type);

    return 0;
}

int get_file_type(char *type, int size, char *path)
{
    char *ending=NULL;

    char *temptype;

    ending = strrchr(path,'.');

    if(ending==NULL)
        temptype = "application/octet-stream";
    else if(!strcmp((ending+1), "html") || !strcmp((ending+1), "htm"))
        temptype = "text/html";
    else if(!strcmp((ending+1), "jpeg") || !strcmp((ending+1), "jpg") || !strcmp((ending+1), "jpe"))
        temptype = "image/jpeg";
    else if(!strcmp((ending+1), "gif"))
        temptype = "image/gif";
    else if(!strcmp((ending+1), "png"))
        temptype = "image/png";
    else if(!strcmp((ending+1), "txt"))
        temptype = "text/plain";
    else if(!strcmp((ending+1), "ico"))
        temptype = "image/x-ico";
    else
        temptype = "application/octet-stream";

    stringcpy(type, temptype, size);

    return 0;
}

int GetPostVariableInt(header *requestheader, char *variable)
{
    char *temp;
    int result;

    temp = GetPostVariable(requestheader, variable);

    if(!temp)
        return 0;

    result = atoi(temp);
    free(temp);

    return result;
}
