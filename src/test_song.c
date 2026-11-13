#define _POSIX_C_SOURCE 200809L
#include "tt_song.h"
#include "tt_package.h"
#include "../third_party/miniz/miniz.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void strip_order_activity(char *record) {
    if(!strncmp(record,"AT\t",3)) {
        char *last=strrchr(record,'\t');assert(last);*last='\n';last[1]=0;
    }
}

int main(void) {
    char path[]="/tmp/f80-song-XXXXXX",error[192];
    int fd=mkstemp(path);assert(fd>=0);close(fd);
    TtSong song,loaded;tt_song_init(&song);
    song.bpm=143;song.pattern_count=2;
    snprintf(song.name[0],TT_SONG_NAME,"Break A");
    snprintf(song.name[1],TT_SONG_NAME,"Fill %% B");
    tt_pattern_init(&song.pattern[1]);
    for(int table=0;table<TT_SONG_TABLES;++table)
        song.table[table].length=TT_TABLE_MAX_TICKS;
    song.pattern[1].cell[3][2]=(TtCell){64,1,TT_EFFECT_M,128,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    song.pattern[1].cell[4][2]=(TtCell){-1,-1,TT_EFFECT_T,0,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    song.pattern[1].cell[5][2]=(TtCell){67,1,TT_EFFECT_V,0x4f,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    song.pattern[1].cell[6][2]=(TtCell){-1,-1,TT_EFFECT_P,0x80,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    song.pattern[1].cell[7][2]=(TtCell){72,1,TT_EFFECT_STRETCH,0x40,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    song.pattern[1].cell[8][2]=(TtCell){-1,-1,TT_EFFECT_X,1,TT_EFFECT_Y,0x0a,TT_EFFECT_Z,0x0c};
    song.pattern[1].cell[10][2]=(TtCell){-1,-1,'0',0x40,TT_EFFECT_M,
                                          tt_effect_register_value(0),TT_EFFECT_NONE,-1};
    song.pattern[1].cell[9][2].note=TT_NOTE_OFF;
    song.pattern[1].cell[3][2].effect2=TT_EFFECT_J;
    song.pattern[1].cell[3][2].value2=0x12;
    song.pattern[1].cell[3][2].effect3=TT_EFFECT_N;
    song.pattern[1].cell[3][2].value3=0x34;
    song.table[0].length=TT_TABLE_MAX_TICKS;
    snprintf(song.table[0].name,TT_SONG_NAME,"Chop %% Table");
    song.table[0].tick[0].note=60;
    song.table[0].tick[0].instrument=1;
    song.table[0].tick[6].note=tt_note_relative(1,7);
    song.table[0].tick[6].instrument=1;
    song.table[0].tick[0].effect[0]=(TtTableEffect){'M',0x33};
    song.table[0].tick[0].effect[1]=(TtTableEffect){'S',0xaf};
    song.table[0].tick[0].effect[4]=(TtTableEffect){'U',0x05};
    song.table[0].tick[TT_TABLE_MAX_TICKS-1].effect[5]=(TtTableEffect){'X',0x01};
    song.table[0].tick[TT_TABLE_MAX_TICKS-1].note=TT_NOTE_OFF;
    song.table[0].tick[TT_TABLE_MAX_TICKS-1].effect[4]=(TtTableEffect){'Y',0xff};
    song.table[0].tick[2].effect[0]=(TtTableEffect){'S',-1};
    song.table[0].tick[3].effect[0]=(TtTableEffect){'W',0x4f};
    song.table[0].tick[3].effect[1]=(TtTableEffect){'N',0x38};
    song.table[0].tick[3].effect[2]=(TtTableEffect){'R',0xc0};
    song.table[0].tick[5].effect[0]=(TtTableEffect){'F',0xff};
    song.table[0].tick[5].effect[1]=(TtTableEffect){'M',tt_effect_register_value(15)};
    song.table[0].tick[4].effect[0]=(TtTableEffect){'r',0x02};
    song.order_count=3;song.order[0]=0;song.order[1]=1;song.order[2]=0;
    song.order_active[0]=1;song.order_active[1]=0;song.order_active[2]=1;
    TtSongSample sample[8]={0},readback[8]={0};
    snprintf(sample[0].name,sizeof(sample[0].name),"Kick & Snare");
    snprintf(sample[0].source_name,sizeof(sample[0].source_name),"break.wav");
    snprintf(sample[0].path,sizeof(sample[0].path),"/tmp/samples/break.wav");
    sample[0].frames=44000;sample[0].rate=22000;
    tt_phrase_init(&sample[0].phrase,44000);
    tt_slice_set_note(&sample[0].phrase,2,65);
    assert(tt_song_save(path,&song,sample,1,error,sizeof(error)));
    int count=0;assert(tt_song_load(path,&loaded,readback,&count,error,sizeof(error)));
    assert(count==1 && loaded.bpm==143 && loaded.pattern_count==2);
    assert(loaded.pattern_rows==16 && loaded.rows_per_beat==4 &&
           loaded.ticks_per_row==12);
    assert(loaded.order_count==3 && loaded.order[2]==0);
    assert(loaded.order_active[0] && !loaded.order_active[1] && loaded.order_active[2]);
    assert(!strcmp(loaded.name[1],"Fill % B"));
    assert(!memcmp(&loaded.pattern[1],&song.pattern[1],sizeof(TtPattern)));
    assert(!memcmp(&loaded.table,&song.table,sizeof(song.table)));
    assert(!strcmp(loaded.table[0].name,"Chop % Table"));
    assert(readback[0].phrase.slice_note_offset[2]==5);
    assert(!strcmp(readback[0].path,"samples/break.wav"));
    char legacy_path[]="/tmp/f80-legacy-song-XXXXXX";
    int legacy_fd=mkstemp(legacy_path);assert(legacy_fd>=0);
    FILE *modern=fopen(path,"r"),*legacy=fdopen(legacy_fd,"w");
    assert(modern && legacy);
    char record[4096];assert(fgets(record,sizeof(record),modern));
    assert(fputs("F80_SAMPLE_SONG\t1\n",legacy)>=0);
    while(fgets(record,sizeof(record),modern)) {
        if(!strncmp(record,"INSTRUMENT\t",11))continue;
        if(!strncmp(record,"TABLES\t",7))break;
        if(!strncmp(record,"ROWS\t",5) || !strncmp(record,"BEAT_ROWS\t",10) ||
           !strncmp(record,"TICKS\t",6))continue;
        strip_order_activity(record);
        assert(fputs(record,legacy)>=0);
    }
    assert(fputs("END\n",legacy)>=0);
    assert(!fclose(modern) && !fclose(legacy));
    assert(tt_song_load(legacy_path,&loaded,readback,&count,error,sizeof(error)));
    assert(loaded.table[0].length==TT_TABLE_DEFAULT_TICKS);
    assert(loaded.table[0].tick[0].note==-1);
    unlink(legacy_path);
    char v2_path[]="/tmp/f80-v2-song-XXXXXX";
    int v2_fd=mkstemp(v2_path);assert(v2_fd>=0);
    modern=fopen(path,"r");FILE *v2=fdopen(v2_fd,"w");
    assert(modern && v2);
    assert(fgets(record,sizeof(record),modern));
    assert(fputs("F80_SAMPLE_SONG\t2\n",v2)>=0);
    while(fgets(record,sizeof(record),modern)) {
        if(!strncmp(record,"INSTRUMENT\t",11))continue;
        if(!strncmp(record,"ROWS\t",5) || !strncmp(record,"BEAT_ROWS\t",10) ||
           !strncmp(record,"TICKS\t",6))continue;
        char *fx=strstr(record,"\tRC0");if(fx)fx[1]='~';
        fx=strstr(record,"\tr02");if(fx)fx[1]='R';
        if(!strncmp(record,"TABLE\t",6)) {
            char *name=strrchr(record,'\t');assert(name);*name='\n';name[1]=0;
        }
        strip_order_activity(record);
        assert(fputs(record,v2)>=0);
    }
    assert(!fclose(modern) && !fclose(v2));
    assert(tt_song_load(v2_path,&loaded,readback,&count,error,sizeof(error)));
    assert(!strcmp(loaded.table[0].name,"Table 00"));
    assert(loaded.table[0].length==TT_TABLE_MAX_TICKS &&
           loaded.table[0].tick[0].effect[1].value==0xaf);
    unlink(v2_path);
    char v4_path[]="/tmp/f80-v4-song-XXXXXX";
    int v4_fd=mkstemp(v4_path);assert(v4_fd>=0);
    modern=fopen(path,"r");FILE *v4=fdopen(v4_fd,"w");
    assert(modern && v4);
    assert(fgets(record,sizeof(record),modern));
    assert(fputs("F80_SAMPLE_SONG\t4\n",v4)>=0);
    while(fgets(record,sizeof(record),modern)) {
        if(!strncmp(record,"INSTRUMENT\t",11))continue;
        char *fx=strstr(record,"\tRC0");if(fx)fx[1]='~';
        fx=strstr(record,"\tr02");if(fx)fx[1]='R';
        strip_order_activity(record);
        assert(fputs(record,v4)>=0);
    }
    assert(!fclose(modern) && !fclose(v4));
    assert(tt_song_load(v4_path,&loaded,readback,&count,error,sizeof(error)));
    assert(loaded.table[0].tick[3].effect[2].command=='R');
    assert(loaded.table[0].tick[4].effect[0].command=='r');
    unlink(v4_path);
    song.pattern_rows=32;song.rows_per_beat=3;song.ticks_per_row=6;
    song.pattern[1].cell[23][4]=(TtCell){72,1,TT_EFFECT_NONE,-1,
                                           TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    assert(tt_song_save(path,&song,sample,1,error,sizeof(error)));
    assert(tt_song_load(path,&loaded,readback,&count,error,sizeof(error)));
    assert(loaded.pattern_rows==32 && loaded.rows_per_beat==3 &&
           loaded.ticks_per_row==6);
    assert(loaded.pattern[1].cell[23][4].note==72);
    char resolved[512];
    assert(tt_song_resolve_path("/tmp/project/song.song",readback[0].path,
                                    resolved,sizeof(resolved)));
    assert(!strcmp(resolved,"/tmp/project/samples/break.wav"));
    assert(tt_song_relative_path("/tmp/project/song.ttt",
                                 "/tmp/project/shared.wav",resolved,sizeof(resolved)));
    assert(!strcmp(resolved,"./shared.wav"));
    assert(tt_song_relative_path("/tmp/project/song.ttt",
                                 "/tmp/wavs/shared.wav",resolved,sizeof(resolved)));
    assert(!strcmp(resolved,"../wavs/shared.wav"));
    assert(tt_song_relative_path("/tmp/new/song.ttt",
                                 "/tmp/project/songs/../missing.wav",
                                 resolved,sizeof(resolved)));
    assert(!strcmp(resolved,"../project/missing.wav"));
    char root[]="/tmp/f80-song-path-XXXXXX";
    int root_fd=mkstemp(root);assert(root_fd>=0);close(root_fd);unlink(root);
    assert(!mkdir(root,0700));
    char song_dir[512],nested_dir[512],wav_dir[512],nested_song[512];
    snprintf(song_dir,sizeof(song_dir),"%s/songs",root);
    snprintf(nested_dir,sizeof(nested_dir),"%s/songs/nested",root);
    snprintf(wav_dir,sizeof(wav_dir),"%s/wavs",root);
    snprintf(nested_song,sizeof(nested_song),"%s/song.song",nested_dir);
    assert(!mkdir(song_dir,0700) && !mkdir(nested_dir,0700) && !mkdir(wav_dir,0700));
    snprintf(sample[0].path,sizeof(sample[0].path),"%s/wavs/break.wav",root);
    assert(tt_song_save(nested_song,&song,sample,1,error,sizeof(error)));
    assert(tt_song_load(nested_song,&loaded,readback,&count,error,sizeof(error)));
    assert(!strcmp(readback[0].path,"../../wavs/break.wav"));
    assert(tt_song_resolve_path(nested_song,readback[0].path,resolved,sizeof(resolved)));
    char expected[512];snprintf(expected,sizeof(expected),"%s/../../wavs/break.wav",nested_dir);
    assert(!strcmp(resolved,expected));
    unlink(nested_song);rmdir(nested_dir);rmdir(song_dir);rmdir(wav_dir);rmdir(root);
    FILE *file=fopen(path,"w");assert(file);fputs("bad\n",file);fclose(file);
    assert(!tt_song_load(path,&loaded,readback,&count,error,sizeof(error)));
    assert(loaded.order_count==3);
    TtWave wav={0},fm={0},reopened={0};
    assert(tt_load_wav("samples/sine-c4.wav",&wav,error,sizeof(error)));
    sample[0].frames=wav.frames;sample[0].rate=wav.rate;
    tt_phrase_init(&sample[0].phrase,wav.frames);
    snprintf(sample[0].path,sizeof(sample[0].path),"samples/sine-c4.wav");
    snprintf(sample[0].source_name,sizeof(sample[0].source_name),"sine-c4.wav");
    snprintf(sample[1].name,sizeof(sample[1].name),"FM voice");
    snprintf(sample[1].source_name,sizeof(sample[1].source_name),"bank.syx");
    snprintf(sample[1].path,sizeof(sample[1].path),"builtin:dx7");
    sample[1].instrument_type=TT_INSTRUMENT_DX7;
    tt_dx7_init(&sample[1].dx7);
    tt_phrase_init(&sample[1].phrase,0);
    fm.instrument_type=TT_INSTRUMENT_DX7;
    fm.syx_size=163;
    for(size_t i=0;i<fm.syx_size;++i)fm.syx_bank[i]=(unsigned char)i;
    const char *sources[TT_SONG_SAMPLES]={"samples/sine-c4.wav"};
    const TtWave *waves[TT_SONG_SAMPLES]={&wav,&fm};
    char package_path[256],unpacked[512];
    snprintf(package_path,sizeof(package_path),"%s.ttt",path);
    assert(!tt_package_is_path(package_path));
    assert(tt_package_is_path("older.tts.zip"));
    assert(!tt_package_save(package_path,&song,sample,2,
                            (const char *const *)((const char *[TT_SONG_SAMPLES]){"missing.wav"}),
                            waves,error,sizeof(error)));
    assert(tt_package_save(package_path,&song,sample,2,sources,waves,error,sizeof(error)));
    assert(tt_package_is_path(package_path));
    FILE *packed_file=fopen(package_path,"rb");assert(packed_file);
    char header[64];assert(fgets(header,sizeof(header),packed_file));
    assert(!strcmp(header,"F80_SAMPLE_SONG\t7\n"));
    assert(!fclose(packed_file));
    assert(tt_song_load(package_path,&loaded,readback,&count,error,sizeof(error)));
    assert(loaded.bpm==143 && !strcmp(readback[0].path,"!samples/01.wav"));
    packed_file=fopen(package_path,"r+b");assert(packed_file);
    assert(fgets(header,sizeof(header),packed_file));
    long bpm_at=ftell(packed_file);assert(bpm_at>=0);
    assert(fgets(header,sizeof(header),packed_file));
    assert(!strcmp(header,"BPM\t143\n"));
    assert(fseek(packed_file,bpm_at,SEEK_SET)==0);
    assert(fwrite("BPM\t144\n",1,8,packed_file)==8);
    assert(!fclose(packed_file));
    TtPackageBank banks[TT_SONG_SAMPLES];
    assert(tt_package_unpack(package_path,unpacked,sizeof(unpacked),banks,error,sizeof(error)));
    assert(strstr(unpacked,"/song.ttt"));
    assert(tt_song_load(unpacked,&loaded,readback,&count,error,sizeof(error)));
    assert(loaded.bpm==144); /* text header remains authoritative */
    assert(count==2 && !strcmp(readback[0].path,"!samples/01.wav"));
    assert(readback[1].instrument_type==TT_INSTRUMENT_DX7);
    assert(banks[1].size==fm.syx_size && !memcmp(banks[1].data,fm.syx_bank,fm.syx_size));
    assert(tt_song_resolve_path(unpacked,readback[0].path+1,resolved,sizeof(resolved)));
    assert(tt_load_wav(resolved,&reopened,error,sizeof(error)));
    assert(reopened.frames==wav.frames && reopened.rate==wav.rate);
    char repacked[256],unpacked_again[512];
    snprintf(repacked,sizeof(repacked),"%s-repacked.ttt",path);
    const char *repacked_sources[TT_SONG_SAMPLES]={resolved};
    const TtWave *repacked_waves[TT_SONG_SAMPLES]={&reopened,&fm};
    assert(tt_package_save(repacked,&loaded,readback,count,
                           repacked_sources,repacked_waves,error,sizeof(error)));
    assert(tt_package_unpack(repacked,unpacked_again,sizeof(unpacked_again),
                             banks,error,sizeof(error)));
    assert(tt_song_load(unpacked_again,&loaded,readback,&count,error,sizeof(error)));
    assert(loaded.bpm==144 && !strcmp(readback[0].path,"!samples/01.wav"));
    char archive_entries[8][512],archive_reference[768],archive_wav[512];
    int archive_count=0;
    assert(tt_package_list_wavs(repacked,NULL,0,&archive_count,
                                error,sizeof(error)) && archive_count==1);
    assert(tt_package_list_wavs(repacked,archive_entries,8,&archive_count,
                                error,sizeof(error)));
    assert(archive_count==1 && !strcmp(archive_entries[0],"samples/01.wav"));
    assert(snprintf(archive_reference,sizeof(archive_reference),"%s!%s",
                    repacked,archive_entries[0])<(int)sizeof(archive_reference));
    assert(tt_package_resolve_asset(path,archive_reference,archive_wav,
                                    sizeof(archive_wav),error,sizeof(error)));
    TtWave archive_loaded={0};
    assert(tt_load_wav(archive_wav,&archive_loaded,error,sizeof(error)));
    assert(archive_loaded.frames==wav.frames && archive_loaded.rate==wav.rate);
    tt_free(&archive_loaded);
    assert(snprintf(archive_reference,sizeof(archive_reference),"%s!missing.wav",
                    repacked)<(int)sizeof(archive_reference));
    assert(!tt_package_resolve_asset(path,archive_reference,archive_wav,
                                     sizeof(archive_wav),error,sizeof(error)));
    char many_path[]="/tmp/f80-many-wavs-XXXXXX";
    int many_fd=mkstemp(many_path);assert(many_fd>=0);close(many_fd);
    mz_zip_archive many_zip={0};
    assert(mz_zip_writer_init_file(&many_zip,many_path,0));
    for(int i=0;i<300;++i) {
        char name[32];snprintf(name,sizeof(name),"disc/%03d.wav",i);
        assert(mz_zip_writer_add_mem(&many_zip,name,"W",1,MZ_NO_COMPRESSION));
    }
    assert(mz_zip_writer_finalize_archive(&many_zip));
    assert(mz_zip_writer_end(&many_zip));
    int many_count=0;
    assert(tt_package_list_wavs(many_path,NULL,0,&many_count,error,sizeof(error)) &&
           many_count==300);
    char (*many_entries)[512]=calloc((size_t)many_count,sizeof(*many_entries));
    assert(many_entries);
    assert(tt_package_list_wavs(many_path,many_entries,many_count,&many_count,
                                error,sizeof(error)) && many_count==300);
    assert(!strcmp(many_entries[299],"disc/299.wav"));
    free(many_entries);unlink(many_path);
    TtSongSample demo={0};
    snprintf(demo.name,sizeof(demo.name),"Demo");
    snprintf(demo.source_name,sizeof(demo.source_name),"demo.wav");
    snprintf(demo.path,sizeof(demo.path),"builtin:demo");
    demo.frames=1;demo.rate=22050;tt_phrase_init(&demo.phrase,1);
    const char *empty_sources[TT_SONG_SAMPLES]={0};
    const TtWave *empty_waves[TT_SONG_SAMPLES]={0};
    assert(tt_package_save(repacked,&song,&demo,1,empty_sources,empty_waves,
                           error,sizeof(error)));
    assert(tt_package_unpack(repacked,unpacked_again,sizeof(unpacked_again),
                             banks,error,sizeof(error)));
    assert(tt_song_load(unpacked_again,&loaded,readback,&count,error,sizeof(error)));
    assert(count==1 && !strcmp(readback[0].path,"builtin:demo"));
    TtWave missing={0};
    const TtWave *missing_waves[TT_SONG_SAMPLES]={&missing,&fm};
    assert(tt_package_save(repacked,&song,sample,2,sources,missing_waves,
                           error,sizeof(error)));
    assert(tt_package_unpack(repacked,unpacked_again,sizeof(unpacked_again),
                             banks,error,sizeof(error)));
    assert(tt_song_load(unpacked_again,&loaded,readback,&count,error,sizeof(error)));
    assert(count==2 && !strcmp(readback[0].path,"!samples/01.wav"));
    assert(tt_song_resolve_path(unpacked_again,readback[0].path+1,
                                resolved,sizeof(resolved)));
    assert(access(resolved,F_OK)!=0); /* recovery remains possible */
    char external[512];assert(realpath("samples/sine-c4.wav",external));
    snprintf(sample[0].path,sizeof(sample[0].path),"%s",external);
    sample[0].path_explicit=1;
    assert(tt_package_save(repacked,&song,sample,2,sources,waves,
                           error,sizeof(error)));
    assert(tt_package_unpack(repacked,unpacked_again,sizeof(unpacked_again),
                             banks,error,sizeof(error)));
    assert(tt_song_load(unpacked_again,&loaded,readback,&count,error,sizeof(error)));
    assert(!strcmp(readback[0].path,external));
    assert(tt_song_resolve_path(unpacked_again,"samples/01.wav",
                                resolved,sizeof(resolved)));
    assert(access(resolved,F_OK)!=0); /* external WAV was not embedded */
    assert(tt_song_resolve_path(unpacked_again,readback[0].path,
                                resolved,sizeof(resolved)));
    assert(!strcmp(resolved,external));
    tt_free(&wav);tt_free(&reopened);
    tt_package_cleanup_all();
    unlink(repacked);
    unlink(package_path);
    unlink(path);puts("song and packed ZIP round-trip passed");
    return 0;
}
