#include "pattern.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int hex(char digit) {
    if(digit>='0' && digit<='9')return digit-'0';
    digit=(char)toupper((unsigned char)digit);
    return digit>='A' && digit<='F'?digit-'A'+10:-1;
}
static const char *effect_alias(int effect) {
    switch(effect) {
    case 'G':return "GLD";case 'I':return "IIN";
    case 'J':return "JDN";case 'K':return "KUP";
    case 'M':return "MVL";case 'N':return "NPN";
    case 'O':return "OUT";case 'P':return "PAN";
    case 'R':return "RRR";case 'r':return "RTG";case 'S':return "SLC";
    case 'T':return "TBL";case 'V':return "VIB";
    case 'W':return "WBL";default:return NULL;
    }
}
void sample_cell_text(const SampleCell *cell,char text[11]) {
    static const char digits[]="0123456789ABCDEF";
    memcpy(text,"..........",11);
    if(cell->note>=0) {
        char note[4];sample_note_name(cell->note,note);
        memcpy(text,note,3);
    }
    if(cell->instrument>=0 && cell->instrument<=255) {
        text[3]=digits[cell->instrument>>4];
        text[4]=digits[cell->instrument&15];
    }
    if(cell->effect!=SAMPLE_EFFECT_NONE) {
        const char *alias=effect_alias(cell->effect);
        if(!alias)alias="...";
        memcpy(text+5,alias,3);
        if(cell->value>=0 && cell->value<=255) {
            text[8]=digits[cell->value>>4];text[9]=digits[cell->value&15];
        }
    }
    text[10]=0;
}
static int parse_note(const char *text) {
    if(!memcmp(text,"...",3))return -1;
    for(int note=0;note<=127;++note) {
        char name[4];sample_note_name(note,name);
        if(!memcmp(text,name,3))return note;
    }
    return -2;
}
static int parse_cell(const char *text,SampleCell *cell) {
    cell->note=parse_note(text);
    if(cell->note == -2)return 0;
    if(text[3]=='.' && text[4]=='.')cell->instrument=-1;
    else {
        int high=hex(text[3]),low=hex(text[4]);
        if(high<0 || low<0)return 0;
        cell->instrument=high*16+low;
    }
    if(!memcmp(text+5,".....",5)) {
        cell->effect=SAMPLE_EFFECT_NONE;cell->value=-1;
    }
    else {
        if(!memcmp(text+5,"SLI",3))cell->effect=SAMPLE_EFFECT_S;
        else if(!memcmp(text+5,"VOL",3))cell->effect=SAMPLE_EFFECT_M;
        else if(!memcmp(text+5,"RTG",3))cell->effect=SAMPLE_EFFECT_LEGACY_RETRIGGER;
        else if(!memcmp(text+5,"~ST",3))cell->effect=SAMPLE_EFFECT_STRETCH;
        else {
            cell->effect=SAMPLE_EFFECT_NONE;
            for(const char *commands="GIJKMNOPRSTVW";*commands;++commands) {
                const char *alias=effect_alias(*commands);
                if(!memcmp(text+5,alias,3)) { cell->effect=*commands;break; }
            }
            if(cell->effect==SAMPLE_EFFECT_NONE)return 0;
        }
        if(text[8]=='.' && text[9]=='.')cell->value=-1;
        else {
            int high=hex(text[8]),low=hex(text[9]);
            if(high<0 || low<0)return 0;
            cell->value=high*16+low;
        }
    }
    return 1;
}
void sample_pattern_row_text(const SamplePattern *pattern,int row,char text[67]) {
    for(int channel=0;channel<SAMPLE_CHANNELS;++channel) {
        if(channel)text[channel*11-1]='|';
        sample_cell_text(&pattern->cell[row][channel],text+channel*11);
    }
    text[65]=0;
}
int sample_pattern_parse_row(const char *text,SamplePattern *pattern,int row) {
    if(row<0 || row>=SAMPLE_MAX_STEPS || strlen(text)!=65)return 0;
    for(int channel=0;channel<SAMPLE_CHANNELS;++channel) {
        int at=channel*11;
        if(channel && text[at-1]!='|')return 0;
        if(!parse_cell(text+at,&pattern->cell[row][channel]))return 0;
    }
    return 1;
}
int sample_pattern_save(const char *path,const SamplePattern *pattern,
                        char *error,size_t error_size) {
    FILE *file=fopen(path,"w");
    if(!file) { snprintf(error,error_size,"Cannot save pattern: %s",strerror(errno));return 0; }
    for(int row=0;row<SAMPLE_STEPS;++row) {
        for(int channel=0;channel<SAMPLE_CHANNELS;++channel) {
            char text[11];sample_cell_text(&pattern->cell[row][channel],text);
            if(channel)fputc('|',file);
            fputs(text,file);
        }
        fputc('\n',file);
    }
    if(fclose(file)!=0) {
        snprintf(error,error_size,"Could not finish pattern: %s",strerror(errno));return 0;
    }
    return 1;
}
int sample_pattern_load(const char *path,SamplePattern *pattern,
                        char *error,size_t error_size) {
    FILE *file=fopen(path,"r");
    if(!file) { snprintf(error,error_size,"Cannot open pattern: %s",strerror(errno));return 0; }
    SamplePattern next;
    sample_pattern_init(&next);
    char line[128];int row=0,valid=1;
    while(fgets(line,sizeof(line),file)) {
        if(row>=SAMPLE_STEPS) { valid=0;break; }
        size_t length=strcspn(line,"\r\n");
        if(length!=SAMPLE_CHANNELS*10+SAMPLE_CHANNELS-1) { valid=0;break; }
        for(int channel=0;channel<SAMPLE_CHANNELS;++channel) {
            int at=channel*11;
            if(channel && line[at-1]!='|') { valid=0;break; }
            if(!parse_cell(line+at,&next.cell[row][channel])) { valid=0;break; }
        }
        if(!valid)break;
        ++row;
    }
    if(ferror(file))valid=0;
    fclose(file);
    if(!valid || row!=SAMPLE_STEPS) {
        snprintf(error,error_size,"Invalid pattern near row %02X",row);return 0;
    }
    *pattern=next;
    return 1;
}
