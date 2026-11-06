#define _POSIX_C_SOURCE 200809L
#include "csv.h"
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

    SampleWave wave={0};
    assert(sample_make_demo(&wave));
    snprintf(wave.name,sizeof(wave.name),"kick,\"dry\".wav");
    snprintf(wave.source_name,sizeof(wave.source_name),"%s",wave.name);
    SamplePhrase original,loaded;
    sample_phrase_init(&original,wave.frames);
    sample_move_marker(&original,wave.frames,4,12345);
    original.reference_note=62;
    original.bpm=137;
    sample_slice_set_note(&original,2,65);
    assert(sample_csv_save(path,&wave,&original,error,sizeof(error)));

    sample_phrase_init(&loaded,wave.frames);
    assert(sample_csv_load(path,&wave,&loaded,error,sizeof(error)));
    assert(!memcmp(&original,&loaded,sizeof(original)));
    assert(sample_slice_base_note(&loaded,2)==65);
    sample_phrase_set_reference_note(&loaded,64);
    assert(sample_slice_base_note(&loaded,2)==67);
    assert(sample_csv_save(path,&wave,&loaded,error,sizeof(error)));
    SamplePhrase transposed;
    sample_phrase_init(&transposed,wave.frames);
    assert(sample_csv_load(path,&wave,&transposed,error,sizeof(error)));
    assert(!memcmp(&loaded,&transposed,sizeof(loaded)));
    snprintf(wave.name,sizeof(wave.name),"renamed sample");
    assert(sample_csv_load(path,&wave,&loaded,error,sizeof(error)));
    assert(!strcmp(wave.name,"kick,\"dry\".wav"));

    snprintf(wave.source_name,sizeof(wave.source_name),"other.wav");
    loaded.bpm=41;
    assert(!sample_csv_load(path,&wave,&loaded,error,sizeof(error)));
    assert(loaded.bpm==41);
    assert(strstr(error,"kick,\"dry\".wav"));

    FILE *file=fopen(path,"w");
    assert(file);
    fputs("record,index,value,start,end\nmarker,0,999999999,,\n",file);
    assert(!fclose(file));
    assert(!sample_csv_load(path,&wave,&loaded,error,sizeof(error)));
    assert(loaded.bpm==41);

    unlink(path);
    sample_free(&wave);
    puts("slice CSV round-trip and validation passed");
    return 0;
}
