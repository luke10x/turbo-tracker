#define _POSIX_C_SOURCE 200809L
#include "tt_zip_browse.h"
#include "f80_file_dialog.h"
#include "../third_party/miniz/miniz.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int zip_browse_archive(const char *path,char archive[1024],
                              const char **inside) {
    const char *bang=strchr(path,'!');
    size_t length=bang?(size_t)(bang-path):strlen(path);
    if(!length || length>=1024)return 0;
    memcpy(archive,path,length);archive[length]='\0';
    if(!bang) {
        size_t n=strlen(path);
        if(n<4 || strcasecmp(path+n-4,".zip"))return 0;
        if(inside)*inside="";
    } else if(inside) {
        *inside=bang+1;
        while(**inside=='/')++*inside;
    }
    mz_zip_archive zip={0};
    if(!mz_zip_reader_init_file(&zip,archive,0))return 0;
    mz_zip_reader_end(&zip);
    return 1;
}

static int zip_browse_safe_name(const char *name) {
    return name && name[0] && name[0]!='/' && name[0]!='\\' &&
           !strstr(name,"../") && !strstr(name,"..\\") &&
           strcmp(name,"..") && !strstr(name,"/../") &&
           !strstr(name,"\\..\\");
}

static int zip_browse_is_archive(const char *name) {
    size_t length=name?strlen(name):0;
    return length>=4 && !strcasecmp(name+length-4,".zip");
}

static int zip_browse_add(F80FileEntry *entries,int capacity,int *count,
                          const char *name,int directory,unsigned long long size) {
    size_t length=strlen(name);
    if(!length || length>=sizeof(entries[0].name))return 1;
    for(int i=0;i<*count;++i)if(!strcmp(entries[i].name,name)) {
        if(directory)entries[i].directory=1;
        if(size>entries[i].size)entries[i].size=size;
        return 1;
    }
    if(*count>=capacity)return 1;
    F80FileEntry *entry=&entries[(*count)++];
    snprintf(entry->name,sizeof(entry->name),"%s",name);
    entry->directory=directory;entry->size=directory?0:size;
    return 1;
}

static int zip_browse_list(void *user,const char *directory,
                           F80FileEntry *entries,int capacity,int *count,
                           char *error,size_t error_size) {
    const F80FileBrowseApi *local=f80_file_dialog_local_browse_api();
    char archive[1024];const char *inside="";
    *count=0;
    if(!zip_browse_archive(directory,archive,&inside)) {
        int ok=local->list(local->user,directory,entries,capacity,count,error,error_size);
        if(!ok)return 0;
        for(int i=0;i<*count;++i)if(!entries[i].directory) {
            char candidate[F80_FILE_DIALOG_PATH_MAX];
            if(local->join(local->user,directory,entries[i].name,candidate,
                           sizeof(candidate)) &&
               zip_browse_archive(candidate,archive,NULL)) {
                entries[i].directory=1;entries[i].size=0;
            }
        }
        return 1;
    }
    mz_zip_archive zip={0};
    if(!mz_zip_reader_init_file(&zip,archive,0)) {
        snprintf(error,error_size,"Cannot open ZIP archive");return 0;
    }
    char prefix[1024];
    size_t prefix_length=strlen(inside);
    while(prefix_length && inside[prefix_length-1]=='/')--prefix_length;
    if(prefix_length>=sizeof(prefix)) {
        mz_zip_reader_end(&zip);snprintf(error,error_size,"ZIP path is too long");return 0;
    }
    memcpy(prefix,inside,prefix_length);prefix[prefix_length]='\0';
    if(prefix_length) { prefix[prefix_length++]='/';prefix[prefix_length]='\0'; }
    mz_uint total=mz_zip_reader_get_num_files(&zip);
    for(mz_uint i=0;i<total;++i) {
        mz_zip_archive_file_stat stat;
        if(!mz_zip_reader_file_stat(&zip,i,&stat) ||
           !zip_browse_safe_name(stat.m_filename) ||
           strncmp(stat.m_filename,prefix,prefix_length))continue;
        const char *remainder=stat.m_filename+prefix_length;
        if(!remainder[0])continue;
        const char *slash=strchr(remainder,'/');
        if(slash) {
            char child[256];size_t n=(size_t)(slash-remainder);
            if(!n || n>=sizeof(child))continue;
            memcpy(child,remainder,n);child[n]='\0';
            zip_browse_add(entries,capacity,count,child,1,0);
        } else {
            size_t n=strlen(remainder);
            if(n && remainder[n-1]=='/') {
                char child[256];if(n>=sizeof(child))continue;
                memcpy(child,remainder,n-1);child[n-1]='\0';
                zip_browse_add(entries,capacity,count,child,1,0);
            } else {
                if(zip_browse_is_archive(remainder))continue;
                zip_browse_add(entries,capacity,count,remainder,
                               stat.m_is_directory,
                               (unsigned long long)stat.m_uncomp_size);
            }
        }
    }
    mz_zip_reader_end(&zip);
    error[0]='\0';
    return 1;
}

static int zip_browse_join(void *user,const char *directory,const char *name,
                           char *path,size_t path_size) {
    const F80FileBrowseApi *local=f80_file_dialog_local_browse_api();
    if(name[0]=='/') {
        int n=snprintf(path,path_size,"%s",name);
        return n>=0 && (size_t)n<path_size;
    }
    char archive[1024];const char *inside;
    if(zip_browse_archive(directory,archive,&inside)) {
        int n;
        if(strchr(directory,'!'))
            n=snprintf(path,path_size,"%s%s%s",directory,
                       inside[0]?"/":"",name);
        else n=snprintf(path,path_size,"%s!%s",directory,name);
        return n>=0 && (size_t)n<path_size;
    }
    return local->join(local->user,directory,name,path,path_size);
}

static int zip_browse_parent(void *user,const char *directory,
                             char *path,size_t path_size) {
    const F80FileBrowseApi *local=f80_file_dialog_local_browse_api();
    const char *bang=strchr(directory,'!');
    if(!bang)return local->parent(local->user,directory,path,path_size);
    size_t archive_length=(size_t)(bang-directory);
    const char *inside=bang+1;
    while(*inside=='/')++inside;
    size_t inside_length=strlen(inside);
    while(inside_length && inside[inside_length-1]=='/')--inside_length;
    if(!inside_length) {
        char archive[1024];
        if(archive_length>=sizeof(archive))return 0;
        memcpy(archive,directory,archive_length);archive[archive_length]='\0';
        return local->parent(local->user,archive,path,path_size);
    }
    const char *slash=NULL;
    for(size_t i=0;i<inside_length;++i)if(inside[i]=='/')slash=inside+i;
    size_t keep=slash?(size_t)(slash-inside):0;
    int n=keep?snprintf(path,path_size,"%.*s!%.*s",(int)archive_length,
                        directory,(int)keep,inside):
               snprintf(path,path_size,"%.*s!",(int)archive_length,directory);
    return n>=0 && (size_t)n<path_size;
}

static int zip_browse_stat(void *user,const char *path,int *directory,
                           unsigned long long *size) {
    const F80FileBrowseApi *local=f80_file_dialog_local_browse_api();
    const char *bang=strchr(path,'!');
    char archive[1024];const char *inside="";
    if(!bang) {
        if(zip_browse_archive(path,archive,NULL)) {
            if(directory)*directory=1;
            if(size)*size=0;
            return 1;
        }
        return local->stat(local->user,path,directory,size);
    }
    if(!zip_browse_archive(path,archive,&inside) ||
       (inside[0] && !zip_browse_safe_name(inside)))return 0;
    if(inside[0] && zip_browse_is_archive(inside))return 0;
    if(!inside[0]) {
        if(directory)*directory=1;
        if(size)*size=0;
        return 1;
    }
    mz_zip_archive zip={0};
    if(!mz_zip_reader_init_file(&zip,archive,0))return 0;
    int found=mz_zip_reader_locate_file(&zip,inside,NULL,0);
    if(found>=0) {
        mz_zip_archive_file_stat stat;
        if(mz_zip_reader_file_stat(&zip,(mz_uint)found,&stat)) {
            if(directory)*directory=stat.m_is_directory;
            if(size)*size=(unsigned long long)stat.m_uncomp_size;
            mz_zip_reader_end(&zip);return 1;
        }
    }
    char prefix[1024];int n=snprintf(prefix,sizeof(prefix),"%s/",inside);
    mz_uint total=mz_zip_reader_get_num_files(&zip);
    int is_directory=0;
    if(n>0 && (size_t)n<sizeof(prefix))for(mz_uint i=0;i<total;++i) {
        mz_zip_archive_file_stat stat;
        if(mz_zip_reader_file_stat(&zip,i,&stat) &&
           !strncmp(stat.m_filename,prefix,(size_t)n)) { is_directory=1;break; }
    }
    mz_zip_reader_end(&zip);
    if(!is_directory)return 0;
    if(directory)*directory=1;
    if(size)*size=0;
    return 1;
}

const F80FileBrowseApi *tt_zip_browse_api(void) {
    static const F80FileBrowseApi api={NULL,zip_browse_list,zip_browse_join,
                                       zip_browse_parent,zip_browse_stat};
    return &api;
}
