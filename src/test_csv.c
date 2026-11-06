#define _POSIX_C_SOURCE 200809L
#include "tt_csv.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[]="/tmp/f80-slices-XXXXXX",error[192];
    int descriptor=mkstemp(path);
    assert(descriptor>=0);
    close(descriptor);

    TtWave wave={0};
    assert(tt_make_demo(&wave));
    snprintf(wave.name,sizeof(wave.name),"kick,\"dry\".wav");
    snprintf(wave.source_name,sizeof(wave.source_name),"%s",wave.name);
    TtPhrase original,loaded;
    tt_phrase_init(&original,wave.frames);
    tt_move_marker(&original,wave.frames,4,12345);
    original.reference_note=62;
    original.bpm=137;
    tt_slice_set_note(&original,2,65);
    assert(tt_csv_save(path,&wave,&original,error,sizeof(error)));

    tt_phrase_init(&loaded,wave.frames);
    assert(tt_csv_load(path,&wave,&loaded,error,sizeof(error)));
    assert(!memcmp(&original,&loaded,sizeof(original)));
    assert(tt_slice_base_note(&loaded,2)==65);
    tt_phrase_set_reference_note(&loaded,64);
    assert(tt_slice_base_note(&loaded,2)==67);
    assert(tt_csv_save(path,&wave,&loaded,error,sizeof(error)));
    TtPhrase transposed;
    tt_phrase_init(&transposed,wave.frames);
    assert(tt_csv_load(path,&wave,&transposed,error,sizeof(error)));
    assert(!memcmp(&loaded,&transposed,sizeof(loaded)));
    snprintf(wave.name,sizeof(wave.name),"renamed sample");
    assert(tt_csv_load(path,&wave,&loaded,error,sizeof(error)));
    assert(!strcmp(wave.name,"kick,\"dry\".wav"));

    snprintf(wave.source_name,sizeof(wave.source_name),"other.wav");
    loaded.bpm=41;
    assert(!tt_csv_load(path,&wave,&loaded,error,sizeof(error)));
    assert(loaded.bpm==41);
    assert(strstr(error,"kick,\"dry\".wav"));

    FILE *file=fopen(path,"w");
    assert(file);
    fputs("record,index,value,start,end\nmarker,0,999999999,,\n",file);
    assert(!fclose(file));
    assert(!tt_csv_load(path,&wave,&loaded,error,sizeof(error)));
    assert(loaded.bpm==41);

    unlink(path);
    tt_free(&wave);
    puts("slice CSV round-trip and validation passed");
    return 0;
}
