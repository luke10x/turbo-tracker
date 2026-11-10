#include "tt_pattern.h"
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
    static const char *const register_alias[TT_EFFECT_REGISTER_COUNT]={
        "0RG","1RG","2RG","3RG","4RG","5RG","6RG","7RG",
        "8RG","9RG","ARG","BRG","CRG","DRG","ERG","FRG"};
    int register_index=tt_effect_register_index(effect);
    if(register_index>=0)return register_alias[register_index];
    switch(effect) {
    case 'G':return "GLD";case 'I':return "IIN";
    case 'J':return "JDN";case 'K':return "KUP";
    case 'M':return "MVL";case 'N':return "NPN";
    case 'O':return "OUT";case 'P':return "PAN";
    case 'R':return "RRR";case 'r':return "RTG";case 'S':return "SLC";
    case 'T':return "TBL";case 'U':return "UDL";case 'V':return "VIB";
    case 'W':return "WBL";case 'X':return "XJP";
    case 'Y':return "YRJ";case 'Z':return "ZTM";
    default:return NULL;
    }
}
void tt_pattern_effect_text(int command,int value,char text[6]) {
    static const char digits[]="0123456789ABCDEF";
    const char *alias=effect_alias(command);
    if(!alias)alias="...";
    memcpy(text,alias,3);
    if(tt_effect_is_register(value) ||
       value==TT_EFFECT_VALUE_REGISTER_PREFIX) {
        char reference[3];tt_effect_value_text(value,reference);
        memcpy(text+3,reference,2);
    } else if(value>=0 && value<=255) {
        text[3]=digits[value>>4];text[4]=digits[value&15];
    } else memcpy(text+3,"..",2);
    text[5]=0;
}
void tt_cell_text(const TtCell *cell,char text[11]) {
    static const char digits[]="0123456789ABCDEF";
    memcpy(text,"..........",11);
    if(cell->note==TT_NOTE_OFF)memcpy(text,"OFF",3);
    else if(tt_note_is_relative(cell->note)) {
        static const char digits[]="0123456789ABCDEF";
        text[0]=tt_note_relative_positive(cell->note)?'+':'-';text[1]='=';
        text[2]=digits[tt_note_relative_amount(cell->note)];
    }
    else if(cell->note>=0) {
        char note[4];tt_note_name(cell->note,note);
        memcpy(text,note,3);
    }
    if(cell->instrument>=0 && cell->instrument<=255) {
        text[3]=digits[cell->instrument>>4];
        text[4]=digits[cell->instrument&15];
    }
    if(cell->effect!=TT_EFFECT_NONE) {
        const char *alias=effect_alias(cell->effect);
        if(!alias)alias="...";
        memcpy(text+5,alias,3);
        if(tt_effect_is_register(cell->value) ||
           cell->value==TT_EFFECT_VALUE_REGISTER_PREFIX) {
            char reference[3];tt_effect_value_text(cell->value,reference);
            memcpy(text+8,reference,2);
        } else if(cell->value>=0 && cell->value<=255) {
            text[8]=digits[cell->value>>4];text[9]=digits[cell->value&15];
        }
    }
    text[10]=0;
}
static int parse_note(const char *text) {
    if(!memcmp(text,"OFF",3))return TT_NOTE_OFF;
    if(!memcmp(text,"...",3))return -1;
    if((text[0]=='+' || text[0]=='-') && text[1]=='=') {
        int amount=hex(text[2]);
        return amount>=0?tt_note_relative(text[0]=='+',amount):-1000;
    }
    for(int note=0;note<=127;++note) {
        char name[4];tt_note_name(note,name);
        if(!memcmp(text,name,3))return note;
    }
    return -1000;
}
static int parse_cell(const char *text,TtCell *cell) {
    cell->note=parse_note(text);
    if(cell->note == -1000)return 0;
    if(text[3]=='.' && text[4]=='.')cell->instrument=-1;
    else {
        int high=hex(text[3]),low=hex(text[4]);
        if(high<0 || low<0)return 0;
        cell->instrument=high*16+low;
    }
    if(!memcmp(text+5,".....",5)) {
        cell->effect=TT_EFFECT_NONE;cell->value=-1;
    }
    else {
        int register_index=-1;
        for(int reg=0;reg<TT_EFFECT_REGISTER_COUNT;++reg) {
            int command=reg<10?'0'+reg:'A'+reg-10;
            if(text[5]==command && text[6]=='R' && text[7]=='G') {
                register_index=reg;break;
            }
        }
        if(register_index>=0)cell->effect=register_index<10?'0'+register_index:
                                                     'A'+register_index-10;
        else if(!memcmp(text+5,"SLI",3))cell->effect=TT_EFFECT_S;
        else if(!memcmp(text+5,"VOL",3))cell->effect=TT_EFFECT_M;
        else if(!memcmp(text+5,"RTG",3))cell->effect=TT_EFFECT_LEGACY_RETRIGGER;
        else if(!memcmp(text+5,"~ST",3))cell->effect=TT_EFFECT_STRETCH;
        else {
            cell->effect=TT_EFFECT_NONE;
            for(const char *commands="GIJKMNOPRSTUVWXYZ";*commands;++commands) {
                const char *alias=effect_alias(*commands);
                if(!memcmp(text+5,alias,3)) { cell->effect=*commands;break; }
            }
            if(cell->effect==TT_EFFECT_NONE)return 0;
        }
        if(text[8]=='$' && text[9]=='.')
            cell->value=TT_EFFECT_VALUE_REGISTER_PREFIX;
        else if(text[8]=='$' && hex(text[9])>=0)
            cell->value=tt_effect_register_value(hex(text[9]));
        else if(text[8]=='.' && text[9]=='.')cell->value=-1;
        else {
            int high=hex(text[8]),low=hex(text[9]);
            if(high<0 || low<0)return 0;
            cell->value=high*16+low;
        }
    }
    return 1;
}
static void cell_row_text(const TtCell *cell,char text[20]) {
    static const char digits[]="0123456789ABCDEF";
    if(cell->note==TT_NOTE_OFF)memcpy(text,"OFF",3);
    else if(tt_note_is_relative(cell->note)) {
        static const char digits[]="0123456789ABCDEF";
        text[0]=tt_note_relative_positive(cell->note)?'+':'-';text[1]='=';
        text[2]=digits[tt_note_relative_amount(cell->note)];
    }
    else if(cell->note>=0)tt_note_name(cell->note,text);
    else memcpy(text,"...",3);
    if(cell->instrument>=0 && cell->instrument<=255) {
        text[3]=digits[cell->instrument>>4];text[4]=digits[cell->instrument&15];
    } else memcpy(text+3,"..",2);
    for(int slot=0;slot<3;++slot) {
        const TtPatternEffect *fx=tt_cell_effect_const(cell,slot);
        const char *alias=fx->command==TT_EFFECT_NONE?NULL:effect_alias(fx->command);
        if(!alias)memcpy(text+5+slot*5,".....",5);
        else {
            memcpy(text+5+slot*5,alias,3);
            if(tt_effect_is_register(fx->value) ||
               fx->value==TT_EFFECT_VALUE_REGISTER_PREFIX) {
                char value[3];tt_effect_value_text(fx->value,value);
                memcpy(text+8+slot*5,value,2);
            } else if(fx->value>=0 && fx->value<=255) {
                text[8+slot*5]=digits[fx->value>>4];text[9+slot*5]=digits[fx->value&15];
            } else memcpy(text+8+slot*5,"..",2);
        }
    }
    text[20]=0;
}
void tt_pattern_row_text(const TtPattern *pattern,int row,char text[126]) {
    for(int channel=0;channel<TT_CHANNELS;++channel) {
        if(channel)text[channel*21-1]='|';
        cell_row_text(&pattern->cell[row][channel],text+channel*21);
    }
    text[125]=0;
}
int tt_pattern_parse_row(const char *text,TtPattern *pattern,int row) {
    size_t length=strlen(text);
    if(row<0 || row>=TT_MAX_STEPS || (length!=65 && length!=125))return 0;
    if(length==65) {
        for(int channel=0;channel<TT_CHANNELS;++channel) {
            int at=channel*11;
            if(channel && text[at-1]!='|')return 0;
            if(!parse_cell(text+at,&pattern->cell[row][channel]))return 0;
            pattern->cell[row][channel].effect2=pattern->cell[row][channel].effect3=TT_EFFECT_NONE;
            pattern->cell[row][channel].value2=pattern->cell[row][channel].value3=-1;
        }
        return 1;
    }
    for(int channel=0;channel<TT_CHANNELS;++channel) {
        int at=channel*21;
        if(channel && text[at-1]!='|')return 0;
        TtCell *cell=&pattern->cell[row][channel];
        char old[11];memcpy(old,text+at,5);memcpy(old+5,text+at+5,5);old[10]=0;
        if(!parse_cell(old,cell))return 0;
        for(int slot=1;slot<3;++slot) {
            char token[12]="..... .....";memcpy(token+5,text+at+5+slot*5,5);
            TtCell fx=tt_cell_empty();
            if(!parse_cell(token,&fx))return 0;
            TtPatternEffect *dst=tt_cell_effect(cell,slot);*dst=*tt_cell_effect(&fx,0);
        }
        if(tt_cell_table_effect_count(cell)>1)return 0;
    }
    return 1;
}
int tt_pattern_save(const char *path,const TtPattern *pattern,
                        char *error,size_t error_size) {
    FILE *file=fopen(path,"w");
    if(!file) { snprintf(error,error_size,"Cannot save pattern: %s",strerror(errno));return 0; }
    for(int row=0;row<TT_STEPS;++row) {
        for(int channel=0;channel<TT_CHANNELS;++channel) {
            char text[21];cell_row_text(&pattern->cell[row][channel],text);
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
int tt_pattern_load(const char *path,TtPattern *pattern,
                        char *error,size_t error_size) {
    FILE *file=fopen(path,"r");
    if(!file) { snprintf(error,error_size,"Cannot open pattern: %s",strerror(errno));return 0; }
    TtPattern next;
    tt_pattern_init(&next);
    char line[128];int row=0,valid=1;
    while(fgets(line,sizeof(line),file)) {
        if(row>=TT_STEPS) { valid=0;break; }
        size_t length=strcspn(line,"\r\n");
        if(length!=TT_CHANNELS*10+TT_CHANNELS-1 &&
           length!=TT_CHANNELS*20+TT_CHANNELS-1) { valid=0;break; }
        line[length]=0;
        if(!tt_pattern_parse_row(line,&next,row))valid=0;
        if(!valid)break;
        ++row;
    }
    if(ferror(file))valid=0;
    fclose(file);
    if(!valid || row!=TT_STEPS) {
        snprintf(error,error_size,"Invalid pattern near row %02X",row);return 0;
    }
    *pattern=next;
    return 1;
}
