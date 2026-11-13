#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "tt_package.h"
#include "../third_party/miniz/miniz.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct PackageDir {
    char path[PATH_MAX];
    struct PackageDir *next;
} PackageDir;
static PackageDir *package_dirs;

static void remove_package_dir(const char *dir) {
    char path[PATH_MAX];
    if(snprintf(path,sizeof(path),"%s/song.ttt",dir)<(int)sizeof(path))unlink(path);
    for(int i=1;i<=TT_SONG_SAMPLES;++i) {
        if(snprintf(path,sizeof(path),"%s/samples/%02d.wav",dir,i)<(int)sizeof(path))unlink(path);
    }
    if(snprintf(path,sizeof(path),"%s/samples",dir)<(int)sizeof(path))rmdir(path);
    if(snprintf(path,sizeof(path),"%s/song.song",dir)<(int)sizeof(path))unlink(path);
    rmdir(dir);
}
void tt_package_cleanup_all(void) {
    while(package_dirs) {
        PackageDir *next=package_dirs->next;
        remove_package_dir(package_dirs->path);
        free(package_dirs);package_dirs=next;
    }
}
int tt_package_is_path(const char *path) {
    size_t n=strlen(path);
    if((n>=8 && !strcmp(path+n-8,".tts.zip")) ||
       (n>=8 && !strcmp(path+n-8,".ttt.zip")))return 1;
    if(n<4 || strcmp(path+n-4,".ttt"))return 0;
    FILE *file=fopen(path,"rb");
    if(!file)return 0;
    char line[4096];size_t total=0;int packed=0;
    if(fgets(line,sizeof(line),file) && !strncmp(line,"F80_SAMPLE_SONG\t",16)) {
        do {
            size_t length=strlen(line);
            total+=length;
            if(total>8u*1024u*1024u || !length || line[length-1]!='\n')break;
            if(!strcmp(line,"END\n") || !strcmp(line,"END\r\n")) {
                unsigned char signature[4];
                packed=fread(signature,1,sizeof(signature),file)==sizeof(signature) &&
                       signature[0]=='P' && signature[1]=='K' &&
                       ((signature[2]==3 && signature[3]==4) ||
                        (signature[2]==5 && signature[3]==6));
                break;
            }
        } while(fgets(line,sizeof(line),file));
    }
    fclose(file);return packed;
}
static int zip_entry(mz_zip_archive *zip,const char *name,size_t maximum,
                     mz_uint *index,char *error,size_t error_size) {
    int found=mz_zip_reader_locate_file(zip,name,NULL,0);
    if(found<0)return 0;
    mz_zip_archive_file_stat stat;
    if(!mz_zip_reader_file_stat(zip,(mz_uint)found,&stat) ||
       stat.m_uncomp_size>maximum || stat.m_is_directory) {
        snprintf(error,error_size,"Invalid or oversized package entry: %s",name);
        return -1;
    }
    *index=(mz_uint)found;return 1;
}
int tt_package_save(const char *path,const TtSong *song,
                    const TtSongSample *samples,int count,
                    const char *const sample_paths[TT_SONG_SAMPLES],
                    const TtWave *const waves[TT_SONG_SAMPLES],
                    char *error,size_t error_size) {
    if(count<1 || count>TT_SONG_SAMPLES) {
        snprintf(error,error_size,"Invalid instrument count");return 0;
    }
    char song_tmp[]="/tmp/tts-song-XXXXXX";
    int fd=mkstemp(song_tmp);
    if(fd<0) { snprintf(error,error_size,"Cannot prepare packed song");return 0; }
    close(fd);
    TtSongSample packed[TT_SONG_SAMPLES];
    memcpy(packed,samples,(size_t)count*sizeof(*packed));
    for(int i=0;i<count;++i) {
        if(packed[i].instrument_type==TT_INSTRUMENT_DX7 && !packed[i].frames)
            snprintf(packed[i].path,sizeof(packed[i].path),"builtin:dx7");
        else if(!packed[i].path_explicit && strcmp(packed[i].path,"builtin:demo"))
            snprintf(packed[i].path,sizeof(packed[i].path),"!samples/%02d.wav",i+1);
    }
    if(!tt_song_save(song_tmp,song,packed,count,error,error_size)) {
        unlink(song_tmp);return 0;
    }
    FILE *header=fopen(song_tmp,"rb");
    if(!header || fseek(header,0,SEEK_END)) {
        if(header)fclose(header);
        unlink(song_tmp);snprintf(error,error_size,"Cannot read song header");return 0;
    }
    long header_size=ftell(header);
    if(header_size<=0 || header_size>8L*1024L*1024L) {
        fclose(header);unlink(song_tmp);
        snprintf(error,error_size,"Song header is too large");return 0;
    }
    rewind(header);
    char zip_tmp[PATH_MAX];
    if(snprintf(zip_tmp,sizeof(zip_tmp),"%s.tmpXXXXXX",path)>=(int)sizeof(zip_tmp)) {
        fclose(header);unlink(song_tmp);snprintf(error,error_size,"Package path too long");return 0;
    }
    fd=mkstemp(zip_tmp);
    if(fd<0) {
        fclose(header);unlink(song_tmp);snprintf(error,error_size,"Cannot create package: %s",strerror(errno));return 0;
    }
    close(fd);
    mz_zip_archive zip={0};int ok=0;
    if(!mz_zip_writer_init_file(&zip,zip_tmp,(mz_uint64)header_size)) {
        snprintf(error,error_size,"Cannot start ZIP archive");goto done;
    }
    for(int i=0;i<count;++i) {
        char entry[32];
        if(packed[i].instrument_type!=TT_INSTRUMENT_DX7 &&
           packed[i].path[0]=='!' && !packed[i].path_explicit) {
            snprintf(entry,sizeof(entry),"samples/%02d.wav",i+1);
            /* Keep missing samples in the song so edits can still be saved.
               Recovery can supply the WAV in a later session. */
            if(!waves[i] || waves[i]->pcm) {
                if(!sample_paths[i] ||
                   !mz_zip_writer_add_file(&zip,entry,sample_paths[i],NULL,0,MZ_BEST_SPEED)) {
                    snprintf(error,error_size,"Cannot pack WAV for instrument %02X",i+1);
                    goto done;
                }
            }
        }
        if(waves[i] && waves[i]->syx_size) {
            if(waves[i]->syx_size>TT_SYX_MAX_BYTES) {
                snprintf(error,error_size,"Invalid SYX bank for instrument %02X",i+1);
                goto done;
            }
            snprintf(entry,sizeof(entry),"banks/%02d.syx",i+1);
            if(!mz_zip_writer_add_mem(&zip,entry,waves[i]->syx_bank,
                                      waves[i]->syx_size,MZ_BEST_SPEED)) {
                snprintf(error,error_size,"Cannot pack SYX bank for instrument %02X",i+1);
                goto done;
            }
        }
    }
    if(!mz_zip_writer_finalize_archive(&zip)) {
        snprintf(error,error_size,"Cannot finish ZIP archive");goto done;
    }
    if(!mz_zip_writer_end(&zip)) {
        snprintf(error,error_size,"Cannot close ZIP archive");goto done;
    }
    FILE *packed_file=fopen(zip_tmp,"r+b");
    if(!packed_file) {
        snprintf(error,error_size,"Cannot write song header");goto done;
    }
    char buffer[4096];long remaining=header_size;
    while(remaining>0) {
        size_t amount=remaining<(long)sizeof(buffer)?(size_t)remaining:sizeof(buffer);
        if(fread(buffer,1,amount,header)!=amount ||
           fwrite(buffer,1,amount,packed_file)!=amount)break;
        remaining-=(long)amount;
    }
    if(fclose(packed_file) || remaining) {
        snprintf(error,error_size,"Cannot finish song header");goto done;
    }
    if(rename(zip_tmp,path)) {
        snprintf(error,error_size,"Cannot replace package: %s",strerror(errno));goto done;
    }
    ok=1;
done:
    if(!ok)mz_zip_writer_end(&zip);
    fclose(header);
    unlink(song_tmp);
    if(!ok)unlink(zip_tmp);
    return ok;
}
int tt_package_unpack(const char *path,char *song_path,size_t song_path_size,
                      TtPackageBank banks[TT_SONG_SAMPLES],
                      char *error,size_t error_size) {
    memset(banks,0,TT_SONG_SAMPLES*sizeof(*banks));
    mz_zip_archive zip={0};
    if(!mz_zip_reader_init_file(&zip,path,0)) {
        snprintf(error,error_size,"Cannot open packed song ZIP");return 0;
    }
    FILE *source=fopen(path,"rb");
    char first[64]={0};
    int hybrid=source && fgets(first,sizeof(first),source) &&
               !strncmp(first,"F80_SAMPLE_SONG\t",16);
    if(source)fclose(source);
    mz_uint song_index=0;
    int legacy=0,exists=1;
    if(!hybrid) {
        exists=zip_entry(&zip,"song.ttt",8u*1024u*1024u,&song_index,error,error_size);
        if(!exists) {
            legacy=1;
            exists=zip_entry(&zip,"song.song",8u*1024u*1024u,&song_index,error,error_size);
        }
        if(exists<=0) {
            if(!exists)snprintf(error,error_size,"Package has no song.ttt");
            mz_zip_reader_end(&zip);return 0;
        }
    }
    char dir[]="/tmp/tts-package-XXXXXX";
    int dir_fd=mkstemp(dir);
    if(dir_fd<0) {
        snprintf(error,error_size,"Cannot create package workspace");
        mz_zip_reader_end(&zip);return 0;
    }
    close(dir_fd);unlink(dir);
    if(mkdir(dir,0700)) {
        snprintf(error,error_size,"Cannot create package workspace");
        mz_zip_reader_end(&zip);return 0;
    }
    char samples_dir[PATH_MAX];
    int ok=0;
    if(snprintf(samples_dir,sizeof(samples_dir),"%s/samples",dir)>=(int)sizeof(samples_dir) ||
       mkdir(samples_dir,0700)) {
        snprintf(error,error_size,"Cannot create sample workspace");goto done;
    }
    if(snprintf(song_path,song_path_size,"%s/%s",dir,legacy?"song.song":"song.ttt")>=(int)song_path_size) {
        snprintf(error,error_size,"Song path too long");goto done;
    }
    if(hybrid) {
        source=fopen(path,"rb");
        FILE *target=fopen(song_path,"wb");
        char line[4096];size_t total=0;int ended=0;
        if(source && target)while(fgets(line,sizeof(line),source)) {
            size_t length=strlen(line);
            total+=length;
            if(total>8u*1024u*1024u || !length || line[length-1]!='\n' ||
               fwrite(line,1,length,target)!=length)break;
            if(!strcmp(line,"END\n") || !strcmp(line,"END\r\n")) {
                ended=1;break;
            }
        }
        if(source)fclose(source);
        if(target && fclose(target))ended=0;
        if(!ended) { snprintf(error,error_size,"Invalid song header");goto done; }
    } else if(!mz_zip_reader_extract_to_file(&zip,song_index,song_path,0)) {
        snprintf(error,error_size,"Cannot unpack song data");goto done;
    }
    for(int i=0;i<TT_SONG_SAMPLES;++i) {
        char name[32],destination[PATH_MAX];mz_uint index;
        snprintf(name,sizeof(name),"samples/%02d.wav",i+1);
        exists=zip_entry(&zip,name,64u*1024u*1024u,&index,error,error_size);
        if(exists<0)goto done;
        if(exists) {
            if(snprintf(destination,sizeof(destination),"%s/%s",dir,name)>=(int)sizeof(destination) ||
               !mz_zip_reader_extract_to_file(&zip,index,destination,0)) {
                snprintf(error,error_size,"Cannot unpack %s",name);goto done;
            }
        }
        snprintf(name,sizeof(name),"banks/%02d.syx",i+1);
        exists=zip_entry(&zip,name,TT_SYX_MAX_BYTES,&index,error,error_size);
        if(exists<0)goto done;
        if(exists) {
            mz_zip_archive_file_stat stat;
            if(!mz_zip_reader_file_stat(&zip,index,&stat) || !stat.m_uncomp_size ||
               !mz_zip_reader_extract_to_mem(&zip,index,banks[i].data,
                                             (size_t)stat.m_uncomp_size,0)) {
                snprintf(error,error_size,"Cannot unpack %s",name);goto done;
            }
            banks[i].size=(size_t)stat.m_uncomp_size;
        }
    }
    PackageDir *entry=malloc(sizeof(*entry));
    if(!entry) { snprintf(error,error_size,"Out of memory tracking package");goto done; }
    snprintf(entry->path,sizeof(entry->path),"%s",dir);
    entry->next=package_dirs;package_dirs=entry;
    ok=1;
done:
    mz_zip_reader_end(&zip);
    if(!ok)remove_package_dir(dir);
    return ok;
}
