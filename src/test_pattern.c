#define _POSIX_C_SOURCE 200809L
#include "tt_pattern.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[]="/tmp/f80-pattern-XXXXXX",error[192];
    int descriptor=mkstemp(path);
    assert(descriptor>=0);close(descriptor);
    TtPattern original,loaded;
    tt_pattern_init(&original);
    original.cell[0][1]=(TtCell){60,1,TT_EFFECT_M,0xff,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    original.cell[8][5]=(TtCell){72,0,TT_EFFECT_S,0x4f,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    original.cell[9][2]=(TtCell){-1,-1,TT_EFFECT_M,0x80,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    original.cell[10][3]=(TtCell){60,1,TT_EFFECT_S,0xff,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    original.cell[11][3]=(TtCell){-1,-1,TT_EFFECT_S,-1,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    original.cell[12][3]=(TtCell){-1,-1,'0',0x30,TT_EFFECT_M,
                                  tt_effect_register_value(0),TT_EFFECT_NONE,-1};
    const char *commands="GIJKMNOPRSTUVWXY";
    for(int row=0;commands[row] && row+1<TT_STEPS;++row)
        original.cell[row+1][4]=(TtCell){-1,-1,commands[row],0x4f,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    original.cell[14][4]=(TtCell){60,1,TT_EFFECT_STRETCH,0x40,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    original.cell[14][5]=(TtCell){-1,-1,TT_EFFECT_Z,0x30,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    original.cell[15][0]=(TtCell){TT_NOTE_OFF,-1,TT_EFFECT_X,0x1f,
                                    TT_EFFECT_Y,0x0f,TT_EFFECT_Z,0x30};
    original.cell[13][5].note=tt_note_relative(1,4);
    char blank_value[11];
    tt_cell_text(&original.cell[11][3],blank_value);
    assert(!strcmp(blank_value,".....SLC.."));
    char reverse_alias[6];
    tt_pattern_effect_text(TT_EFFECT_R,0x40,reverse_alias);
    assert(!strcmp(reverse_alias,"RRR40"));
    char jump_alias[6];
    tt_pattern_effect_text(TT_EFFECT_X,0xa5,jump_alias);
    assert(!strcmp(jump_alias,"XJPA5"));
    char delay_alias[6];
    tt_pattern_effect_text(TT_EFFECT_U,0x80,delay_alias);
    assert(!strcmp(delay_alias,"UDL80"));
    char register_alias[6];
    tt_pattern_effect_text('F',0xff,register_alias);
    assert(!strcmp(register_alias,"FRGFF"));
    char register_value[6];
    tt_pattern_effect_text(TT_EFFECT_M,tt_effect_register_value(0),register_value);
    assert(!strcmp(register_value,"MVL$0"));
    char register_cell[11];tt_cell_text(&original.cell[12][3],register_cell);
    assert(!strcmp(register_cell,".....0RG30"));
    char off_cell[11];
    tt_cell_text(&original.cell[15][0],off_cell);
    assert(!strcmp(off_cell,"OFF..XJP1F"));
    char relative_cell[11];tt_cell_text(&original.cell[13][5],relative_cell);
    assert(!strcmp(relative_cell,"+=4......."));
    assert(tt_pattern_save(path,&original,error,sizeof(error)));
    FILE *file=fopen(path,"r");assert(file);
    char line[128];assert(fgets(line,sizeof(line),file));
    assert(strstr(line,"|C-401MVLFF.....")!=NULL);
    fclose(file);
    memset(&loaded,0,sizeof(loaded));
    assert(tt_pattern_load(path,&loaded,error,sizeof(error)));
    assert(!memcmp(&original,&loaded,sizeof(original)));
    char register_row[126];tt_pattern_row_text(&original,12,register_row);
    assert(strstr(register_row,".....0RG30MVL$0")!=NULL);
    TtPattern duplicate_table;tt_pattern_init(&duplicate_table);
    duplicate_table.cell[0][0]=(TtCell){-1,-1,TT_EFFECT_T,0,
                                        TT_EFFECT_T,1,TT_EFFECT_NONE,-1};
    char duplicate_row[126];
    tt_pattern_row_text(&duplicate_table,0,duplicate_row);
    TtPattern rejected;tt_pattern_init(&rejected);
    assert(!tt_pattern_parse_row(duplicate_row,&rejected,0));
    char legacy_row[66];
    for(int channel=0;channel<TT_CHANNELS;++channel) {
        if(channel)legacy_row[channel*11-1]='|';
        tt_cell_text(&original.cell[0][channel],legacy_row+channel*11);
    }
    legacy_row[65]=0;
    memcpy(legacy_row+5,"SLI",3);
    memcpy(legacy_row+16,"VOL",3);
    TtPattern legacy;tt_pattern_init(&legacy);
    assert(tt_pattern_parse_row(legacy_row,&legacy,0));
    assert(!memcmp(&legacy.cell[0],&original.cell[0],sizeof(original.cell[0])));
    file=fopen(path,"w");assert(file);
    for(int row=0;row<TT_STEPS;++row) {
        char row_text[126];
        tt_pattern_row_text(&original,row,row_text);
        assert(fputs(row_text,file)>=0 && fputc('\n',file)!=EOF);
    }
    assert(!fclose(file));
    assert(tt_pattern_load(path,&loaded,error,sizeof(error)));
    assert(!memcmp(&original,&loaded,sizeof(original)));
    file=fopen(path,"w");assert(file);
    for(int row=0;row<TT_STEPS;++row) {
        for(int channel=0;channel<TT_CHANNELS;++channel) {
            if(channel)fputc('|',file);
            char cell[11];tt_cell_text(&original.cell[row][channel],cell);fputs(cell,file);
        }
        fputc('\n',file);
    }
    assert(!fclose(file));
    assert(tt_pattern_load(path,&loaded,error,sizeof(error)));
    for(int row=0;row<TT_STEPS;++row)for(int channel=0;channel<TT_CHANNELS;++channel) {
        assert(loaded.cell[row][channel].effect2==TT_EFFECT_NONE);
        assert(loaded.cell[row][channel].effect3==TT_EFFECT_NONE);
    }
    TtPhrase phrase;tt_phrase_init(&phrase,1600);
    int start,end;
    assert(tt_cell_range(&phrase,&loaded.cell[0][1],1600,&start,&end));
    assert(start==0 && end==1600);
    assert(tt_cell_range(&phrase,&loaded.cell[8][5],1600,&start,&end));
    assert(start==phrase.marker[4] && end==phrase.marker[15]);
    assert(!tt_cell_range(&phrase,&loaded.cell[10][3],1600,&start,&end));
    TtPattern before_invalid=loaded;
    file=fopen(path,"w");assert(file);
    fputs("C-401SLC04|bad\n",file);assert(!fclose(file));
    assert(!tt_pattern_load(path,&loaded,error,sizeof(error)));
    assert(!memcmp(&before_invalid,&loaded,sizeof(before_invalid)));
    unlink(path);
    puts("six-channel pattern round-trip and validation passed");
    return 0;
}
