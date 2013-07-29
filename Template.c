#include "Template.h"
#include "dbg.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int32_t InitializeTemplate(tobServ_template *templatehandle)
{
    templatehandle->numvariables = 0;
    templatehandle->variables = NULL;
    templatehandle->numsections = 0;
    templatehandle->sections = NULL;
    templatehandle->numswitches = 0;
    templatehandle->switches = NULL;

    return 0;
}

int32_t FreeTemplate(tobServ_template *templatehandle)
{
    uint32_t i, a;
    
    for(i=0;i<templatehandle->numvariables;i++)
    {
        tobString_Free(&templatehandle->variables[i].name);
        tobString_Free(&templatehandle->variables[i].replace);
    }
    if(templatehandle->variables)
        free(templatehandle->variables);

    for(i=0;i<templatehandle->numswitches;i++)
    {
        tobString_Free(&templatehandle->switches[i].name);
    }
    if(templatehandle->switches)
        free(templatehandle->switches);

    for(i=0;i<templatehandle->numsections;i++)
    {
        tobString_Free(&templatehandle->sections[i].name);

        for(a=0;a<templatehandle->sections[i].numrows;a++)
            FreeTemplate(templatehandle->sections[i].rows[a]);
    }
    if(templatehandle->sections)
        free(templatehandle->sections);

    templatehandle->variables=NULL;
    templatehandle->numvariables=0;
    templatehandle->switches=NULL;
    templatehandle->numswitches=0;
    templatehandle->sections=NULL;
    templatehandle->numsections=0;

    return 0;
}

int32_t AddTemplateVariable(tobServ_template *templatehandle, char *name, char *replace)
{
    uint32_t newnum;

    newnum = templatehandle->numvariables+1;
    templatehandle->variables = realloc(templatehandle->variables, sizeof(tobServ_TemplateVariable)*newnum);
    check_mem(templatehandle->variables);

    check(tobString_Init(&templatehandle->variables[templatehandle->numvariables].name, MAX_VARIABLE_LENGTH)==0, "tobString_Init failed");
    check(tobString_Add(&templatehandle->variables[templatehandle->numvariables].name, name, strlen(name))==0, "tobString_Add failed");

    check(tobString_Init(&templatehandle->variables[templatehandle->numvariables].replace, MAX_VARIABLE_LENGTH)==0, "tobString_Init failed");
    check(tobString_Add(&templatehandle->variables[templatehandle->numvariables].replace, replace, strlen(replace))==0, "tobString_Add failed");

    templatehandle->numvariables = newnum;

    return 0;

error:
   
    tobString_Free(&templatehandle->variables[templatehandle->numvariables].name);
    tobString_Free(&templatehandle->variables[templatehandle->numvariables].replace);

    return -1;
}

int32_t AddTemplateSection(tobServ_template *templatehandle, char *name)
{
    uint32_t newnum;

    newnum = templatehandle->numsections+1;
    templatehandle->sections = realloc(templatehandle->sections, sizeof(tobServ_TemplateSection)*newnum);

    check(tobString_Init(&templatehandle->sections[templatehandle->numsections].name, MAX_VARIABLE_LENGTH)==0, "tobString_Init failed");
    check(tobString_Add(&templatehandle->sections[templatehandle->numsections].name, name, strlen(name))==0, "tobString_Add failed");

    templatehandle->sections[templatehandle->numsections].numrows = 0;
    templatehandle->sections[templatehandle->numsections].rows = NULL;

    templatehandle->numsections = newnum;

    return (newnum-1);

error:
    tobString_Free(&templatehandle->sections[templatehandle->numsections].name);
    return -1;
}

tobServ_template *AddTemplateSectionRow(tobServ_template *templatehandle, int sectionID)
{
    uint32_t newnum;

    newnum = templatehandle->sections[sectionID].numrows+1;
    templatehandle->sections[sectionID].rows = realloc(templatehandle->sections[sectionID].rows, sizeof(tobServ_template*) * newnum);
    check_mem(templatehandle->sections[sectionID].rows);

    //get space for the new row template and initialize it
    templatehandle->sections[sectionID].rows[templatehandle->sections[sectionID].numrows] = malloc(sizeof(tobServ_template));
    check_mem(templatehandle->sections[sectionID].rows[templatehandle->sections[sectionID].numrows]);
    
    InitializeTemplate(templatehandle->sections[sectionID].rows[templatehandle->sections[sectionID].numrows]);

    templatehandle->sections[sectionID].numrows = newnum;

    return templatehandle->sections[sectionID].rows[templatehandle->sections[sectionID].numrows-1];

error:
    return NULL;
}

tobServ_parsedFile ParseFile(tobServ_file *file)
{
    tobServ_parsedFile result;

    result = ParseFileSubString(file->content, file->size);
    check(result.type>=0, "ParseFileSubString failed");

    result.type = 0;

    return result;

error:
    result.type = -1;
    return result;
}

tobServ_parsedFile ParseFileSubString(char *string, int size)
{
    uint32_t i, a, fallback;
    tobServ_parsedFile result;
    char name[MAX_VARIABLE_LENGTH];
    char buffer[MAX_VARIABLE_LENGTH+10];
    tobString text;
    tobString switchSET;
    tobString switchUNSET;
    tobString sectioncontent;

    result.numparts = 0;
    result.parts = NULL;
    result.type = 0; 


    tobString_Init(&switchSET, 512);
    tobString_Init(&switchUNSET, 512);
    tobString_Init(&sectioncontent, 512);

    tobString_Init(&text, 512);
    
    for(i=0;i<size;i++)
    {
        fallback = 0;
        if(string[i] == '%') //VARIABLE POSSIBLY FOUND
        {
            i++; //go to the next char
            a=0; //initialize variablename character counter
                
            for(;i<size && a<MAX_VARIABLE_LENGTH;i++)
            {
                if(string[i] == '%') //end found
                {                   
                    name[a] = '\0';
                            
                    if(a>0)//it's only a variable if the variablename length is greater than zero
                    {
                        //check if text must be added
                        if(text.len>0)
                        {
                            result.numparts++;
                            result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);
                            check_mem(result.parts);

                            result.parts[result.numparts-1].type = PARSEDFILE_TEXT;
                            result.parts[result.numparts-1].numparts = 0;
                            result.parts[result.numparts-1].parts = NULL;

                            result.parts[result.numparts-1].name = text;

                            tobString_Init(&text, 512);//create a new text buffer
                        }
                        
                        result.numparts++;
                        result.parts = realloc(result.parts, result.numparts*sizeof(tobServ_parsedFile));
                        check_mem(result.parts);

                        result.parts[result.numparts-1].type = PARSEDFILE_VARIABLE;
                        result.parts[result.numparts-1].numparts = 0;
                        result.parts[result.numparts-1].parts = NULL;
                        
                        tobString_Init(&result.parts[result.numparts-1].name, MAX_VARIABLE_LENGTH);
                        check(tobString_Copy(&result.parts[result.numparts-1].name, name, a)==0, "tobString_Copy failed");
                    }
                    else
                        check(tobString_AddChar(&text, '%')==0, "tobString_AddChar failed"); //%% means escaped %

                    break; //we are done
                }
                else
                {
                    //save char and go to the next one
                    name[a] = string[i];
                    if (string[i] == ' ' || string[i] == '\n') // variable names have to be a single word in a single line
                    {
                        check(tobString_AddChar(&text, '%')==0, "tobString_AddChar failed");
                        fallback = 1;
                        break;
                    }
                    a++;
                }
            }

            if(a==MAX_VARIABLE_LENGTH || i==size) //the variablename is too long or we have reached the end without finding the second %
            {
                check(tobString_AddChar(&text, '%')==0, "tobString_AddChar failed");
                fallback = 1;
            }
        }
        else if(string[i] == '[' && string[i+1] != '/') //SECTION POSSIBLY FOUND [/ is reserved for section endings
        {
            i++; //go to the next char
            a=0; //reset name index
                
            for(;i<size && a<MAX_VARIABLE_LENGTH;i++)
            {
                if(string[i] == ']') //end found
                {
                    name[a] = '\0';

                    i++;
                            
                    if(a>0) //it's only a section if the sectionname length is greater than zero
                    {
                        snprintf(buffer, sizeof(buffer), "[/%s]", name);
                                
                        //search for the ending
                        for(;i< ( size - a - 3) ;i++) // there still has to be enough space for the test
                        {                                   
                            if(!strncmp(string+i, buffer, a+3)) //a+3 is the length of [/SECTIONNAME]
                            {
                                //check if text must be added
                                if(text.len>0)
                                {
                                    result.numparts++;
                                    result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);
                                    check_mem(result.parts);

                                    result.parts[result.numparts-1].type = PARSEDFILE_TEXT;
                                    result.parts[result.numparts-1].numparts = 0;
                                    result.parts[result.numparts-1].parts = NULL;

                                    result.parts[result.numparts-1].name = text;

                                    tobString_Init(&text, 512);
                                }
                                
                                result.numparts++;
                                result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);
                                check_mem(result.parts);

                                result.parts[result.numparts-1] = ParseFileSubString(sectioncontent.str, sectioncontent.len);
                                check(result.parts[result.numparts-1].type >= 0, "ParseFileSubString failed");

                                tobString_Init(&result.parts[result.numparts-1].name, MAX_VARIABLE_LENGTH);
                                result.parts[result.numparts-1].type = PARSEDFILE_SECTION;

                                check(tobString_Copy(&result.parts[result.numparts-1].name, name, a)==0, "tobString_Copy failed");                              

                                i += a + 2; //put the index add the end of the section
                                break;
                            }
                            else
                                check(tobString_AddChar(&sectioncontent, string[i])==0, "tobString_AddChar failed");
                        }

                        if(i == size - a - 3) //ending wasn't found till the very end
                            check(tobString_sprintf(&text, "[%s]%s", name, sectioncontent.str)==0, "tobString_sprintf failed");

                        tobString_Free(&sectioncontent);
                    }
                    else
                        check(tobString_Add(&text, "[]", strlen("[]"))==0, "tobString_Add failed"); //no replacement needed because it isn't a valid sectionname

                    break; //we are done
                }

                name[a] = string[i];
                a++;
            }

            if(a==MAX_VARIABLE_LENGTH || i==size) //the variablename is too long or we have reached the end without finding ]
            {
                check(tobString_AddChar(&text, '[')==0, "tobString_AddChar failed");
                fallback = 1;
            }
        }
        else if(string[i] == '{') //SWITCH possibly found
        {
            i++; //go to the next char
            a=0; //reset name index
                
            for(;i<size && a<MAX_VARIABLE_LENGTH;i++)
            {
                if(string[i] == '{') //end found
                {
                    i++;
                    if(string[i] != '{') // { must follow right after
                    {
                        check(tobString_sprintf(&text, "{%s}", name)==0, "tobString_sprintf failed"); //no switch found add everything
                    }
                    else
                    {
                        i++; //next char

                        //get the onset part of the switch
                        for(;i<size;i++)
                        {
                            if(string[i] == '}')
                                break;

                            check(tobString_AddChar(&switchSET, string[i])==0, "tobString_AddChar failed");
                        }
                        if(i==size) //couldn't find '}'
                        {
                            check(tobString_sprintf(&text, "{%s}{%s", name, switchSET.str)==0, "tobString_sprintf failed");
                        }
                        else
                        {
                            //it was found now search for the unset part of the switch
                            i++; //advance
                            if(string[i] != '{') //opening { must follow right after
                            {
                                check(tobString_sprintf(&text, "{%s}{%s}", name, switchSET.str)==0, "tobString_sprintf failed");
                            }
                            else
                            {
                                i++; //next char
                                for(;i<size;i++)
                                {
                                    if(string[i] == '}')
                                        break;

                                    check(tobString_AddChar(&switchUNSET, string[i])==0, "tobString_AddChar failed");
                                }
                                
                                if(i==size) //couldn't find '}'
                                {
                                    check(tobString_sprintf(&text, "{%s}{%s}{", name, switchSET.str, switchUNSET.str)==0, "tobString_sprintf failed");
                                }
                                else
                                {
                                    //everything was found seems all valid

                                    //check if text must be added
                                    if(text.len>0)
                                    {
                                        result.numparts++;
                                        result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);
                                        check_mem(result.parts);

                                        result.parts[result.numparts-1].type = PARSEDFILE_TEXT;
                                        result.parts[result.numparts-1].numparts = 0;
                                        result.parts[result.numparts-1].parts = NULL;

                                        result.parts[result.numparts-1].name = text;

                                        tobString_Init(&text, 512);
                                    }

                                    result.numparts++;
                                    result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);
                                    check_mem(result.parts);

                                    //add switchSET and switchUNSET parsed
                                    result.parts[result.numparts-1].type = PARSEDFILE_SWITCH;
                                    result.parts[result.numparts-1].numparts = 2;
                                    result.parts[result.numparts-1].parts = malloc(sizeof(tobServ_parsedFile)*2);
                                    check_mem(result.parts[result.numparts-1].parts);

                                    result.parts[result.numparts-1].parts[0] = ParseFileSubString(switchSET.str, switchSET.len);
                                    check(result.parts[result.numparts-1].parts[0].type>=0, "ParseFileSubString failed");
                                                    
                                    result.parts[result.numparts-1].parts[1] = ParseFileSubString(switchUNSET.str, switchUNSET.len);
                                    check(result.parts[result.numparts-1].parts[0].type>=0, "ParseFileSubString failed");
                                }
                            }
                        }                       

                        tobString_Free(&switchSET);
                        tobString_Free(&switchUNSET);
                    }
                            
                    break;//done searching for the var name
                }

                //part of the name
                name[a] = string[i];
                a++;
            }

            if(a==MAX_VARIABLE_LENGTH || i==size) //the variablename is too long or we have reached the end without finding the second %
            {
                check(tobString_AddChar(&text, '{')==0, "tobString_AddChar failed");
                fallback = 1;
            } //the variablename is too long or we have reached the end without finding the closing bracket
            //   check(tobString_sprintf(&text, "{%s", name)==0, "tobString_sprintf failed"); //add everything but don't replace anything
        }
        else
            tobString_AddChar(&text, string[i]);


        if (fallback == 1)
        {
            check(tobString_Add(&text, name, a)==0, "tobString_Add failed");
            if (a == MAX_VARIABLE_LENGTH) i--; //reparse the current character, otherwise it would get skipped
        }
    }

    //add last text part
    if(text.len>0)
    {
        result.numparts++;
        result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);
        check_mem(result.parts);

        result.parts[result.numparts-1].type = PARSEDFILE_TEXT;
        result.parts[result.numparts-1].numparts = 0;
        result.parts[result.numparts-1].parts = NULL;

        result.parts[result.numparts-1].name = text;
    }
    else
        tobString_Free(&text);
    

    return result;

error:
    tobString_Free(&text);
    tobString_Free(&switchSET);
    tobString_Free(&switchUNSET);
    tobString_Free(&sectioncontent);

    FreeParsed(&result);

    result.type = -1;
    return result;
}

tobString TemplateReplace(tobServ_template *templatehandle, tobServ_parsedFile *parsed)
{
    uint32_t i, a, b;
    tobString result;
    tobString subresult;

    tobString_Init(&result, 1024);
    tobString_Init(&subresult, 1024);
    
    for(i=0;i<parsed->numparts;i++)
    {
        switch(parsed->parts[i].type)
        {
        case PARSEDFILE_TEXT:
            check(tobString_Add(&result, parsed->parts[i].name.str, parsed->parts[i].name.len)==0, "tobString_Add failed");
            break;
            
        case PARSEDFILE_VARIABLE:
            for(a=0;a<templatehandle->numvariables;a++)
            {
                if(!strcmp(templatehandle->variables[a].name.str, parsed->parts[i].name.str))
                {
                    check(tobString_Add(&result, templatehandle->variables[a].replace.str, templatehandle->variables[a].replace.len)==0, "tobString_Add failed");
                    break;
                }
            }
            if(a==templatehandle->numvariables) //variable not found
                check(tobString_sprintf(&result, "%%%s%%", parsed->parts[i].name.str)==0, "tobString_sprintf failed"); //add the var unreplaced
            break;
            
        case PARSEDFILE_SECTION:
            for(a=0;a<templatehandle->numsections;a++)
            {
                if(!strcmp(templatehandle->sections[a].name.str, parsed->parts[i].name.str))
                {
                    for(b=0;b<templatehandle->sections[a].numrows;b++)
                    {
                        subresult = TemplateReplace(templatehandle->sections[a].rows[b], &parsed->parts[i]); //recursively fill the sectioncontent
                        check(tobString_Add(&result, subresult.str, subresult.len)==0, "tobString_Add failed");

                        tobString_Free(&subresult);
                    }
                    break;
                }
            }
            if(a==templatehandle->numsections)//section not found
            {
                //add some error
                check(tobString_sprintf(&result, "[%s][/%s]", parsed->parts[i].name.str, parsed->parts[i].name.str)==0, "tobString_sprintf failed");
                log_warn("Section %s was not found in the template", parsed->parts[i].name.str);
            }
            break;
            
        case PARSEDFILE_SWITCH:
            for(a=0;a<templatehandle->numswitches;a++)
            {
                if(!strcmp(templatehandle->switches[a].name.str, parsed->parts[i].name.str))
                {
                    subresult = TemplateReplace(templatehandle, &parsed->parts[i].parts[0]); //recursively fill the switchSET
                    check(tobString_Add(&result, subresult.str, subresult.len)==0, "tobString_Add failed");
                    tobString_Free(&subresult);
                    break;
                }
            }
            if(a==templatehandle->numswitches)
            {
                subresult = TemplateReplace(templatehandle, &parsed->parts[i].parts[1]); //recursively fill the switchUNSET
                check(tobString_Add(&result, subresult.str, subresult.len)==0, "tobString_Add failed");
                tobString_Free(&subresult);
            }

            break;
        }
    }

    return result;

error:
    tobString_Free(&subresult);
    tobString_Free(&result);

    return result;
}

int32_t FreeParsed(tobServ_parsedFile *parsed)
{
    uint32_t i;
    
    switch(parsed->type)
    {
    case PARSEDFILE_ROOT:
        for(i=0;i<parsed->numparts;i++)
            FreeParsed(&parsed->parts[i]);
        if(parsed->parts)
            free(parsed->parts);

        break;

    case PARSEDFILE_TEXT:
        tobString_Free(&parsed->name);
        break;

    case PARSEDFILE_SWITCH:
        tobString_Free(&parsed->name);
        FreeParsed(&parsed->parts[0]);
        FreeParsed(&parsed->parts[1]);
        if(parsed->parts)
            free(parsed->parts);
        break;

    case PARSEDFILE_SECTION:
        tobString_Free(&parsed->name);
        for(i=0;i<parsed->numparts;i++)
            FreeParsed(&parsed->parts[i]);
        if(parsed->parts)
            free(parsed->parts);

    case PARSEDFILE_VARIABLE:
        tobString_Free(&parsed->name);
        break;

    default:
        break;
    }

    return 0;
}

int32_t SetTemplateSwitch(tobServ_template *template, char *name)
{
    template->numswitches++;

    template->switches = realloc(template->switches, sizeof(tobServ_TemplateSwitch)*template->numswitches);
    check_mem(template->switches);

    tobString_Init(&template->switches[template->numswitches].name, MAX_VARIABLE_LENGTH);
    check(tobString_Add(&template->switches[template->numswitches].name, name, strlen(name))==0, "tobString_Add failed");

    return 0;

error:
    template->numswitches--;

    return -1;
}
