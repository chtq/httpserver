#include <pthread.h>
#include <time.h>
#include <malloc.h>
#include <string.h>

#include "FileCache.h"
#include "Template.h"

int InitializeFileCache(tobServ_FileCache *filecache, int maxfiles, int maxfilesize)
{
    if(maxfilesize<1)
	return 1;
    if(maxfiles<1)
	return 2;
    
    filecache->numfiles = 0;
    filecache->active = 1;
    filecache->maxfiles = maxfiles;
    filecache->maxfilesize = maxfilesize;
    filecache->files = malloc(sizeof(tobServ_CachedFile)*maxfiles);

    pthread_mutex_init(&filecache->freeTookPlaceMutex, NULL);
    pthread_cond_init(&filecache->freeTookPlaceCond, NULL);
    pthread_rwlock_init(&filecache->lock, NULL);

    return 0;
}

int FreeFileCache(tobServ_FileCache *filecache)
{
    int i;
    
    pthread_rwlock_destroy(&filecache->lock);
    pthread_mutex_destroy(&filecache->freeTookPlaceMutex);
    pthread_cond_destroy(&filecache->freeTookPlaceCond);
    
    for(i=0;i<filecache->numfiles;i++)
    {
	pthread_mutex_destroy(&filecache->files[i].filelock);

	free(filecache->files[i].file->content);
	FreeParsed(&filecache->files[i].file->parsedFile);
	
	free(filecache->files[i].file);
    }

    if(filecache->files)
	free(filecache->files);

    return 0;
}

struct _tobServ_file GetFileFromFileCache(tobServ_FileCache* filecache, char *path, int parse)
{
    int i;
    struct _tobServ_file result;

    if(!filecache->active) //if caching is not active right now get it from disk
    {
	result = LoadFileFromDisk(path);
	if(parse)
	    result.parsedFile = ParseFile(&result);

	result.cacheID = -1;
    }
    
    pthread_rwlock_rdlock(&filecache->lock);

    for(i=0;i<filecache->numfiles;i++)
    {
	if(!strcmp(filecache->files[i].path, path))
	{
	    pthread_mutex_lock(&filecache->files[i].filelock);

	    filecache->files[i].usecount++;
	    filecache->files[i].lastaccess = time(NULL);

	    if(parse)
	    {
		//if not parsed already, parse it
		if(filecache->files[i].file->parsedFile.type < 0)
		    filecache->files[i].file->parsedFile = ParseFile(filecache->files[i].file);
	    }

	    pthread_mutex_unlock(&filecache->files[i].filelock);
				 
	    break;
	}
    }

    pthread_rwlock_unlock(&filecache->lock);

    if(i==filecache->numfiles) //file not found in cache
    {
	result = LoadFileFromDisk(path);
	if(parse)
	    result.parsedFile = ParseFile(&result);

	result.cacheID = AddFileToCache(filecache, path, result);
    }
    else
	return *filecache->files[i].file;
	
    return result;
}

int AddFileToCache(tobServ_FileCache* filecache, char *path, tobServ_file file)
{
    int highestDeletePriority;
    int lowestLastUsed=0;
    int i;
    int newID = -3;
    
    //check if too large
    pthread_rwlock_rdlock(&filecache->lock);
    
    if(file.size > filecache->maxfilesize)
    {
	pthread_rwlock_unlock(&filecache->lock);
	return -1;
    }

    pthread_rwlock_unlock(&filecache->lock);

    //check if we have space if not create space
    pthread_rwlock_wrlock(&filecache->lock);

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
	    FreeParsed(&filecache->files[newID].file->parsedFile);

	    filecache->files[newID].file->content = file.content;
	    filecache->files[newID].file->size = file.size;
	    filecache->files[newID].file->type = file.type;
	    filecache->files[newID].file->parsedFile = file.parsedFile;

	    filecache->files[newID].lastaccess = time(NULL);
	    filecache->files[newID].usecount = 1; //adder is using it

	    filecache->files[newID].file->cacheID = newID;
	}
	else //cannot cache
	{
	    pthread_rwlock_unlock(&filecache->lock);
	    return -2;
	}
    }
    else //we still have some space left
    {
	newID = filecache->numfiles;

	filecache->files[newID].file = malloc(sizeof(tobServ_file));
	filecache->files[newID].file->content = file.content;
	filecache->files[newID].file->size = file.size;
	filecache->files[newID].file->type = file.type;
	filecache->files[newID].file->parsedFile = file.parsedFile;

	filecache->files[newID].lastaccess = time(NULL);
	*filecache->files[newID].file = file;
	filecache->files[newID].usecount = 1; //adder is using it
	pthread_mutex_init(&filecache->files[newID].filelock, NULL); //new mutex lock

	filecache->files[newID].file->cacheID = filecache->numfiles;

	filecache->numfiles++;
    }
    
    pthread_rwlock_unlock(&filecache->lock);
    return newID;
}

tobServ_file LoadFileFromDisk(char *path)
{
    FILE *handle;
    char *buffer;
    char *type;
    size_t result;
    tobServ_file file;
    int size;

    type = malloc(MAX_FILETYPE_SIZE);

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

    result = fread(buffer, 1, size, handle);
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

    file.parsedFile.type = -1;

    return file;
}

int FreeFileFromFileCache(tobServ_FileCache* filecache, tobServ_file* file)
{  
    if(file->cacheID>0) //it is cached
    {
	pthread_rwlock_rdlock(&filecache->lock);

	pthread_mutex_lock(&filecache->files[file->cacheID].filelock);
	filecache->files[file->cacheID].usecount--;
	pthread_mutex_unlock(&filecache->files[file->cacheID].filelock);				 

	pthread_rwlock_unlock(&filecache->lock);

	pthread_mutex_lock(&filecache->freeTookPlaceMutex);
	filecache->freeTookPlace = 1;
	pthread_cond_broadcast(&filecache->freeTookPlaceCond);
	pthread_mutex_unlock(&filecache->freeTookPlaceMutex);
    }

    return 0;
}

int AlterAndEmptyFileCache(tobServ_FileCache* filecache, int maxfiles, int maxfilesize)
{
    int i;
    int newmaxfiles;
    int newmaxfilesize;
    
    pthread_rwlock_wrlock(&filecache->lock);    
    filecache->active = 0; //deactivate caching
    
    if(maxfiles<1)
	newmaxfiles = filecache->maxfiles;
    if(maxfilesize<1)
	newmaxfilesize = filecache->maxfilesize;
	
	
    pthread_rwlock_unlock(&filecache->lock);

    while(1)
    {
	//go through all files and check if they are all not used
	pthread_rwlock_wrlock(&filecache->lock);

	for(i=0;i<filecache->numfiles;i++)
	{
	    if(filecache->files[i].usecount>0)
		break;
	}
	if(i==filecache->numfiles)//all not used anymore?
	{
	    pthread_rwlock_unlock(&filecache->lock);
	    break;
	}
	
	pthread_rwlock_unlock(&filecache->lock);

	//wait for a file to be freed
	pthread_mutex_lock(&filecache->freeTookPlaceMutex);
	while (!filecache->freeTookPlace) 
	{
	    pthread_cond_wait(&filecache->freeTookPlaceCond, &filecache->freeTookPlaceMutex);
	}
	filecache->freeTookPlace = 0; //reset
	pthread_mutex_unlock(&filecache->freeTookPlaceMutex);
    }

    //reset everything and apply the new options
    pthread_rwlock_wrlock(&filecache->lock);
    
    FreeFileCache(filecache);
    InitializeFileCache(filecache, newmaxfiles, newmaxfilesize);
    
    filecache->active = 1; //reactivate caching    
    pthread_rwlock_unlock(&filecache->lock);

    return 0;
}

tobServ_file get_file(tobServ_FileCache *filecache, char *path, int parse, int cache)
{
    tobServ_file result;
    
    if(!cache)
    {
	result = LoadFileFromDisk(path);
	result.parsedFile.numparts = 0;
	result.parsedFile.parts = NULL;
	result.parsedFile.type = 0;
	result.cacheID = -1;

	if(parse)
	    result.parsedFile = ParseFile(&result);
	else
	    result.parsedFile.type = -1;
    }
    else
	result = GetFileFromFileCache(filecache, path, parse);

    return result;
}

int free_file(tobServ_FileCache *cache, tobServ_file *file)
{
    if(file->cacheID>0)
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
