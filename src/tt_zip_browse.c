#define _POSIX_C_SOURCE 200809L
#include "tt_zip_browse.h"
#include "f80_file_dialog.h"
#include "tt_synth.h"
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

/* SYX banks are virtual, read-only folders. Each entry is a patch path such as
   bank.syx!03 - BASS.syx; the numeric prefix is the bank voice index. */
static int syx_read_bank(const char *path,uint8_t data[4105]) {
    FILE *file=fopen(path,"rb");
    if(!file)return 0;
    size_t size=fread(data,1,4105,file);
    int failed=ferror(file);fclose(file);
    if(failed || size!=4104)return 0;
    TtDx7Patch patch;
    return tt_dx7_import(data,size,0,&patch);
}
static int syx_split(const char *path,char bank[1024],int *voice) {
    const char *bang=strchr(path,'!');
    if(!bang || (size_t)(bang-path)>=1024 || strlen(bang+1)<2 ||
       bang[1]<'0' || bang[1]>'9' || bang[2]<'0' || bang[2]>'9')return 0;
    size_t n=(size_t)(bang-path);memcpy(bank,path,n);bank[n]='\0';
    if(strcasecmp(bank+((strlen(bank)>4)?strlen(bank)-4:0),".syx"))return 0;
    int index=(bang[1]-'0')*10+(bang[2]-'0');
    if(index>=32)return 0;
    *voice=index;return 1;
}
static int syx_voice_exists(const char *path) {
    char bank[1024];int voice;uint8_t bytes[4105];TtDx7Patch patch;
    if(!syx_split(path,bank,&voice) || !syx_read_bank(bank,bytes))return 0;
    return tt_dx7_import(bytes,4104,voice,&patch);
}
static int syx_browse_list(void *user,const char *directory,F80FileEntry *entries,
                           int capacity,int *count,char *error,size_t error_size) {
    (void)user;
    uint8_t bytes[4105];
    if(syx_read_bank(directory,bytes)) {
        *count=0;
        for(int voice=0;voice<32 && *count<capacity;++voice) {
            TtDx7Patch patch;
            if(!tt_dx7_import(bytes,4104,voice,&patch))continue;
            char name[256];char voice_name[11];
            memcpy(voice_name,bytes+6+voice*128+118,10);voice_name[10]='\0';
            for(int i=0;i<10;++i)if((unsigned char)voice_name[i]<32 ||
                                     (unsigned char)voice_name[i]>126)voice_name[i]='_';
            for(int i=9;i>=0 && voice_name[i]==' ';--i)voice_name[i]='\0';
            int n=snprintf(name,sizeof(name),"%02d - %.10s.syx",voice,voice_name);
            if(n<0 || (size_t)n>=sizeof(name))continue;
            snprintf(entries[*count].name,sizeof(entries[*count].name),"%s",name);
            entries[*count].directory=0;entries[*count].size=4104;++*count;
        }
        error[0]='\0';return 1;
    }
    const F80FileBrowseApi *zip=tt_zip_browse_api();
    int ok=zip->list(zip->user,directory,entries,capacity,count,error,error_size);
    if(!ok)return 0;
    if(strchr(directory,'!'))return 1;
    for(int i=0;i<*count;++i)if(!entries[i].directory) {
        char candidate[1024];
        if(zip->join(zip->user,directory,entries[i].name,candidate,sizeof(candidate)) &&
           syx_read_bank(candidate,bytes)) {
            entries[i].directory=1;entries[i].size=0;
        }
    }
    return 1;
}
static int syx_browse_join(void *user,const char *directory,const char *name,
                           char *path,size_t path_size) {
    (void)user;
    if(!strchr(directory,'!') && syx_read_bank(directory,(uint8_t[4105]){0})) {
        int n=snprintf(path,path_size,"%s!%s",directory,name);
        return n>=0 && (size_t)n<path_size;
    }
    return tt_zip_browse_api()->join(NULL,directory,name,path,path_size);
}
static int syx_browse_parent(void *user,const char *directory,char *path,
                             size_t path_size) {
    (void)user;
    char bank[1024];int voice;
    if(syx_split(directory,bank,&voice)) {
        int n=snprintf(path,path_size,"%s",bank);
        return n>=0 && (size_t)n<path_size;
    }
    return tt_zip_browse_api()->parent(NULL,directory,path,path_size);
}
static int syx_browse_stat(void *user,const char *path,int *directory,
                           unsigned long long *size) {
    (void)user;
    if(!strchr(path,'!') && syx_read_bank(path,(uint8_t[4105]){0})) {
        if(directory)*directory=1;
        if(size)*size=0;
        return 1;
    }
    if(syx_voice_exists(path)) {
        if(directory)*directory=0;
        if(size)*size=4104;
        return 1;
    }
    return tt_zip_browse_api()->stat(NULL,path,directory,size);
}
const F80FileBrowseApi *tt_zip_syx_browse_api(void) {
    static const F80FileBrowseApi api={NULL,syx_browse_list,syx_browse_join,
                                       syx_browse_parent,syx_browse_stat};
    return &api;
}
