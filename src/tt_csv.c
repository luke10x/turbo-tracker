#include "tt_csv.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void csv_field(FILE *file,const char *value) {
    fputc('"',file);
    for(const char *at=value;*at;++at) {
        if(*at=='"')fputc('"',file);
        fputc(*at,file);
    }
    fputc('"',file);
}
int tt_csv_save(const char *path,const TtWave *wave,
                    const TtPhrase *phrase,char *error,size_t error_size) {
    FILE *file=fopen(path,"w");
    if(!file) { snprintf(error,error_size,"Cannot save CSV: %s",strerror(errno));return 0; }
    fputs("record,index,value,start,end,base_note,note_offset\nsource_wav,,",file);
    csv_field(file,wave->source_name[0]?wave->source_name:wave->name);
    fputs(",,,,\nsample_name,,",file);
    csv_field(file,wave->name);
    fprintf(file,",,,,\nsample_frames,,%d,,,,\nreference_note,,%d,,,,\nbpm,,%d,,,,\n",
            wave->frames,phrase->reference_note,phrase->bpm);
    for(int i=0;i<TT_MARKERS;++i)
        fprintf(file,"marker,%X,%d,,,,\n",i,phrase->marker[i]);
    for(int i=0;i<TT_SLICES;++i) {
        fprintf(file,"slice,%X,,%d,%d,",i,phrase->marker[i],phrase->marker[i+1]);
        if(phrase->slice_note_offset[i]!=TT_SLICE_INHERIT)
            fprintf(file,"%d",tt_slice_base_note(phrase,i));
        fputc(',',file);
        if(phrase->slice_note_offset[i]!=TT_SLICE_INHERIT)
            fprintf(file,"%d",phrase->slice_note_offset[i]);
        fputc('\n',file);
    }
    if(fclose(file)!=0) {
        snprintf(error,error_size,"Could not finish CSV: %s",strerror(errno));return 0;
    }
    return 1;
}
static int split_csv(char *line,char *field[7],int columns) {
    char *read=line,*write=line;
    for(int column=0;column<columns;++column) {
        field[column]=write;
        if(*read=='"') {
            ++read;
            while(*read) {
                if(*read=='"') {
                    if(read[1]=='"') { *write++='"';read+=2;continue; }
                    ++read;break;
                }
                *write++=*read++;
            }
            if(*read && *read!=',' && *read!='\n' && *read!='\r')return 0;
        } else {
            while(*read && *read!=',' && *read!='\n' && *read!='\r')
                *write++=*read++;
        }
        char separator=*read;
        *write++=0;
        if(column==columns-1)return separator=='\n' || separator=='\r' || separator==0;
        if(separator!=',')return 0;
        ++read;
    }
    return 0;
}
static int integer(const char *text,int *number) {
    if(!*text)return 0;
    char *end;
    errno=0;
    long value=strtol(text,&end,10);
    if(errno || *end || value<INT_MIN || value>INT_MAX)return 0;
    *number=(int)value;return 1;
}
static int hex_index(const char *text,int *index) {
    if(!text[0] || text[1])return 0;
    char c=text[0];
    if(c>='0' && c<='9')*index=c-'0';
    else if(c>='A' && c<='F')*index=c-'A'+10;
    else if(c>='a' && c<='f')*index=c-'a'+10;
    else return 0;
    return 1;
}
int tt_csv_load(const char *path,TtWave *wave,
                    TtPhrase *phrase,char *error,size_t error_size) {
    FILE *file=fopen(path,"r");
    if(!file) { snprintf(error,error_size,"Cannot open CSV: %s",strerror(errno));return 0; }
    TtPhrase next=*phrase;
    unsigned markers=0,steps=0,slices=0,metadata=0;
    char source[256]={0},sample_name[256]={0},line[1024];
    int source_frames=-1,row=0,valid=1,version=0;
    int slice_start[TT_SLICES],slice_end[TT_SLICES],slice_note[TT_SLICES];
    int slice_offset[TT_SLICES];
    for(int i=0;i<TT_SLICES;++i) { slice_note[i]=-1;slice_offset[i]=TT_SLICE_INHERIT; }
    while(fgets(line,sizeof(line),file)) {
        ++row;
        if(!strchr(line,'\n') && !feof(file)) { valid=0;break; }
        if(row==1) {
            if(!strcmp(line,"record,index,value,start,end,base_note,note_offset\n") ||
               !strcmp(line,"record,index,value,start,end,base_note,note_offset\r\n"))version=2;
            else if(!strcmp(line,"record,index,value,start,end\n") ||
                    !strcmp(line,"record,index,value,start,end\r\n"))version=1;
            else valid=0;
            if(!valid)break;
            continue;
        }
        char *field[7]={0};
        if(!split_csv(line,field,version==2?7:5)) { valid=0;break; }
        int index,value;
        if(!strcmp(field[0],"source_wav")) {
            if((metadata&1) || strlen(field[2])>=sizeof(source))valid=0;
            else { strcpy(source,field[2]);metadata|=1; }
        } else if(version==2 && !strcmp(field[0],"sample_name")) {
            if((metadata&16) || !field[2][0] || strlen(field[2])>=sizeof(sample_name))valid=0;
            else { strcpy(sample_name,field[2]);metadata|=16; }
        } else if(!strcmp(field[0],"sample_frames")) {
            if((metadata&2) || !integer(field[2],&source_frames))valid=0;
            else metadata|=2;
        } else if(!strcmp(field[0],"reference_note")) {
            if((metadata&4) || !integer(field[2],&value) || value<0 || value>127)valid=0;
            else { next.reference_note=value;metadata|=4; }
        } else if(!strcmp(field[0],"bpm")) {
            if((metadata&8) || !integer(field[2],&value) || value<30 || value>300)valid=0;
            else { next.bpm=value;metadata|=8; }
        } else if(!strcmp(field[0],"marker")) {
            if(!hex_index(field[1],&index) || !integer(field[2],&value) ||
               value<0 || value>wave->frames)valid=0;
            else if(markers&(1u<<index))valid=0;
            else { next.marker[index]=value;markers|=1u<<index; }
        } else if(version==2 && !strcmp(field[0],"slice")) {
            if(!hex_index(field[1],&index) || index>=TT_SLICES ||
               (slices&(1u<<index)) || !integer(field[3],&slice_start[index]) ||
               !integer(field[4],&slice_end[index]))valid=0;
            else {
                if(!!*field[5] != !!*field[6])valid=0;
                if(*field[5] && (!integer(field[5],&slice_note[index]) ||
                                 slice_note[index]<0 || slice_note[index]>127 ||
                                 !integer(field[6],&slice_offset[index]) ||
                                 slice_offset[index]<-127 || slice_offset[index]>127))valid=0;
                slices|=1u<<index;
            }
        } else if(version==1 && !strcmp(field[0],"step")) {
            if(!hex_index(field[1],&index) || (steps&(1u<<index)))valid=0;
            else steps|=1u<<index;
        } else valid=0;
        if(!valid)break;
    }
    if(ferror(file))valid=0;
    fclose(file);
    if(!valid || metadata!=(version==2?31u:15u) || markers!=0xffff ||
       (version==2 && (slices!=0x7fff || row!=37)) ||
       (version==1 && ((row!=21 && row!=37) || (row==37 && steps!=0xffff)))) {
        snprintf(error,error_size,"Invalid slice CSV near row %d",row);return 0;
    }
    const char *expected=wave->source_name[0]?wave->source_name:wave->name;
    if(strcmp(source,expected) || source_frames!=wave->frames) {
        snprintf(error,error_size,"Load source WAV %s (%d frames) first",source,source_frames);
        return 0;
    }
    for(int i=1;i<TT_MARKERS;++i)if(next.marker[i]<next.marker[i-1]) {
        snprintf(error,error_size,"CSV markers must stay in order");return 0;
    }
    if(version==1)for(int i=0;i<TT_SLICES;++i)
        next.slice_note_offset[i]=TT_SLICE_INHERIT;
    if(version==2)for(int i=0;i<TT_SLICES;++i) {
        if(slice_start[i]!=next.marker[i] || slice_end[i]!=next.marker[i+1]) {
            snprintf(error,error_size,"Slice %X does not match its markers",i);return 0;
        }
        int effective=next.reference_note+slice_offset[i];
        if(effective<0)effective=0;
        if(effective>127)effective=127;
        if(slice_note[i]>=0 && slice_note[i]!=effective) {
            snprintf(error,error_size,"Slice %X base note does not match offset",i);return 0;
        }
        next.slice_note_offset[i]=slice_offset[i];
    }
    *phrase=next;
    if(version==2)snprintf(wave->name,sizeof(wave->name),"%s",sample_name);
    return 1;
}
