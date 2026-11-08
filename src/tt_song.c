#include "tt_song.h"
#include "tt_pattern.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

void tt_song_init(TtSong *song) {
    memset(song,0,sizeof(*song));
    song->bpm=120;song->pattern_rows=TT_STEPS;
    song->rows_per_beat=4;
    song->ticks_per_row=TT_TICKS_PER_ROW;
    song->pattern_count=1;song->order_count=1;
    for(int pattern=0;pattern<TT_SONG_PATTERNS;++pattern)
        for(int row=0;row<TT_MAX_STEPS;++row)
            for(int channel=0;channel<TT_CHANNELS;++channel)
                song->pattern[pattern].cell[row][channel]=
                    tt_cell_empty();
    snprintf(song->name[0],TT_SONG_NAME,"Pattern 00");
    tt_pattern_init(&song->pattern[0]);
    for(int table=0;table<TT_SONG_TABLES;++table) {
        snprintf(song->table[table].name,TT_SONG_NAME,"Table %02X",table);
        song->table[table].length=TT_TABLE_MIN_TICKS;
        for(int tick=0;tick<TT_TABLE_MAX_TICKS;++tick) {
            song->table[table].tick[tick].note=-1;
            song->table[table].tick[tick].instrument=-1;
            for(int effect=0;effect<TT_TABLE_EFFECTS;++effect)
                song->table[table].tick[tick].effect[effect].value=-1;
        }
    }
}
void tt_song_make_demo(TtSong *song) {
    tt_song_init(song);
    song->bpm=112;
    song->pattern_count=4;
    song->order_count=4;
    song->order[0]=0;song->order[1]=2;
    song->order[2]=1;song->order[3]=3;
    const char *names[]={"tgndshtn part 1","tgndshtn part 2",
                         "tgndshtn part 3","tgndshtn part 4"};
    for(int pattern=0;pattern<4;++pattern) {
        snprintf(song->name[pattern],TT_SONG_NAME,"%s",names[pattern]);
        for(int row=0;row<TT_STEPS;++row)
            for(int channel=0;channel<TT_CHANNELS;++channel)
                song->pattern[pattern].cell[row][channel]=
                    tt_cell_empty();
        song->pattern[pattern].cell[0][0]=
            (TtCell){60,pattern+1,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    }
    TtTableTick *demo=&song->table[0].tick[0];
    demo->note=60;demo->instrument=1;
    demo->effect[0]=(TtTableEffect){'M',0x33};
    demo->effect[1]=(TtTableEffect){'S',0xaf};
    demo->effect[4]=(TtTableEffect){'U',0x05};
    song->table[0].tick[1].effect[0]=(TtTableEffect){'V',0x43};
    song->table[0].tick[1].effect[1]=(TtTableEffect){'W',0x34};
    song->table[0].tick[1].effect[2]=(TtTableEffect){'N',0x37};
    demo=&song->table[0].tick[8];
    demo->note=64;demo->instrument=1;
    demo->effect[0]=(TtTableEffect){'S',0xfa};
}
static int hex(int c) {
    if(c>='0' && c<='9')return c-'0';
    if(c>='A' && c<='F')return c-'A'+10;
    if(c>='a' && c<='f')return c-'a'+10;
    return -1;
}
static int encode(const char *source,char *out,size_t cap) {
    const char *digits="0123456789ABCDEF";size_t at=0;
    for(const unsigned char *p=(const unsigned char *)source;*p;++p) {
        int escape=*p=='%' || *p=='\t' || *p=='\r' || *p=='\n' || *p<32 || *p>=127;
        if(at+(escape?3u:1u)>=cap)return 0;
        if(escape) { out[at++]='%';out[at++]=digits[*p>>4];out[at++]=digits[*p&15]; }
        else out[at++]=(char)*p;
    }
    out[at]=0;return 1;
}
static int decode(const char *source,char *out,size_t cap) {
    size_t at=0;
    for(const char *p=source;*p;++p) {
        unsigned char c=(unsigned char)*p;
        if(c=='%') {
            int hi=hex((unsigned char)p[1]),lo=p[1]?hex((unsigned char)p[2]):-1;
            if(hi<0 || lo<0)return 0;
            c=(unsigned char)((hi<<4)|lo);p+=2;
        }
        if(!c || at+1>=cap)return 0;
        out[at++]=(char)c;
    }
    out[at]=0;return 1;
}
static int integer(const char *text,int low,int high,int *value) {
    if(!*text)return 0;
    char *end;errno=0;long n=strtol(text,&end,10);
    if(errno || *end || n<low || n>high)return 0;
    *value=(int)n;return 1;
}
static int fields(FILE *file,char *line,size_t cap,char **part,int count) {
    if(!fgets(line,(int)cap,file))return 0;
    size_t n=strlen(line);
    if(!n || line[n-1]!='\n')return 0;
    line[--n]=0;if(n && line[n-1]=='\r')line[--n]=0;
    int found=1;part[0]=line;
    for(size_t i=0;i<n;++i)if(line[i]=='\t') {
        line[i]=0;
        if(found>=count)return 0;
        part[found++]=line+i+1;
    }
    return found==count;
}
static void saved_path(const char *song_path,const TtSongSample *sample,
                       char out[512]) {
    const char *source=sample->path;
    if(!strcmp(source,"builtin:demo") || source[0]!='/') {
        snprintf(out,512,"%s",source);return;
    }
    if(!strcmp(source,"/sample-tracker-upload.wav")) {
        snprintf(out,512,"%s",sample->source_name);return;
    }
    char directory[PATH_MAX];
    const char *end=strrchr(song_path,'/');
    if(!end)snprintf(directory,sizeof(directory),".");
    else if(end==song_path)snprintf(directory,sizeof(directory),"/");
    else snprintf(directory,sizeof(directory),"%.*s",(int)(end-song_path),song_path);
    char absolute_directory[PATH_MAX];
    if(directory[0]=='/')snprintf(absolute_directory,sizeof(absolute_directory),"%s",directory);
    else {
        char cwd[PATH_MAX];
        if(!getcwd(cwd,sizeof(cwd))) { snprintf(out,512,"%s",source);return; }
        if(!strcmp(directory,"."))snprintf(absolute_directory,sizeof(absolute_directory),"%s",cwd);
        else snprintf(absolute_directory,sizeof(absolute_directory),"%s/%s",cwd,directory);
    }
    /* Compare complete path components, then climb once per remaining
       directory component. This also handles sibling and parent folders. */
    const char *a=absolute_directory+1,*b=source+1;
    while(*a && *b) {
        const char *ae=strchr(a,'/'),*be=strchr(b,'/');
        size_t an=ae?(size_t)(ae-a):strlen(a);
        size_t bn=be?(size_t)(be-b):strlen(b);
        if(an!=bn || strncmp(a,b,an))break;
        if(!ae || !be) { a+=an;b+=bn;if(*b=='/')++b;break; }
        a=ae+1;b=be+1;
    }
    char relative[512]={0};size_t at=0;
    while(*a) {
        const char *slash=strchr(a,'/');
        if(at+3>=sizeof(relative))break;
        memcpy(relative+at,"../",3);at+=3;
        if(!slash)break;
        a=slash+1;
    }
    if(*b && at+strlen(b)<sizeof(relative))
        snprintf(relative+at,sizeof(relative)-at,"%s",b);
    else if(!*b)relative[at]=0;
    else relative[0]=0;
    snprintf(out,512,"%s",relative[0]?relative:source);
}
int tt_song_resolve_path(const char *song_path,const char *saved,
                             char *resolved,size_t size) {
    if(!strcmp(saved,"builtin:demo") || saved[0]=='/')
        return snprintf(resolved,size,"%s",saved)<(int)size;
    const char *slash=strrchr(song_path,'/');
    if(song_path[0]!='/') {
        char cwd[PATH_MAX];
        if(!getcwd(cwd,sizeof(cwd)))return 0;
        return slash?
            snprintf(resolved,size,"%s/%.*s/%s",cwd,(int)(slash-song_path),song_path,saved)<(int)size:
            snprintf(resolved,size,"%s/%s",cwd,saved)<(int)size;
    }
    if(!slash)return snprintf(resolved,size,"%s",saved)<(int)size;
    return snprintf(resolved,size,"%.*s/%s",(int)(slash-song_path),song_path,saved)<(int)size;
}
int tt_song_save(const char *path,const TtSong *song,
                     const TtSongSample *samples,int sample_count,
                     char *error,size_t error_size) {
    if(sample_count<1 || sample_count>TT_SONG_SAMPLES ||
       song->pattern_count<1 || song->pattern_count>TT_SONG_PATTERNS ||
       song->order_count<1 || song->order_count>TT_SONG_ORDER ||
       song->bpm<30 || song->bpm>300 ||
       song->pattern_rows<16 || song->pattern_rows>TT_MAX_STEPS ||
       song->pattern_rows%16 || song->rows_per_beat<1 ||
       song->rows_per_beat>16 || song->ticks_per_row<1 ||
       song->ticks_per_row>TT_MAX_TICKS_PER_ROW) {
        snprintf(error,error_size,"Invalid song counts or BPM");return 0;
    }
    FILE *file=fopen(path,"w");
    if(!file) { snprintf(error,error_size,"Cannot save song: %s",strerror(errno));return 0; }
    fprintf(file,"F80_SAMPLE_SONG\t5\nBPM\t%d\nROWS\t%d\nBEAT_ROWS\t%d\nTICKS\t%d\nSAMPLES\t%d\n",
            song->bpm,song->pattern_rows,song->rows_per_beat,
            song->ticks_per_row,sample_count);
    int valid=1;
    for(int i=0;i<sample_count && valid;++i) {
        char name[768],source[768],path_text[1536],relative[512];
        saved_path(path,&samples[i],relative);
        if(!encode(samples[i].name,name,sizeof(name)) ||
           !encode(samples[i].source_name,source,sizeof(source)) ||
           !encode(relative,path_text,sizeof(path_text))) { valid=0;break; }
        fprintf(file,"SAMPLE\t%d\t%s\t%s\t%s\t%d\t%d\t%d\n",
                i,name,source,path_text,samples[i].frames,samples[i].rate,
                samples[i].phrase.reference_note);
        for(int marker=0;marker<TT_MARKERS;++marker)
            fprintf(file,"MARKER\t%d\t%d\t%d\n",i,marker,
                    samples[i].phrase.marker[marker]);
        for(int slice=0;slice<TT_SLICES;++slice)
            fprintf(file,"SLICE\t%d\t%d\t%d\n",i,slice,
                    samples[i].phrase.slice_note_offset[slice]);
    }
    fprintf(file,"PATTERNS\t%d\n",song->pattern_count);
    for(int i=0;i<song->pattern_count && valid;++i) {
        char name[192];if(!encode(song->name[i],name,sizeof(name))) { valid=0;break; }
        fprintf(file,"PATTERN\t%d\t%s\n",i,name);
        for(int row=0;row<song->pattern_rows;++row) {
            char text[126];tt_pattern_row_text(&song->pattern[i],row,text);
            fprintf(file,"ROW\t%d\t%s\n",row,text);
        }
    }
    fprintf(file,"ORDER\t%d\n",song->order_count);
    for(int i=0;i<song->order_count;++i) {
        if(song->order[i]>=song->pattern_count)valid=0;
        fprintf(file,"AT\t%d\t%d\n",i,song->order[i]);
    }
    fprintf(file,"TABLES\t%d\n",TT_SONG_TABLES);
    for(int table=0;table<TT_SONG_TABLES;++table) {
        const TtTable *t=&song->table[table];
        char name[3*TT_SONG_NAME];
        if(!t->name[0] || !encode(t->name,name,sizeof(name)))valid=0;
        if(t->length<TT_TABLE_MIN_TICKS || t->length>TT_TABLE_MAX_TICKS ||
           t->length%TT_TABLE_MIN_TICKS)valid=0;
        fprintf(file,"TABLE\t%d\t%d\t%s\n",table,t->length,valid?name:"");
        for(int tick=0;tick<t->length && tick<TT_TABLE_MAX_TICKS;++tick) {
            const TtTableTick *entry=&song->table[table].tick[tick];
            if(entry->note < -1 || entry->note>127 ||
               entry->instrument < -1 || entry->instrument>255)valid=0;
            fprintf(file,"TICK\t%d\t%d\t%d",tick,entry->note,entry->instrument);
            for(int effect=0;effect<TT_TABLE_EFFECTS;++effect) {
                const TtTableEffect *fx=&entry->effect[effect];
                if(!fx->command)fputs("\t...",file);
                else {
                    if((fx->command<'G' || fx->command>'Z') && fx->command!='r')
                        valid=0;
                    if(fx->value>255 || fx->value< -1)
                        valid=0;
                    if(fx->value<0)fprintf(file,"\t%c..",fx->command);
                    else fprintf(file,"\t%c%02X",fx->command,fx->value);
                }
            }
            fputc('\n',file);
        }
    }
    fputs("END\n",file);
    if(fclose(file)!=0 || !valid) {
        snprintf(error,error_size,"Could not finish song file");return 0;
    }
    return 1;
}
int tt_song_load(const char *path,TtSong *song,
                     TtSongSample *samples,int *sample_count,
                     char *error,size_t error_size) {
    FILE *file=fopen(path,"r");
    if(!file) { snprintf(error,error_size,"Cannot open song: %s",strerror(errno));return 0; }
    TtSong *next=calloc(1,sizeof(*next));
    TtSongSample *source=calloc(TT_SONG_SAMPLES,sizeof(*source));
    if(!next || !source) {
        free(next);free(source);fclose(file);
        snprintf(error,error_size,"Out of memory loading song");return 0;
    }
    char line[4096],*field[10];int count=0,valid=1,number,version=0;
#define READ(N) fields(file,line,sizeof(line),field,(N))
    if(!READ(2) || strcmp(field[0],"F80_SAMPLE_SONG"))valid=0;
    else if(!strcmp(field[1],"1"))version=1;
    else if(!strcmp(field[1],"2"))version=2;
    else if(!strcmp(field[1],"3"))version=3;
    else if(!strcmp(field[1],"4"))version=4;
    else if(!strcmp(field[1],"5"))version=5;
    else valid=0;
    next->pattern_rows=TT_STEPS;
    next->rows_per_beat=4;
    next->ticks_per_row=TT_TICKS_PER_ROW;
    if(valid && (!READ(2) || strcmp(field[0],"BPM") ||
                 !integer(field[1],30,300,&next->bpm)))valid=0;
    if(valid && version>=4 && (!READ(2) || strcmp(field[0],"ROWS") ||
                 !integer(field[1],16,TT_MAX_STEPS,&next->pattern_rows) ||
                 next->pattern_rows%16))valid=0;
    if(valid && version>=4 && (!READ(2) || strcmp(field[0],"BEAT_ROWS") ||
                 !integer(field[1],1,16,&next->rows_per_beat)))valid=0;
    if(valid && version>=4 && (!READ(2) || strcmp(field[0],"TICKS") ||
                 !integer(field[1],1,TT_MAX_TICKS_PER_ROW,&next->ticks_per_row)))valid=0;
    if(valid && (!READ(2) || strcmp(field[0],"SAMPLES") ||
                 !integer(field[1],1,TT_SONG_SAMPLES,&count)))valid=0;
    for(int i=0;i<count && valid;++i) {
        TtSongSample *s=&source[i];
        if(!READ(8) || strcmp(field[0],"SAMPLE") ||
           !integer(field[1],i,i,&number) ||
           !decode(field[2],s->name,sizeof(s->name)) || !s->name[0] ||
           !decode(field[3],s->source_name,sizeof(s->source_name)) || !s->source_name[0] ||
           !decode(field[4],s->path,sizeof(s->path)) || !s->path[0] ||
           !integer(field[5],1,TT_MAX_FRAMES,&s->frames) ||
           !integer(field[6],1,384000,&s->rate) ||
           !integer(field[7],0,127,&s->phrase.reference_note)) { valid=0;break; }
        s->phrase.bpm=next->bpm;
        for(int marker=0;marker<TT_MARKERS && valid;++marker) {
            if(!READ(4) || strcmp(field[0],"MARKER") ||
               !integer(field[1],i,i,&number) ||
               !integer(field[2],marker,marker,&number) ||
               !integer(field[3],0,s->frames,&s->phrase.marker[marker]))valid=0;
        }
        for(int marker=1;marker<TT_MARKERS;++marker)
            if(s->phrase.marker[marker]<s->phrase.marker[marker-1])valid=0;
        for(int slice=0;slice<TT_SLICES && valid;++slice) {
            if(!READ(4) || strcmp(field[0],"SLICE") ||
               !integer(field[1],i,i,&number) ||
               !integer(field[2],slice,slice,&number)) { valid=0;break; }
            if(!strcmp(field[3],"-1000"))
                s->phrase.slice_note_offset[slice]=TT_SLICE_INHERIT;
            else if(!integer(field[3],-127,127,&s->phrase.slice_note_offset[slice]))valid=0;
        }
    }
    if(valid && (!READ(2) || strcmp(field[0],"PATTERNS") ||
                 !integer(field[1],1,TT_SONG_PATTERNS,&next->pattern_count)))valid=0;
    for(int i=0;i<next->pattern_count && valid;++i) {
        for(int row=0;row<TT_MAX_STEPS;++row)
            for(int channel=0;channel<TT_CHANNELS;++channel)
                next->pattern[i].cell[row][channel]=
                    tt_cell_empty();
        if(!READ(3) || strcmp(field[0],"PATTERN") ||
           !integer(field[1],i,i,&number) ||
           !decode(field[2],next->name[i],TT_SONG_NAME) || !next->name[i][0])valid=0;
        for(int row=0;row<next->pattern_rows && valid;++row) {
            if(!READ(3) || strcmp(field[0],"ROW") ||
               !integer(field[1],row,row,&number) ||
               !tt_pattern_parse_row(field[2],&next->pattern[i],row))valid=0;
        }
    }
    if(valid && (!READ(2) || strcmp(field[0],"ORDER") ||
                 !integer(field[1],1,TT_SONG_ORDER,&next->order_count)))valid=0;
    for(int i=0;i<next->order_count && valid;++i) {
        if(!READ(3) || strcmp(field[0],"AT") ||
           !integer(field[1],i,i,&number) ||
           !integer(field[2],0,next->pattern_count-1,&number))valid=0;
        else next->order[i]=(unsigned char)number;
    }
    for(int table=0;table<TT_SONG_TABLES;++table) {
        snprintf(next->table[table].name,TT_SONG_NAME,"Table %02X",table);
        next->table[table].length=TT_TABLE_MIN_TICKS;
        for(int tick=0;tick<TT_TABLE_MAX_TICKS;++tick) {
            next->table[table].tick[tick].note=-1;
            next->table[table].tick[tick].instrument=-1;
            for(int effect=0;effect<TT_TABLE_EFFECTS;++effect)
                next->table[table].tick[tick].effect[effect].value=-1;
        }
    }
    if(valid && version>=2) {
        if(!READ(2) || strcmp(field[0],"TABLES") ||
           !integer(field[1],TT_SONG_TABLES,TT_SONG_TABLES,&number))valid=0;
        for(int table=0;table<TT_SONG_TABLES && valid;++table) {
            int length=0;
            if(!READ(version>=3?4:3) || strcmp(field[0],"TABLE") ||
               !integer(field[1],table,table,&number) ||
               !integer(field[2],TT_TABLE_MIN_TICKS,TT_TABLE_MAX_TICKS,&length) ||
               length%TT_TABLE_MIN_TICKS) { valid=0;break; }
            if(version>=3 && (!decode(field[3],next->table[table].name,
                                      TT_SONG_NAME) || !next->table[table].name[0])) {
                valid=0;break;
            }
            next->table[table].length=length;
            for(int tick=0;tick<length && valid;++tick) {
                TtTableTick *entry=&next->table[table].tick[tick];
                if(!READ(10) || strcmp(field[0],"TICK") ||
                   !integer(field[1],tick,tick,&number) ||
                   !integer(field[2],-1,127,&entry->note) ||
                   !integer(field[3],-1,255,&entry->instrument)) { valid=0;break; }
                for(int effect=0;effect<TT_TABLE_EFFECTS;++effect) {
                    const char *token=field[4+effect];
                    TtTableEffect *fx=&entry->effect[effect];
                    if(!strcmp(token,"..."))continue;
                    if(strlen(token)!=3 ||
                       ((token[0]<'G' || token[0]>'Z') &&
                        !(version<=4 && token[0]=='~') &&
                        !(version>=5 && token[0]=='r'))) {
                        valid=0;break;
                    }
                    fx->command=version<=4 && token[0]=='R'?'r':
                                version<=4 && token[0]=='~'?'R':token[0];
                    if(token[1]=='.' && token[2]=='.')fx->value=-1;
                    else {
                        int hi=hex(token[1]),lo=hex(token[2]);
                        if(hi<0 || lo<0) { valid=0;break; }
                        fx->value=hi*16+lo;
                    }
                }
            }
        }
    }
    if(valid && (!READ(1) || strcmp(field[0],"END") || fgetc(file)!=EOF))valid=0;
    if(ferror(file))valid=0;
    fclose(file);
#undef READ
    if(!valid) {
        free(next);free(source);
        snprintf(error,error_size,"Invalid song text or missing data");return 0;
    }
    *song=*next;memcpy(samples,source,(size_t)count*sizeof(*source));*sample_count=count;
    free(next);free(source);
    return 1;
}
