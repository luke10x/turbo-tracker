#define _POSIX_C_SOURCE 200809L
#include "pattern.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[]="/tmp/f80-pattern-XXXXXX",error[192];
    int descriptor=mkstemp(path);
    assert(descriptor>=0);close(descriptor);
    SamplePattern original,loaded;
    sample_pattern_init(&original);
    original.cell[0][1]=(SampleCell){60,1,SAMPLE_EFFECT_M,0xff};
    original.cell[8][5]=(SampleCell){72,0,SAMPLE_EFFECT_S,0x4f};
    original.cell[9][2]=(SampleCell){-1,-1,SAMPLE_EFFECT_M,0x80};
    original.cell[10][3]=(SampleCell){60,1,SAMPLE_EFFECT_S,0xff};
    original.cell[11][3]=(SampleCell){-1,-1,SAMPLE_EFFECT_S,-1};
    const char *commands="GIJKMNOPRSTVW";
    for(int row=0;commands[row];++row)
        original.cell[row+1][4]=(SampleCell){-1,-1,commands[row],0x4f};
    original.cell[14][4]=(SampleCell){60,1,SAMPLE_EFFECT_STRETCH,0x40};
    char blank_value[11];
    sample_cell_text(&original.cell[11][3],blank_value);
    assert(!strcmp(blank_value,".....SLC.."));
    assert(sample_pattern_save(path,&original,error,sizeof(error)));
    FILE *file=fopen(path,"r");assert(file);
    char line[128];assert(fgets(line,sizeof(line),file));
    assert(!strncmp(line,"C-401SLC04|C-401MVLFF|..........",32));
    fclose(file);
    memset(&loaded,0,sizeof(loaded));
    assert(sample_pattern_load(path,&loaded,error,sizeof(error)));
    assert(!memcmp(&original,&loaded,sizeof(original)));
    char legacy_row[67];
    sample_pattern_row_text(&original,0,legacy_row);
    memcpy(legacy_row+5,"SLI",3);
    memcpy(legacy_row+16,"VOL",3);
    SamplePattern legacy;sample_pattern_init(&legacy);
    assert(sample_pattern_parse_row(legacy_row,&legacy,0));
    assert(!memcmp(&legacy.cell[0],&original.cell[0],sizeof(original.cell[0])));
    file=fopen(path,"w");assert(file);
    for(int row=0;row<SAMPLE_STEPS;++row) {
        char row_text[67];
        sample_pattern_row_text(&original,row,row_text);
        if(row==0)memcpy(row_text,legacy_row,65);
        assert(fputs(row_text,file)>=0 && fputc('\n',file)!=EOF);
    }
    assert(!fclose(file));
    assert(sample_pattern_load(path,&loaded,error,sizeof(error)));
    assert(!memcmp(&original,&loaded,sizeof(original)));
    SamplePhrase phrase;sample_phrase_init(&phrase,1600);
    int start,end;
    assert(sample_cell_range(&phrase,&loaded.cell[0][1],1600,&start,&end));
    assert(start==0 && end==1600);
    assert(sample_cell_range(&phrase,&loaded.cell[8][5],1600,&start,&end));
    assert(start==phrase.marker[4] && end==phrase.marker[15]);
    assert(!sample_cell_range(&phrase,&loaded.cell[10][3],1600,&start,&end));
    file=fopen(path,"w");assert(file);
    fputs("C-401SLC04|bad\n",file);assert(!fclose(file));
    assert(!sample_pattern_load(path,&loaded,error,sizeof(error)));
    assert(!memcmp(&original,&loaded,sizeof(original)));
    unlink(path);
    puts("six-channel pattern round-trip and validation passed");
    return 0;
}
