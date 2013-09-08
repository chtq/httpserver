#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include <tobCONF.h>
#include <tobFUNC.h>

#include <tobServModule.h>
#include <FileCache.h>
#include <Template.h>
#include <Sessions.h>
#include <commandlineAPI.h>

#include "dbg.h"

#include "ModuleManager.h"
#include "commandline.h"
#include "globalconstants.h"

typedef struct _tobServ_thread
{
    pthread_t threadID;
    uint32_t created;
    uint32_t connection;
    uint32_t last_active;
    pthread_cond_t *finished;
    pthread_mutex_t *mutex;
    tobServ_modulelist *modulelist;
    tobServ_Querry querry;
    tobServ_commandline *commandline;
    tobServ_ServerStats *serverstats;
} tobServ_thread;

void *handle_request(void *arg);
void send_response(int32_t connection, char *type, char *content, uint32_t size, uint32_t sessioncode, uint32_t usecache, uint32_t code, uint32_t nocookies);
void send_redirect(int32_t connection, char *content, uint32_t code);
header get_header(int32_t connection, tobServ_thread*);
int32_t FreeResponse(tobServ_response);
int32_t FreeResult(header result);
void main_shutdown_handler(int32_t);
int32_t FreeHeader(header);
int32_t FreeSessions(tobServ_SessionList*);
int64_t StartSession(tobServ_SessionList*, char*, uint64_t);
int32_t GetSessionCodeFromCookie(char*);

int main(int argc, char *argv[])
{
    uint32_t portno, i, clilen;

    int32_t sockfd, newsockfd;

    char IP[20];

    tobServ_thread *threads;
    tobServ_modulelist modulelist;
    uint32_t thread_num;
    uint32_t done;

    struct sockaddr_in serv_addr, cli_addr;

    pthread_attr_t attr;

    pthread_cond_t thread_finished;
    pthread_mutex_t mutex_finished;

    tobServ_commandline commandline;

    tobServ_SessionList sessionlist;
    sessionlist.initialized = 0;

    pthread_mutex_t mutex_session;

    struct sigaction new_term_action;

    tobServ_FileCache filecache;
    filecache.initialized = 0;

    uint32_t maxfiles;
    uint32_t maxfilesize;

    tobCONF_File configfile;
    tobCONF_Section *configsection;

    tobServ_ServerStats serverstats;

    tobCONF_Initialize(&configfile);

    //init server stats
    serverstats.numthreads = 0;
    serverstats.maxthreads = 0;
    serverstats.numrequests = 0;
    serverstats.peakthreads = 0;

    srand(time(NULL));

    //parse cfg
    check(tobCONF_ReadFile(&configfile, "server.cfg")==0, "on reading ConfigFile: %s", tobCONF_GetLastError(&configfile));

    configsection = tobCONF_GetSection(&configfile, "server");
    check(configsection, "config section \"server\" doesn't exist");

    thread_num = atoi(tobCONF_GetElement(configsection, "maxthreads"));
    check(thread_num>1, "invalid value for maxthreads");

    portno = atoi(tobCONF_GetElement(configsection, "port"));
    check((portno>=1 && portno<=65536), "invalid value for port: %i", portno);

    configsection = tobCONF_GetSection(&configfile, "Cache");
    check(configsection, "config section \"Cache\" doesn't exist");

    maxfiles = atoi(tobCONF_GetElement(configsection, "maxfiles"));
    check(maxfiles>=1, "invalid value for maxfiles: %i", maxfiles);

    maxfilesize = atoi(tobCONF_GetElement(configsection, "maxfilesize"));
    check(maxfilesize, "invalid value for maxfilesize: %i", maxfilesize);

    tobCONF_Free(&configfile);
    //done parsing the config

    //set up own handler for SIGTERM
    //is sent by commandline
    new_term_action.sa_handler = main_shutdown_handler;
    sigemptyset (&new_term_action.sa_mask);
    new_term_action.sa_flags = 0;
    sigaction (SIGTERM, &new_term_action, NULL);

    sessionlist.num = 0;
    sessionlist.sessions = NULL;

    pthread_mutex_init(&mutex_session, NULL);

    sessionlist.mutex_session = &mutex_session;
    sessionlist.initialized = 1;

    pthread_cond_init(&thread_finished, NULL);
    pthread_mutex_init(&mutex_finished, NULL);

    threads = malloc(sizeof(tobServ_thread)*thread_num);
    check_mem(threads);
    bzero((char *)threads, sizeof(tobServ_thread)*thread_num);

    serverstats.maxthreads = thread_num;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    check(sockfd>=0, "Opening socket failed!");

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    check(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0, "Binding socket on port %i failed! Check if it isn't occupied already", portno);

    check(listen(sockfd,5)==0, "listen on port %i failed", portno);
    clilen = sizeof(cli_addr);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    commandline.mainthreadID = pthread_self();
    commandline.doshutdown = 0;
    commandline.numCommands = 0;
    commandline.commands = NULL;

    pthread_mutex_init(&commandline.commandlist_mutex, NULL);

    //LOADING MODULES
    ModuleManager_Initialize(&modulelist, &commandline);
    
    //don't shutdown on failure
    if(ModuleManager_LoadModules(&modulelist, MODULEFILE)<0)
        log_info("Couldn't load modules, check your module file and try reload!");

    //FileCache
    check(InitializeFileCache(&filecache, maxfiles, maxfilesize)==0, "InitializeFileCache failed");

    //create command handler
    check(pthread_create(&commandline.commandthreadID, &attr, handle_commandline, (void*)&commandline)==0, "pthread_create failed");

    //add cmds to the command handler
    //they are never unregistered because they are needed for the entire time the program runs
    pthread_mutex_init(&serverstats.stats_mutex, NULL);
    commandlineAPI_registerCMD(&commandline, "stats", "prints server stats", commandline_printServerStats, (void*)&serverstats);
    commandlineAPI_registerCMD(&commandline, "reload", "reloads modules", commandline_reloadModules, (void*)&modulelist);
    commandlineAPI_registerCMD(&commandline, "cache_stats", "prints cache stats", commandline_printCacheStats, (void*)&filecache);
    commandlineAPI_registerCMD(&commandline, "cache_list", "lists cache content", commandline_printCacheList,  (void*)&filecache);

    log_info("Server was successfully started");

    while (!commandline.doshutdown)
    {       
        errno = 0;
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
            check(newsockfd>=0, "accept failed");       

            inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, IP, 20);

            for(i=0; i<thread_num; i++)
            {
                if(threads[i].threadID==0)
                {
                    threads[i].connection = newsockfd;
                    threads[i].created = time(NULL);
                    threads[i].last_active = time(NULL);
                    threads[i].finished = &thread_finished;
                    threads[i].mutex = &mutex_finished;
                    threads[i].modulelist = &modulelist;
                    stringcpy(threads[i].querry.IP, IP, 20);
                    threads[i].querry.time = time(NULL);
                    threads[i].commandline = &commandline;
                    threads[i].serverstats = &serverstats;
                    threads[i].querry.sessionlist = &sessionlist;
                    threads[i].querry.filecache = &filecache;
                        
                    check(pthread_create(&threads[i].threadID, &attr, handle_request, (void*)&threads[i])==0, "pthread_create failed");

                    break;
                }
            }
            if(i==thread_num) //no open slots close the connection
            {
                log_warn("Request was dropped because maxthreads was reached");
                close(newsockfd);
            }  
    }

    done = 0;
    while(!done)
    {
        sleep(1);
        check(pthread_mutex_lock(&serverstats.stats_mutex)==0, "pthread_mutex_lock failed");
        if(serverstats.numthreads == 0)
            done = 1;
        pthread_mutex_unlock(&serverstats.stats_mutex);
    }

    pthread_mutex_destroy(&commandline.commandlist_mutex);
    pthread_mutex_destroy(&serverstats.stats_mutex);
    pthread_mutex_destroy(&mutex_finished);
    pthread_cond_destroy(&thread_finished);

    //free everything
    close(sockfd);
    ModuleManager_FreeModules(&modulelist);
    FreeSessions(&sessionlist);
    FreeFileCache(&filecache);

    free(threads);
    threads = NULL;

    printf("DONE\n");

    return 0;

error:
    // almost inevitable memleak at crash - should we care?
    tobCONF_Free(&configfile);
    FreeSessions(&sessionlist);
    FreeFileCache(&filecache);
    //ModuleManager_FreeModules(&modulelist);  
    //causes infinite loop

    if(threads)
        free(threads);

    return 1;
}

void main_shutdown_handler(int signum)
{
    return;
}

void *handle_request(void *arg)
{
    header request;
    char *module = NULL;
    char *action = NULL;
    int length;
    char logger[1024];
    char buffer[512] = {'\0'};
    char *pathclone = NULL;
    tobServ_response response;
    int connection;
    int i, state, code, newcode;
    tobServ_thread *thread_data;

    thread_data = arg; //cast

    //update num threads and peak threads
    check(pthread_mutex_lock(&thread_data->serverstats->stats_mutex)==0, "pthread_mutex_lock failed");

    thread_data->serverstats->numthreads++;
    thread_data->serverstats->numrequests++;

    if(thread_data->serverstats->peakthreads < thread_data->serverstats->numthreads)
        thread_data->serverstats->peakthreads = thread_data->serverstats->numthreads;

    pthread_mutex_unlock(&thread_data->serverstats->stats_mutex);

    connection = thread_data->connection;

    request = get_header(connection, arg);
    
    if(!request.success)
    {
        close(connection);

        //change commandline thread count
        check(pthread_mutex_lock(&thread_data->serverstats->stats_mutex)==0, "pthread_mutex_lock failed");
        thread_data->serverstats->numthreads--;
        pthread_mutex_unlock(&thread_data->serverstats->stats_mutex);


        //mark thread as finished
        check(pthread_mutex_lock(thread_data->mutex)==0, "pthread_mutex_lock failed");
        pthread_cond_signal(thread_data->finished);
        thread_data->threadID = 0;
        pthread_mutex_unlock(thread_data->mutex);

        return 0;
    }
    thread_data->querry.requestheader = &request;  

    pathclone = malloc(strlen(request.path)+1);
    check_mem(pathclone);

    stringcpy(pathclone, request.path, strlen(request.path)+1);

    //access logger currently deactivated for write_log beeing disabled
    /* access logger
    time_t rawtime;
    time(&rawtime);
    char log[512];
    snprintf(log, sizeof(log), "%.24s %s %s %s %s", ctime(&rawtime), ((tobServ_thread*)arg)->querry.IP, request.method, request.host, request.path);
    write_log("access.log", log);
    */


    //separate module and action
    module = pathclone+1;
    action = NULL;

    state = 0;
    length = strlen(module);
    for(i=0; i<length; i++)
    {

        if(!state)
        {
            if(module[i] == '/')
            {
                module[i] = '\0';
                state = 1;
            }
        }
        else
        {
            action = module + i;
            break;
        }

    }

    if(length==0)
        module = "default";

    code = -1;
    for(i=0; i<request.numinfos; i++)
        if(!strcmp(request.infos[i].name, "Cookie"))
            code = GetSessionCodeFromCookie(request.infos[i].value);

    newcode = StartSession(thread_data->querry.sessionlist, ((tobServ_thread*)arg)->querry.IP, code);

    if(newcode==-1)
        newcode = code; //if still in db use old code

    thread_data->querry.code = newcode;
    stringcpy(thread_data->querry.module, module, 128);

    pthread_rwlock_rdlock(&thread_data->modulelist->lock);

  
    if((!strcmp(request.method, "GET") || !strcmp(request.method, "POST")) && (pathclone[0] == '\0' || pathclone[0] == '/'))
    {
        // force www. redirect if no subdomain given
        if ((request.host[0] > '9' || request.host[0] < '0') && (strncmp(request.host, "static.", 7) && strncmp(request.host, "www.", 4) ) )
        {
            char *firstocc, *lastocc;
            firstocc = strchr(request.host, '.');
            lastocc = strrchr(request.host, '.');
            if (firstocc == lastocc)  // doesnt work with TLDs like .co.nz
            {  
                snprintf(buffer, sizeof(buffer), "http://www.%s%s", request.host, request.path);  
                send_redirect(connection, buffer, 301);
            }  
            else
            {
                snprintf(buffer, sizeof(buffer), "http://www.%s%s", firstocc+1, request.path); 
                send_redirect(connection, buffer, 302);
            }
        }
        else 
        {
            if (!strncmp(request.host, "static.", 7)) snprintf(buffer, 512, "%s", request.host + 7);  
            else if (!strncmp(request.host, "www.", 4)) snprintf(buffer, 512, "%s", request.host + 4); 

            for(i=0; i<thread_data->modulelist->count; i++)
            {
                if(!strcmp(thread_data->modulelist->modules[i].host, buffer) || ((request.host[0] <= '9' && request.host[0] >= '0') && !strcmp(((tobServ_thread*)arg)->modulelist->modules[i].name, module)))
                {
                    if (!strcmp(thread_data->modulelist->modules[i].host, buffer))
                    {
                        if (strlen(request.path) > 1) action = request.path + 1;
                        else action = NULL;     
                    }
                            //store module path         
                            strcpy(thread_data->querry.modulepath, thread_data->modulelist->modules[i].path);
                        
                    response = thread_data->modulelist->modules[i].querry_function(thread_data->querry, action, thread_data->modulelist->modules[i].data);

                            pthread_rwlock_unlock(&thread_data->modulelist->lock);


                    if(!response.response || !response.type)
                    {
                        snprintf(logger, 1024, "failed to get response of %s", thread_data->modulelist->modules[i].name);
                        send_response(connection, "text/html", "500 - Internal Server Error", strlen("500 - Internal Server Error"), thread_data->querry.code, 0, 500, response.nocookies);
                    }
                    else
                    {
                        send_response(connection, response.type, response.response, response.length, thread_data->querry.code, response.usecache, response.code, response.nocookies);
                        FreeResponse(response);
                    }
                    break;
                }
            }
            if(i==thread_data->modulelist->count)
                send_response(connection, "text/html", "tobServ 404", strlen("tobServ 404"), thread_data->querry.code, 0, 404, 1);
        }
    }
    else
        send_response(connection, "text/html", "400 - Bad Request", strlen("400 - Bad Request"), thread_data->querry.code, 0, 400, 1);

    pthread_rwlock_unlock(&thread_data->modulelist->lock);

    close(connection);

    FreeHeader(request);

    free(pathclone);
    pathclone = NULL;

    pthread_mutex_lock(&thread_data->serverstats->stats_mutex);

    thread_data->serverstats->numthreads--;

    pthread_mutex_unlock(&thread_data->serverstats->stats_mutex);


    pthread_mutex_lock(thread_data->mutex);

    pthread_cond_signal(thread_data->finished);
    thread_data->threadID = 0;

    pthread_mutex_unlock(thread_data->mutex);

    return 0;

error:

    return 0;
}

header get_header(int connection, tobServ_thread *arg)
{
    char *headerstring = NULL;
    unsigned int length;
    unsigned int size;
    unsigned int contentlength;
    unsigned int contentwritten;
    char *content = NULL;
    char *found, *getvarstring;
    char **lines;
    char **words;

    header result;
    char buffer[256];
    unsigned int n,i,a;
    unsigned int headerend;
    unsigned int numlines, num;
    unsigned int numwords;

    size=512;
    length=0;
    headerstring = malloc(size);
    headerstring[0] = '\0';
    result.success = 1;

    while((found = strstr(headerstring, "\r\n\r\n"))==NULL)
    {
        n = read(connection,buffer,255);
        if(n<1)
        {
            result.success = 0;
            free(headerstring);
            return result;
        }
        buffer[n] = '\0';

        if((length+n+1)>=size)
        {
            size += 512;

            headerstring = realloc(headerstring, size);
            strcat(headerstring, buffer);
            length += n;
        }
        else
        {
            strcat(headerstring, buffer);
            length += n;
        }
        arg->last_active = time(NULL);

    }

    headerend = (size_t)found - (size_t)headerstring;

    headerend = headerend + strlen("\r\n\r\n");

    headerstring[headerend-1] = '\0';

    numlines = explode(&lines, headerstring, "\r\n");

    if(numlines<3)
    {
        result.numinfos = 0;
        stringcpy(result.method, "INVALID", sizeof(result.method));
        stringcpy(result.path, "DROPPED", sizeof(result.path));
        result.success = 0;
        free(lines);
        lines = NULL;
    }
    else
    {
        numwords = explode(&words, lines[0], " ");
        if(numwords!=3)
        {
            result.numinfos = 0;
            stringcpy(result.method, "INVALID", sizeof(result.method));
            stringcpy(result.path, "DROPPED", sizeof(result.path));
            result.success = 0;
            free(lines);
            free(words);
            lines = NULL;
            words = NULL;
        }
        else
        {
            stringcpy(result.method, words[0], sizeof(result.method));
            stringcpy(result.path, words[1], sizeof(result.path));
            stringcpy(result.version, words[2], sizeof(result.version));

            free(words);
            words = NULL;

            result.numinfos = numlines-2;

            result.infos = malloc(sizeof(tobServ_HeaderInfo)*result.numinfos);

            for(i=1; i<numlines-1; i++)
            {
                if(!strcmp(lines[i], ""))
                    break;

                numwords = explode(&words, lines[i], ": ");

                if(numwords!=2)
                {
                    result.numinfos = 0;
                    stringcpy(result.method, "INVALID", sizeof(result.method));
                    stringcpy(result.path, "DROPPED", sizeof(result.path));
                    free(result.infos);
                    result.infos = NULL;
                    result.success = 0;

                    free(words);
                    words = NULL;
                    break;
                }

                stringcpy(result.infos[i-1].name, words[0], sizeof(result.infos[i].name));
                stringcpy(result.infos[i-1].value, words[1], sizeof(result.infos[i].value));

                free(words);
                words = NULL;
            }
            free(lines);
            lines = NULL;
        }

        contentlength = 0;

        for(i=0; i<result.numinfos; i++)
        {
            if(!strcmp(result.infos[i].name, "Content-Length"))
            {
                contentlength = atoi(result.infos[i].value);
            }
            else if(!strcmp(result.infos[i].name, "Host"))
            {
                if (strlen(result.infos[i].value) < sizeof(result.host)) stringcpy(result.host, result.infos[i].value, sizeof(result.host));
                else stringcpy(result.host, "INVALID", sizeof(result.host));
            }
        }

        content = NULL;
        if(contentlength)
        {
            content = malloc(sizeof(char)*(contentlength+1));

            contentwritten=0;

            for(i=0; i<(length-headerend); i++)
            {
                contentwritten++;
                content[i]=headerstring[headerend+i];
            }

            while(contentwritten<contentlength)
            {
                n = read(connection,buffer,255);
                buffer[n] = '\0';
                check(n>=0, "ERROR reading from socket");

                if((contentwritten+n)>contentlength)
                {
                    result.numinfos = 0;
                    stringcpy(result.method, "INVALID", sizeof(result.method));
                    stringcpy(result.path, "DROPPED", sizeof(result.path));
                    result.success = 0;
                    free(result.infos);
                    result.infos = NULL;
                    free(lines);
                    lines = NULL;
                    free(words);
                    words = NULL;
                    free(content);
                    content = NULL;
                    break;
                }


                for(i=0; i<n; i++)
                {
                    content[contentwritten]=buffer[i];
                    contentwritten++;
                }
            }

            if(content)
                content[contentwritten] = '\0';

        }

    }

    result.numpostdata = 0;
    result.postdata = NULL;

    if(!strcmp(result.method, "POST") && content)
    {
        result.numpostdata = explode(&lines, content, "&");
        if(result.numpostdata)
            result.postdata = malloc(sizeof(tobServ_PostData)*result.numpostdata);

        for(i=0; i<result.numpostdata; i++)
        {
            numwords = explode(&words, lines[i], "=");

            if(numwords!=2)
            {
                free(result.postdata);

                result.numpostdata = -1;
                result.postdata = NULL;

                stringcpy(result.method, "INVALID", sizeof(result.method));
                stringcpy(result.path, "DROPPED", sizeof(result.path));
                result.success = 0;
                free(words);
                words = NULL;

                break;
            }
            else
            {
                result.postdata[i].name = malloc(sizeof(char)*(strlen(words[0])+1));
                strcpy(result.postdata[i].name, words[0]);
                result.postdata[i].value = malloc(sizeof(char)*(strlen(words[1])+1));
                strcpy(result.postdata[i].value, words[1]);
                urldecode(result.postdata[i].value);
            }

            free(words);
            words = NULL;
        }

        free(lines);
        lines = NULL;
    }

//parse get variables
    result.getdata = NULL;
    result.numgetdata = 0;

    if((getvarstring = strchr(result.path, '?')))
    {
        getvarstring++;//skip the "?"
        
        num = result.numgetdata = explode(&lines, getvarstring, "&");
        if(result.numgetdata)
            result.getdata = malloc(sizeof(tobServ_GetData)*result.numgetdata);

        a=0;
        for(i=0;i<num;i++)
        {
            numwords = explode(&words, lines[i], "=");

            if(numwords!=2)
            {
                //invalid get variable
                result.numgetdata--;
                result.getdata = realloc(result.getdata, sizeof(tobServ_GetData)*result.numgetdata);
            }
            else
            {
                result.getdata[a].name = malloc(sizeof(char)*(strlen(words[0])+1));
                strcpy(result.getdata[a].name, words[0]);
                result.getdata[a].value = malloc(sizeof(char)*(strlen(words[1])+1));
                strcpy(result.getdata[a].value, words[1]);
                urldecode(result.getdata[a].value);
                a++;
            }
            free(words);
            words = NULL;
        }
        
        free(lines);
        lines = NULL;
    }

    if(headerstring)
        free(headerstring);
    if(content)
        free(content);

    headerstring = NULL;
    content = NULL;

    return result;

error:
    return result;
}

void send_response(int32_t connection, char *type, char *content, uint32_t size, uint32_t sessioncode, uint32_t usecache, uint32_t code, uint32_t nocookies)
{
    char *status, *cache;  
    switch(code)
    {
    case 200:
        status = "200 OK";
        break;
    case 301:
    case 302:
        send_redirect(connection, content, code);
        return;
    case 400:
        status = "400 Bad Request";
        break;
    case 401:
        status = "401 Unauthorized";
        break;
    case 403:
        status = "402 Forbidden";
        break;
    case 404:
        status = "404 Not Found";
        break;
    case 500:
    status = "500 Internal Server Error";
    break;
    case 503:
        status = "503 Service Unavailable";
        break;
    case 451:
        status = "451 Unavailable For Legal Reasons";
        break;
    }
    
    char headerstring[256];
    int totalsent, sent;
    

    headerstring[0] = '\0';
    if(usecache)
        cache = "Cache-Control: max-age=10000\r\n";
    else
        cache = "";

    if (!nocookies)
        snprintf(headerstring, 256, "HTTP/1.1 %s\r\nServer: tobServ V%s\r\nSet-Cookie: session=%i; Path=/; Max-Age=10000; Version=\"1\"\r\n%sContent-Length: %i\r\nContent-Language: de\r\nContent-Type: %s\nConnection: close\r\n\r\n", status, VERSION, sessioncode, cache, size, type);   

    else
        snprintf(headerstring, 256, "HTTP/1.1 %s\r\nServer: tobServ V%s\r\n%sContent-Length: %i\r\nContent-Language: de\r\nContent-Type: %s\nConnection: close\r\n\r\n", status, VERSION, cache, size, type); 

    write(connection,headerstring,strlen(headerstring));

    sent = send(connection,content,size, MSG_NOSIGNAL);  

    if(sent<0)
        return;

    totalsent = sent;

    while(totalsent<size)
    {
        sent = send(connection,content+totalsent,size-totalsent, MSG_NOSIGNAL);

        if(sent<0)
            return;

        totalsent += sent;
    }
}
void send_redirect(int32_t connection, char *content, uint32_t code) // redirect-location will be transmitted via content
{
    char *status;
    char headerstring[256];

    switch(code)
    {
        case 301:   
            status = "301 Moved Permanently";
            break;
    
        case 302:   
            status = "302 Found";
            break;

        default:
            sentinel("Invalid Code for Redirect");

    }

    snprintf(headerstring, 256, "HTTP/1.1 %s\r\nServer: tobServ V%s\r\nLocation:%s\r\nConnection: close\r\n\r\n", status, VERSION, content);
    write(connection,headerstring,strlen(headerstring));
    return;

error:
    return;
}

int FreeResponse(tobServ_response response)
{
    if(response.response)
        free(response.response);
    if(response.type)
        free(response.type);

    response.response = NULL;
    response.type = NULL;

    return 0;
}

int FreeHeader(header result)
{
    unsigned int i;

    if(result.infos)
        free(result.infos);

    for(i=0; i<result.numpostdata; i++)
    {
        if(result.postdata[i].name)
            free(result.postdata[i].name);

        if(result.postdata[i].value)
            free(result.postdata[i].value);
    }    

    if(result.postdata)
        free(result.postdata);

    for(i=0; i<result.numgetdata; i++)
    {
        if(result.getdata[i].name)
            free(result.getdata[i].name);

        if(result.getdata[i].value)
            free(result.getdata[i].value);
    }    

    if(result.getdata)
        free(result.getdata);

    return 0;
}

int FreeSessions(tobServ_SessionList *sessionlist)
{
    int i;

    if(!sessionlist->initialized) //nothing todo
        return 0;

    pthread_mutex_lock(sessionlist->mutex_session);

    for(i=0; i<sessionlist->num; i++)
    {
        if(sessionlist->sessions[i].variables)
            free(sessionlist->sessions[i].variables);
    }
    if(sessionlist->sessions)
        free(sessionlist->sessions);

    sessionlist->sessions = NULL;
    sessionlist->num = 0;

    pthread_mutex_unlock(sessionlist->mutex_session);

    sessionlist->initialized = 0;

    return 0;
}

int64_t StartSession(tobServ_SessionList *sessionlist, char *IP, uint64_t code) //-1 already exists else new code returned
{
    uint32_t i, a, position;
    int64_t left, right;
    uint64_t newcode;

    uint32_t newnum, currenttime;
    tobServ_Session *newsessions;

    currenttime = time(NULL);

    pthread_mutex_lock(sessionlist->mutex_session);

    //if other sessions exist already check for expired ones
    if(sessionlist->sessions)
    {
        newnum = sessionlist->num;
        for(i=0; i<sessionlist->num; i++)
        {
            if(sessionlist->sessions[i].expire<currenttime)
                newnum--;
        }

        //if all sessions expired then null the session list
        if(!newnum)
        {
            free(sessionlist->sessions);
            sessionlist->sessions = NULL;
            sessionlist->num = 0;
        }
        else if(newnum != sessionlist->num) //only recreate if something needs to be changed
        {
            newsessions = malloc(sizeof(tobServ_Session)*newnum);

            a=0;
            for(i=0; i<sessionlist->num; i++)
            {
                if(sessionlist->sessions[i].expire > currenttime)
                {
                    newsessions[a].code = sessionlist->sessions[i].code;
                    newsessions[a].num = sessionlist->sessions[i].num;
                    newsessions[a].variables = sessionlist->sessions[i].variables;
                    newsessions[a].expire = sessionlist->sessions[i].expire;
                    stringcpy(newsessions[a].IP, sessionlist->sessions[i].IP, 20);
                    a++;
                }
            }

            free(sessionlist->sessions);
            sessionlist->sessions = newsessions;
            sessionlist->num = newnum;
        }
    }

    //empty?
    if(!sessionlist->num)
    {
        position = 0; //write new session at the very first position
    }
    else //get the correct position
    {

        //search for the correct position
        left = 0;
        right = sessionlist->num-1;

        while(right >= left)
        {
            //get the middle
            i = (left + right) / 2;

            if(sessionlist->sessions[i].code < code)
                left = i+1;
            else if(sessionlist->sessions[i].code > code)
                right = i-1;
            else //equal
                break;
        }

        if(right >= left) //exists
        {
            check(!strcmp(sessionlist->sessions[i].IP, IP), "Codes match but IPs don't. Reason could be a poor random function, IP switch of the user or someone trying to steal someones identity using his cookie");
        
            sessionlist->sessions[i].expire = time(NULL)+10000;
            pthread_mutex_unlock(sessionlist->mutex_session);
            return -1;
        }
        else //doesn't exist yet
        {
            //add a new session        
            newcode = rand() * RAND_MAX + rand();
        
            if(code > right)
            {
                position = left;
            }
            else
            {
                position = right;
            }        
        }
    }

    sessionlist->sessions = realloc(sessionlist->sessions, (sessionlist->num+1)*sizeof(tobServ_Session));
    sessionlist->num++;

    //move all if some already exist start from the back
    if(sessionlist->num>1)
    {
        for(i=sessionlist->num-2;i>=position;i--)
        {
            sessionlist->sessions[i+1] = sessionlist->sessions[i];
        }
    }

    //add
    stringcpy(sessionlist->sessions[position].IP, IP, 20);
    sessionlist->sessions[position].code = newcode;
    sessionlist->sessions[position].num = 0;
    sessionlist->sessions[position].expire = time(NULL)+10000;
    sessionlist->sessions[position].variables = NULL;

    pthread_mutex_unlock(sessionlist->mutex_session);

    return newcode;

error:
    return -1;
}

int GetSessionCodeFromCookie(char *cookiestring)
{
    char **cookies;
    int num, i, code;

    num = explode(&cookies, cookiestring, "; ");

    for(i=0; i<num; i++)
    {
        if(!strncmp(cookies[i], "session=", 8))
        {
            code = atoi(cookies[i]+8);
            free(cookies);
            return code;
        }
    }

    if(cookies)
        free(cookies);
    return -1;
}

