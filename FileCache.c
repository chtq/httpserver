#include <pthread.h>
#include <time.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>

#include "FileCache.h"
#include "Template.h"
#include "dbg.h"

int32_t InitializeFile(tobServ_file *file)
{
    file->content = NULL;
    file->type = NULL;
    file->cacheID = -1;
    file->parsedFile.type = -1;

    return 0;
}

int32_t InitializeFileCache(tobServ_FileCache *filecache, uint32_t maxfiles, uint32_t maxfilesize)
{
    if(maxfilesize<1)
	return -1;
    if(maxfiles<1)
	return -2;
    
    filecache->numfiles = 0;
    filecache->active = 1;
    filecache->maxfiles = maxfiles;
    filecache->maxfilesize = maxfilesize;
    filecache->files = malloc(sizeof(tobServ_CachedFile)*maxfiles);
    check_mem(filecache->files);

    check(pthread_mutex_init(&filecache->freeTookPlaceMutex, NULL)==0, "pthread_mutex_init failed");
    check(pthread_cond_init(&filecache->freeTookPlaceCond, NULL)==0, "pthread_cond_init failed");
    check(pthread_rwlock_init(&filecache->lock, NULL)==0, "pthread_rwlock_init failed");

    filecache->initialized = 1;

    return 0;

error:
    FreeFileCache(filecache);
    return -1;
}

int32_t FreeFileCache(tobServ_FileCache *filecache)
{
    uint32_t i;

    if(!filecache->initialized) //nothing todo
        return 0;
    
    pthread_rwlock_destroy(&filecache->lock);
    pthread_mutex_destroy(&filecache->freeTookPlaceMutex);
    pthread_cond_destroy(&filecache->freeTookPlaceCond);
    
    for(i=0;i<filecache->numfiles;i++)
    {
	pthread_mutex_destroy(&filecache->files[i].filelock);

	free(filecache->files[i].file->content);
	free(filecache->files[i].file->type);
	FreeParsed(&filecache->files[i].file->parsedFile);

	free(filecache->files[i].path);
	free(filecache->files[i].file);
    }

    if(filecache->files)
	free(filecache->files);

    return 0;
}

int32_t GetTotalFileCacheSize(tobServ_FileCache* filecache)
{
    uint32_t i, sum=0;
    
    check(pthread_rwlock_rdlock(&filecache->lock)==0, "pthread_rw_lock_rdlock failed");

    for(i=0;i<filecache->numfiles;i++)
	sum += filecache->files[i].file->size;
    
    pthread_rwlock_unlock(&filecache->lock);

    return sum;

error:
    return -1;
}

struct _tobServ_file GetFileFromFileCache(tobServ_FileCache* filecache, char *path, uint32_t parse)
{
    uint32_t i;
    uint32_t isCacheLocked=0, isFileLocked=0;
    struct _tobServ_file result;

    InitializeFile(&result);

    if(!filecache->active) //if caching is not active right now get it from disk
    {
	result = LoadFileFromDisk(path);
	check(result.content, "LoadFileFromDisk with %s failed", path);
	
	if(parse)
	{
	    result.parsedFile = ParseFile(&result);
	    check(result.parsedFile.type>0, "ParseFile failed with %s", path);
	}

	result.cacheID = -1;

	return result;
    }
    
    check(pthread_rwlock_rdlock(&filecache->lock)==0, "pthread_rwlock_rdlock failed");
    isCacheLocked = 1;

    for(i=0;i<filecache->numfiles;i++)
    {
	if(!strcmp(filecache->files[i].path, path))
	{
	    check(pthread_mutex_lock(&filecache->files[i].filelock)==0, "pthread_mutex_lock failed");
	    isFileLocked = 1;

	    filecache->files[i].usecount++;
	    filecache->files[i].lastaccess = time(NULL);

	    if(parse)
	    {
		//if not parsed already, parse it
		if(filecache->files[i].file->parsedFile.type < 0)
		    filecache->files[i].file->parsedFile = ParseFile(filecache->files[i].file);
	    }

	    pthread_mutex_unlock(&filecache->files[i].filelock);
	    isFileLocked=0;
				 
	    break;
	}
    }

    pthread_rwlock_unlock(&filecache->lock);
    isCacheLocked = 0;

    if(i==filecache->numfiles) //file not found in cache
    {
        result = LoadFileFromDisk(path);
        check(result.content, "LoadFileFromDisk failed for %s", path);

        if(parse)
        {
            result.parsedFile = ParseFile(&result);
            check(result.parsedFile.type>=0, "ParseFile failed for %s", path);
        }

        result.cacheID = AddFileToCache(filecache, path, result);
        if (result.cacheID <0) 
        {
            log_warn("AddFileToCache returned %d for %s", result.cacheID, path);
            return result;
        }
    }
    else result = *filecache->files[i].file;
	
    return result;

error:
    free_file(filecache, &result);

    //unlocking order is important to prevent altering of the locks themselves
    if(isFileLocked)
	pthread_mutex_unlock(&filecache->files[i].filelock);
    if(isCacheLocked)
	pthread_rwlock_unlock(&filecache->lock);

    return result;
}

int32_t AddFileToCache(tobServ_FileCache* filecache, char *path, tobServ_file file)
{
    int32_t highestDeletePriority;
    uint32_t lowestLastUsed=0;
    uint32_t i;
    int32_t newID = -3;
    uint32_t isCacheLocked=0;
    
    //check if too large
    check(pthread_rwlock_rdlock(&filecache->lock)==0, "pthread_rwlock_rdlock failed");
    isCacheLocked = 1;
    
    if(file.size > filecache->maxfilesize)
    {
	pthread_rwlock_unlock(&filecache->lock);
	isCacheLocked = 0;
	
	return -1;
    }

    pthread_rwlock_unlock(&filecache->lock);
    isCacheLocked = 0;

    //check if we have space if not create space
    check(pthread_rwlock_wrlock(&filecache->lock)==0, "pthread_rwlock_wrlock failed");
    isCacheLocked = 1;

    if(filecache->numfiles == filecache->maxfiles) //we need space
    {
	highestDeletePriority = -1;
	for(i=0;i<filecache->numfiles;i++)
	{
	    if((filecache->files[i].lastaccess < lowestLastUsed || highestDeletePriority < 0) && filecache->files[i].usecount==0)
	    {
		highestDeletePriority = i;
		lowestLastUsed = filecache->files[i].lastaccess;		
	    }
	}

	if(highestDeletePriority > 0) //check if something to free was found
	{
	    newID = highestDeletePriority;
	    
	    free(filecache->files[newID].file->content);
	    free(filecache->files[newID].file->type);
	    FreeParsed(&filecache->files[newID].file->parsedFile);

	    filecache->files[newID].file->content = file.content;
	    filecache->files[newID].file->size = file.size;
	    filecache->files[newID].file->type = file.type;
	    filecache->files[newID].file->parsedFile = file.parsedFile;

	    filecache->files[newID].path = realloc(filecache->files[newID].path, strlen(path)+1);
	    check_mem(filecache->files[newID].path);
	    
	    strcpy(filecache->files[newID].path, path);
	    
	    filecache->files[newID].lastaccess = time(NULL);
	    filecache->files[newID].usecount = 1; //adder is using it

	    filecache->files[newID].file->cacheID = newID;
	}
	else //cannot cache
	{
	    pthread_rwlock_unlock(&filecache->lock);
	    isCacheLocked = 0;
	    return -2;
	}
    }
    else //we still have some space left
    {
	newID = filecache->numfiles;

	filecache->files[newID].file = malloc(sizeof(tobServ_file));
	check_mem(filecache->files[newID].file);
	
	filecache->files[newID].file->content = file.content;
	filecache->files[newID].file->size = file.size;
	filecache->files[newID].file->type = file.type;
	filecache->files[newID].file->parsedFile = file.parsedFile;

	filecache->files[newID].path = malloc(strlen(path)+1);
	strcpy(filecache->files[newID].path, path);

	filecache->files[newID].lastaccess = time(NULL);
	filecache->files[newID].usecount = 1; //adder is using it
	pthread_mutex_init(&filecache->files[newID].filelock, NULL); //new mutex lock

	filecache->files[newID].file->cacheID = filecache->numfiles;

	filecache->numfiles++;
    }
    
    pthread_rwlock_unlock(&filecache->lock);
    isCacheLocked = 0;

    check(newID>=0, "newID=%i is <0 which shouldn't happen", newID); //shouldn't lower than 0
    
    return newID;

error:
    if(isCacheLocked)
	pthread_rwlock_unlock(&filecache->lock);

    return -3;
}

tobServ_file LoadFileFromDisk(char *path)
{
    FILE *handle;
    char *buffer=NULL;
    char *type = NULL;
    size_t result;
    tobServ_file file;
    uint32_t size;

    InitializeFile(&file);

    handle = fopen(path, "rb");
    check(handle, "fopen failed for %s", path);

    fseek(handle, 0, SEEK_END);
    size = ftell(handle);
    rewind (handle);

    buffer = malloc(size);
    check_mem(buffer);

    result = fread(buffer, 1, size, handle);
    check(result==size, "Couldn't read all in fread for file %s", path);

    type = malloc(MAX_FILETYPE_SIZE);
    check_mem(type);

    get_file_type(type, 100, path);

    fclose(handle);

    file.size = size;
    file.content = buffer;
    file.type = type;

    file.parsedFile.type = -1;

    return file;

error:
    if(type)
	free(type);
    if(buffer)
	free(buffer);

    return file;
}

int32_t FreeFileFromFileCache(tobServ_FileCache* filecache, tobServ_file* file)
{
    uint32_t isCacheLocked=0;
    uint32_t isFileLocked=0;
    uint32_t isFreeTookPlaceLocked=0;
    
    if(file->cacheID>=0) //it is cached
    {
	check(pthread_rwlock_rdlock(&filecache->lock)==0, "pthread_rwlock_rdlock failed");
	isCacheLocked = 1;

	check(pthread_mutex_lock(&filecache->files[file->cacheID].filelock)==0, "pthread_mutex_lock failed");
	isFileLocked = 1;
	
	filecache->files[file->cacheID].usecount--;
	
	pthread_mutex_unlock(&filecache->files[file->cacheID].filelock);
	isFileLocked = 0;

	pthread_rwlock_unlock(&filecache->lock);
	isCacheLocked = 0;

	//notitfy someone waiting for files to be freed that a file just got freed
	check(pthread_mutex_lock(&filecache->freeTookPlaceMutex)==0, "pthread_mutex_lock failed");
	isFreeTookPlaceLocked = 1;
	
	filecache->freeTookPlace = 1;	
	pthread_cond_broadcast(&filecache->freeTookPlaceCond);
	
	pthread_mutex_unlock(&filecache->freeTookPlaceMutex);
	isFreeTookPlaceLocked = 0;
    }

    return 0;

error:

    //unlocking order is very important to prevent altering of the locks
    if(isFreeTookPlaceLocked)
	pthread_mutex_unlock(&filecache->freeTookPlaceMutex);	
    if(isFileLocked)
	pthread_mutex_unlock(&filecache->files[file->cacheID].filelock);
    if(isCacheLocked)
	pthread_mutex_unlock(&filecache->files[file->cacheID].filelock);

    return -1;
}

int32_t AlterAndEmptyFileCache(tobServ_FileCache* filecache, uint32_t maxfiles, uint32_t maxfilesize)
{
    uint32_t i;
    uint32_t newmaxfiles;
    uint32_t newmaxfilesize;

    uint32_t isCacheLocked=0;
    uint32_t isFreeTookPlaceLocked=0;
    
    check(pthread_rwlock_wrlock(&filecache->lock)==0, "pthread_rwlock_wrlock failed");
    isCacheLocked = 1;
    filecache->active = 0; //deactivate caching
    
    if(maxfiles<1)
	newmaxfiles = filecache->maxfiles;
    if(maxfilesize<1)
	newmaxfilesize = filecache->maxfilesize;
	
	
    pthread_rwlock_unlock(&filecache->lock);
    isCacheLocked=0;

    while(1)
    {
	//go through all files and check if they are all not used
	pthread_rwlock_wrlock(&filecache->lock);
	isCacheLocked=1;

	for(i=0;i<filecache->numfiles;i++)
	{
	    if(filecache->files[i].usecount>0)
		break;
	}
	if(i==filecache->numfiles)//all not used anymore?
	{
	    pthread_rwlock_unlock(&filecache->lock);
	    isCacheLocked = 0;
	    break;
	}
	
	pthread_rwlock_unlock(&filecache->lock);
	isCacheLocked = 0;

	//wait for a file to be freed
	pthread_mutex_lock(&filecache->freeTookPlaceMutex);
	isFreeTookPlaceLocked = 1;

	while (!filecache->freeTookPlace) 
	{
	    pthread_cond_wait(&filecache->freeTookPlaceCond, &filecache->freeTookPlaceMutex);
	}
	filecache->freeTookPlace = 0; //reset

	pthread_mutex_unlock(&filecache->freeTookPlaceMutex);
	isFreeTookPlaceLocked = 0;
    }

    //reset everything and apply the new options 
    FreeFileCache(filecache);
    InitializeFileCache(filecache, newmaxfiles, newmaxfilesize);//initializeFileCache reactivates caching.

error:
    if(isFreeTookPlaceLocked)
	pthread_mutex_unlock(&filecache->freeTookPlaceMutex);
    if(isCacheLocked)
	pthread_rwlock_unlock(&filecache->lock);

    return 0;
}

tobServ_file get_file(tobServ_FileCache *filecache, char *path, uint32_t parse, uint32_t cache)
{
    tobServ_file result;

    InitializeFile(&result);
    
    if(!cache)
    {
	result = LoadFileFromDisk(path);
	check(result.content, "LoadFileFromDisk failed for %s", path);
	
	result.parsedFile.numparts = 0;
	result.parsedFile.parts = NULL;
	result.parsedFile.type = 0;
	result.cacheID = -1;

	if(parse && result.content)
	{
	    result.parsedFile = ParseFile(&result);
	    check(result.parsedFile.type>=0, "ParseFile failed for %s", path);
	}
	else
	    result.parsedFile.type = -1;
    }
    else
    {
	result = GetFileFromFileCache(filecache, path, parse);
	check(result.content, "GetFileFromFileCache failed for %s", path);
    }

    return result;

error:
    free_file(filecache, &result);

    return result;
}

int32_t free_file(tobServ_FileCache *cache, tobServ_file *file)
{
    if(file->cacheID>=0)
	FreeFileFromFileCache(cache, file);
    else
    {
	if(file->content)
	    free(file->content);
	if(file->type)
	    free(file->type);

	if(file->parsedFile.type>=0)
	    FreeParsed(&file->parsedFile);
    }

    //set all to zero
    InitializeFile(file);

    return 0;
}

int32_t get_file_type(char *type, uint32_t size, char *path)
{
    char *ending=NULL;

    char *temptype;

    ending = strrchr(path,'.');

    if(ending==NULL)
        temptype = "application/octet-stream";
    else if(!strcmp((ending+1), "html") || !strcmp((ending+1), "htm"))
        temptype = "text/html";
    else if(!strcmp((ending+1), "css"))
        temptype = "text/css";
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
