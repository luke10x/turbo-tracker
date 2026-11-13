#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tt_edit.h"

static TtSong song;
static TtPattern active;
static TtStored samples[TT_SONG_SAMPLES];
static TtWave wave;
static TtPhrase phrase;
static int active_index,sample_count,sample_index;
static char loaded_path[512],source_ref[512],csv_path[256],song_path[512],message[192];
static TtEditDoc doc;

static void setup(void) {
    memset(&song,0,sizeof(song));tt_song_init(&song);
    active=song.pattern[0];active_index=0;
    memset(samples,0,sizeof(samples));memset(&wave,0,sizeof(wave));
    wave.frames=1600;wave.rate=8000;strcpy(wave.name,"One");
    tt_phrase_init(&phrase,wave.frames);
    sample_count=2;sample_index=0;
    strcpy(samples[1].wave.name,"Two");samples[1].wave.frames=1600;
    tt_phrase_init(&samples[1].phrase,1600);
    strcpy(loaded_path,"one.wav");source_ref[0]=0;csv_path[0]=0;
    strcpy(song_path,"old.song");message[0]=0;
    doc=(TtEditDoc){.song=&song,.active=&active,.active_index=&active_index,
        .samples=samples,.active_wave=&wave,.active_phrase=&phrase,
        .sample_count=&sample_count,.sample_index=&sample_index,
        .active_loaded_path=loaded_path,.active_source_ref=source_ref,
        .active_csv_path=csv_path,
        .song_path=song_path,
        .msg=message,.msg_size=sizeof(message)};
}
static int apply(TtEditEvent event) {
    message[0]=0;
    return tt_edit_apply(&doc,&event);
}
int main(void) {
    setup();
    TtEditHistory *synth_history=tt_edit_history_new(&doc);assert(synth_history);
    TtEditEvent synth={.type=TT_EDIT_INSTRUMENT_TYPE};
    synth.instrument_type.index=0;synth.instrument_type.type=TT_INSTRUMENT_DX7;
    assert(tt_edit_apply_recorded(synth_history,&doc,&synth));
    assert(wave.instrument_type==TT_INSTRUMENT_DX7 && tt_dx7_valid(&wave.dx7));
    synth=(TtEditEvent){.type=TT_EDIT_DX7_PARAMETER};
    synth.dx7_parameter.index=0;synth.dx7_parameter.parameter=134;synth.dx7_parameter.value=31;
    assert(tt_edit_apply_recorded(synth_history,&doc,&synth));
    synth.dx7_parameter.value=32;assert(!tt_edit_apply_recorded(synth_history,&doc,&synth));
    assert(wave.dx7.data[134]==31);
    assert(tt_edit_undo(synth_history,&doc) && wave.dx7.data[134]==0);
    assert(tt_edit_undo(synth_history,&doc) && wave.instrument_type==TT_INSTRUMENT_SAMPLE);
    assert(tt_edit_redo(synth_history,&doc) && wave.instrument_type==TT_INSTRUMENT_DX7);
    tt_edit_history_free(synth_history);
    setup();
    TtEditEvent e={.type=TT_EDIT_CELL_SET};
    e.cell_set=(TtEditCellSet){2,1,{60,1,TT_EFFECT_S,0x12,
                                      TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1}};
    assert(apply(e) && active.cell[2][1].note==60);
    e.cell_set.row=TT_MAX_STEPS;
    assert(!apply(e) && message[0]);
    e.cell_set.row=3;
    e.cell_set.cell=(TtCell){-1,-1,TT_EFFECT_T,0,TT_EFFECT_NONE,-1,
                              TT_EFFECT_T,1};
    assert(!apply(e) && strstr(message,"only one table"));
    TtCell paste[1]={{72,2,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1,
                       TT_EFFECT_NONE,-1}};
    e=(TtEditEvent){.type=TT_EDIT_CELLS_BULK};
    e.cells=(TtEditCellsBulk){TT_BULK_PASTE,2,1,2,1,0,paste,1};
    assert(apply(e) && active.cell[2][1].note==72);
    e.cells.op=TT_BULK_TRANSPOSE;e.cells.delta=3;
    assert(apply(e) && active.cell[2][1].note==75);
    e.cells.op=TT_BULK_CLEAR;
    assert(apply(e) && active.cell[2][1].note==-1);
    e.cells.left=-1;assert(!apply(e));
    e.cells.left=1;e.cells.op=(TtBulkOp)99;assert(!apply(e));
    TtCell duplicate_table_cell={-1,-1,TT_EFFECT_T,0,TT_EFFECT_T,1,
                                  TT_EFFECT_NONE,-1};
    e.cells=(TtEditCellsBulk){TT_BULK_PASTE,3,1,3,1,0,&duplicate_table_cell,1};
    assert(!apply(e) && strstr(message,"only one table"));

    int old_count=song.pattern_count;
    e=(TtEditEvent){.type=TT_EDIT_PATTERN_NEW};
    assert(apply(e) && song.pattern_count==old_count+1);
    song.pattern_count=TT_SONG_PATTERNS;
    assert(!apply(e) && !strcmp(message,"Pattern bank is full"));
    song.pattern_count=old_count+1;
    e=(TtEditEvent){.type=TT_EDIT_PATTERN_RENAME};
    e.pattern.index=old_count;strcpy(e.pattern.name,"Bridge");
    assert(apply(e) && !strcmp(song.name[old_count],"Bridge"));
    e.pattern.name[0]=0;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_PATTERN_SELECT};e.pattern.index=old_count;
    assert(apply(e) && active_index==old_count);
    active.cell[0][0].note=55;
    e=(TtEditEvent){.type=TT_EDIT_PATTERN_SYNC};
    assert(apply(e) && song.pattern[old_count].cell[0][0].note==55);
    e.type=TT_EDIT_PATTERN_SELECT;e.pattern.index=0;
    assert(apply(e) && active_index==0);
    e.pattern.index=TT_SONG_PATTERNS;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_ORDER_INSERT};
    e.order.position=1;e.order.pattern=old_count;
    int old_orders=song.order_count;
    assert(apply(e) && song.order_count==old_orders+1 && song.order[1]==old_count);
    e.order.pattern=TT_SONG_PATTERNS;assert(!apply(e));
    e.type=TT_EDIT_ORDER_SWAP;e.order.position=0;e.order.other=1;
    assert(apply(e) && song.order[0]==old_count);
    e.type=TT_EDIT_ORDER_ACTIVE;e.order.position=0;e.order.pattern=0;
    assert(apply(e) && !song.order_active[0]);
    e.order.pattern=2;assert(!apply(e));
    e.order.position=TT_SONG_ORDER;e.order.pattern=1;assert(!apply(e));
    e.type=TT_EDIT_ORDER_SWAP;e.order.position=0;e.order.other=1;
    e.order.other=TT_SONG_ORDER;assert(!apply(e));
    e.type=TT_EDIT_ORDER_REMOVE;e.order.position=0;
    assert(apply(e) && song.order_count==old_orders);
    e.order.position=TT_SONG_ORDER;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_BPM};e.bpm=400;
    assert(apply(e) && song.bpm==300);
    e=(TtEditEvent){.type=TT_EDIT_SETTINGS};
    e.settings=(TtEditSettings){120,16,4,12,TT_TABLE_DEFAULT_TICKS};
    assert(apply(e));
    for(int table=0;table<TT_SONG_TABLES;++table)
        assert(song.table[table].length==TT_TABLE_DEFAULT_TICKS);
    e.settings.table_length=TT_TABLE_MAX_TICKS+1;assert(!apply(e));
    e.settings.table_length=TT_TABLE_DEFAULT_TICKS;
    e.settings.pattern_rows=15;assert(!apply(e));

    e=(TtEditEvent){.type=TT_EDIT_TABLE_TICK_SET};
    e.table_tick=(TtEditTableTickSet){0,0,{.note=64,.instrument=1}};
    assert(apply(e) && song.table[0].tick[0].note==64);
    e.table_tick.tick=TT_TABLE_MAX_TICKS;assert(!apply(e));
    TtTableTick tick={.note=67,.instrument=2};
    e=(TtEditEvent){.type=TT_EDIT_TABLE_TICKS_BULK};
    e.table_ticks=(TtEditTableTicksBulk){TT_BULK_PASTE,0,0,0,0,1,&tick};
    assert(apply(e) && song.table[0].tick[0].note==67);
    e.table_ticks.op=TT_BULK_CLEAR;
    assert(apply(e) && song.table[0].tick[0].note==-1);
    e.table_ticks.op=TT_BULK_TRANSPOSE;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_TABLE_LENGTH};
    e.table.table=0;e.table.length=999;
    assert(apply(e) && song.table[0].length==TT_TABLE_MAX_TICKS);
    e=(TtEditEvent){.type=TT_EDIT_TABLE_NAME};
    e.table.table=0;strcpy(e.table.name,"Arp");
    assert(apply(e) && !strcmp(song.table[0].name,"Arp"));
    e.table.name[0]=0;assert(!apply(e));

    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_RENAME};
    e.sample.index=1;strcpy(e.sample.name,"Second");
    assert(apply(e) && !strcmp(samples[1].wave.name,"Second"));
    e.sample.name[0]=0;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_REFERENCE};
    e.sample.index=1;e.sample.value=69;
    assert(apply(e) && samples[1].phrase.reference_note==69);
    e.sample.index=3;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_MARKER_MOVE};
    e.marker=(TtEditMarker){1,150};
    assert(apply(e) && phrase.marker[1]==150);
    e.marker.marker=TT_MARKERS;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_SLICE_NOTE};
    e.slice=(TtEditSlice){0,72};
    assert(apply(e) && tt_slice_base_note(&phrase,0)==72);
    e.slice.note=128;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_BEATS_AUTODETECT};
    assert(!apply(e) && strstr(message,"Load a sample"));
    e.type=TT_EDIT_TONES_AUTODETECT;
    assert(!apply(e) && strstr(message,"Load a sample"));
    wave.frames=8000;wave.rate=8000;wave.pcm=malloc((size_t)wave.frames*sizeof(float));
    assert(wave.pcm);
    for(int i=0;i<wave.frames;++i)wave.pcm[i]=.5f*sinf(6.2831853f*440.0f*i/wave.rate);
    tt_phrase_init(&phrase,wave.frames);
    int original_markers[TT_MARKERS];memcpy(original_markers,phrase.marker,sizeof(original_markers));
    assert(apply(e));
    assert(!memcmp(original_markers,phrase.marker,sizeof(original_markers)));
    for(int i=0;i<TT_SLICES;++i)
        assert(tt_slice_base_note(&phrase,i)>=68 && tt_slice_base_note(&phrase,i)<=70);
    phrase.marker[1]=333;tt_slice_set_note(&phrase,0,74);
    TtEditHistory *auto_history=tt_edit_history_new(&doc);
    assert(auto_history && tt_edit_apply_recorded(auto_history,&doc,&e));
    assert(phrase.marker[1]==333 && tt_slice_base_note(&phrase,0)!=74);
    assert(tt_edit_undo(auto_history,&doc) && phrase.marker[1]==333 &&
           tt_slice_base_note(&phrase,0)==74);
    assert(tt_edit_redo(auto_history,&doc));
    assert(phrase.marker[1]==333 && tt_slice_base_note(&phrase,0)!=74);
    tt_slice_set_note(&phrase,0,74);
    e.type=TT_EDIT_BEATS_AUTODETECT;
    assert(tt_edit_apply_recorded(auto_history,&doc,&e));
    assert(phrase.marker[1]!=333 && tt_slice_base_note(&phrase,0)==74);
    assert(tt_edit_undo(auto_history,&doc) && phrase.marker[1]==333 &&
           tt_slice_base_note(&phrase,0)==74);
    assert(tt_edit_redo(auto_history,&doc) && phrase.marker[1]!=333 &&
           tt_slice_base_note(&phrase,0)==74);
    tt_edit_history_free(auto_history);
    wave.pcm=NULL; /* the history releases its registered sample buffer */

    active.cell[0][0].instrument=1;
    song.pattern[1].cell[0][0].instrument=2;
    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_SWAP};
    e.sample.index=0;e.sample.other=1;
    assert(apply(e) && sample_index==1 && !strcmp(wave.name,"One"));
    assert(active.cell[0][0].instrument==2 && song.pattern[1].cell[0][0].instrument==1);
    e.sample.other=5;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_DELETE};e.sample.index=0;
    assert(apply(e) && sample_count==1 && sample_index==0);
    assert(active.cell[0][0].instrument==1 && song.pattern[1].cell[0][0].instrument==-1);
    assert(!apply(e) && !strcmp(message,"Keep at least one sample"));
    setup();
    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_SELECT};e.sample.index=1;
    assert(apply(e) && sample_index==1 && !strcmp(wave.name,"Two"));
    e.sample.index=5;assert(!apply(e));
    TtWave added={.pcm=malloc(sizeof(float)),.frames=1,.rate=8000};
    assert(added.pcm);strcpy(added.name,"Three");
    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_APPEND};
    e.loaded_wave=(TtEditLoadedWave){0,&added,"three.wav"};
    int saved_count=sample_count;sample_count=TT_SONG_SAMPLES;
    assert(!apply(e) && added.pcm);
    sample_count=saved_count;
    assert(apply(e) && sample_count==3 && sample_index==2);
    assert(!added.pcm && !strcmp(loaded_path,"three.wav"));
    TtWave recovered={.pcm=malloc(sizeof(float)),.frames=1600,.rate=8000};
    assert(recovered.pcm);
    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_RECOVER};
    e.loaded_wave=(TtEditLoadedWave){0,&recovered,"restored.wav"};
    assert(apply(e) && samples[0].wave.pcm && !recovered.pcm);
    TtWave invalid={.pcm=malloc(sizeof(float)),.frames=10,.rate=8000};
    assert(invalid.pcm);
    e.loaded_wave=(TtEditLoadedWave){0,&invalid,"wrong.wav"};
    assert(!apply(e));tt_free(&invalid);
    TtSong replacement;tt_song_init(&replacement);
    TtStored incoming[TT_SONG_SAMPLES]={0};
    incoming[0].wave.pcm=malloc(sizeof(float));
    assert(incoming[0].wave.pcm);
    incoming[0].wave.frames=1;incoming[0].wave.rate=8000;
    strcpy(incoming[0].wave.name,"Replacement");
    tt_phrase_init(&incoming[0].phrase,1);
    strcpy(incoming[0].loaded_path,"replacement.wav");
    e=(TtEditEvent){.type=TT_EDIT_DOCUMENT_REPLACE};
    e.replacement=(TtEditDocumentReplace){&replacement,incoming,0};
    assert(!apply(e));
    e.replacement=(TtEditDocumentReplace){&replacement,incoming,1};
    assert(apply(e) && sample_count==1 && !strcmp(wave.name,"Replacement"));
    assert(!incoming[0].wave.pcm);
    tt_free(&wave);

    setup();
    wave.pcm=malloc(sizeof(float));
    samples[1].wave.pcm=malloc(sizeof(float));
    assert(wave.pcm && samples[1].wave.pcm);
    TtWave *original_pcm_owner=&wave;
    float *original_pcm=original_pcm_owner->pcm;
    TtEditHistory *history=tt_edit_history_new(&doc);
    assert(history && !tt_edit_can_undo(history));
    int pattern_before=song.pattern_count,order_before=song.order_count;
    e=(TtEditEvent){.type=TT_EDIT_PATTERN_APPEND_ORDER};
    assert(tt_edit_apply_recorded(history,&doc,&e));
    assert(song.pattern_count==pattern_before+1 && song.order_count==order_before+1 &&
           song.order[order_before]==pattern_before);
    assert(tt_edit_undo(history,&doc));
    assert(song.pattern_count==pattern_before && song.order_count==order_before &&
           !tt_edit_can_undo(history));
    assert(tt_edit_redo(history,&doc));
    assert(song.pattern_count==pattern_before+1 && song.order_count==order_before+1);
    assert(tt_edit_undo(history,&doc));
    e=(TtEditEvent){.type=TT_EDIT_ORDER_ACTIVE};
    e.order.position=0;e.order.pattern=0;
    int activity_before=song.order_active[0];
    assert(tt_edit_apply_recorded(history,&doc,&e));
    assert(song.order_active[0]==0);
    assert(tt_edit_undo(history,&doc) && song.order_active[0]==activity_before);
    assert(tt_edit_redo(history,&doc) && song.order_active[0]==0);
    assert(tt_edit_undo(history,&doc));
    int tail=song.order_count-1,tail_pattern=song.order[tail];
    song.order_active[tail]=0;
    int orders_before_append=song.order_count;
    e=(TtEditEvent){.type=TT_EDIT_ORDER_APPEND};
    assert(tt_edit_apply_recorded(history,&doc,&e));
    assert(song.order_count==orders_before_append+1 &&
           song.order[orders_before_append]==tail_pattern &&
           song.order_active[orders_before_append]==0);
    assert(tt_edit_undo(history,&doc) && song.order_count==orders_before_append);
    assert(tt_edit_redo(history,&doc) && song.order_active[orders_before_append]==0);
    assert(tt_edit_undo(history,&doc));
    active.cell[0][1]=(TtCell){.effect2=TT_EFFECT_M,.value2=0x20,
                               .effect3=TT_EFFECT_P,.value3=0x40};
    e=(TtEditEvent){.type=TT_EDIT_PATTERN_EFFECTS_COMPACT};
    assert(tt_edit_apply_recorded(history,&doc,&e));
    assert(active.cell[0][1].effect==TT_EFFECT_M &&
           active.cell[0][1].value==0x20 &&
           active.cell[0][1].effect2==TT_EFFECT_P &&
           active.cell[0][1].value2==0x40 &&
           active.cell[0][1].effect3==TT_EFFECT_NONE &&
           active.cell[0][1].value3==-1);
    assert(tt_edit_undo(history,&doc) &&
           active.cell[0][1].effect2==TT_EFFECT_M &&
           active.cell[0][1].effect3==TT_EFFECT_P);
    assert(tt_edit_redo(history,&doc) &&
           active.cell[0][1].effect==TT_EFFECT_M &&
           active.cell[0][1].effect2==TT_EFFECT_P);
    assert(tt_edit_undo(history,&doc));
    e=(TtEditEvent){.type=TT_EDIT_PATTERN_APPEND_ORDER};
    song.order_count=TT_SONG_ORDER;
    assert(!apply(e) && song.pattern_count==pattern_before &&
           !strcmp(message,"Order is full"));
    song.order_count=order_before;song.pattern_count=TT_SONG_PATTERNS;
    assert(!apply(e) && song.order_count==order_before &&
           !strcmp(message,"Pattern bank is full"));
    song.pattern_count=pattern_before;
    e=(TtEditEvent){.type=TT_EDIT_CELL_SET};
    e.cell_set=(TtEditCellSet){0,0,{61,1,TT_EFFECT_NONE,-1,
                                      TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1}};
    assert(tt_edit_apply_recorded(history,&doc,&e));
    assert(tt_edit_can_undo(history) && active.cell[0][0].note==61);
    assert(tt_edit_undo(history,&doc) && active.cell[0][0].note==60);
    assert(tt_edit_can_redo(history));
    assert(tt_edit_redo(history,&doc) && active.cell[0][0].note==61);
    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_SWAP};
    e.sample.index=0;e.sample.other=1;
    assert(tt_edit_apply_recorded(history,&doc,&e));
    assert(sample_index==1 && active.cell[0][0].instrument==2);
    assert(tt_edit_undo(history,&doc));
    assert(sample_index==0 && active.cell[0][0].instrument==1 && wave.pcm==original_pcm);
    assert(tt_edit_redo(history,&doc) && sample_index==1);
    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_SOURCE};
    e.source=(TtEditSource){1,NULL,"/tmp/shared.wav","/tmp/shared.wav"};
    assert(tt_edit_apply_recorded(history,&doc,&e));
    assert(!strcmp(source_ref,"/tmp/shared.wav"));
    assert(tt_edit_undo(history,&doc) && !source_ref[0]);
    assert(tt_edit_redo(history,&doc) && !strcmp(source_ref,"/tmp/shared.wav"));
    e=(TtEditEvent){.type=TT_EDIT_SAMPLE_DELETE};e.sample.index=1;
    assert(tt_edit_apply_recorded(history,&doc,&e) && sample_count==1);
    assert(tt_edit_undo(history,&doc) && sample_count==2);
    assert(wave.pcm==original_pcm);
    assert(tt_edit_redo(history,&doc) && sample_count==1);
    assert(tt_edit_undo(history,&doc) && sample_count==2);
    e=(TtEditEvent){.type=TT_EDIT_BPM};e.bpm=140;
    assert(tt_edit_apply_recorded(history,&doc,&e) && song.bpm==140);
    assert(!tt_edit_can_redo(history));
    strcpy(song_path,"saved.song");
    assert(tt_edit_undo(history,&doc) && !strcmp(song_path,"saved.song"));
    assert(tt_edit_redo(history,&doc) && song.bpm==140);
    strcpy(song_path,"old.song");
    TtSong replacement2;tt_song_init(&replacement2);
    TtStored incoming2[TT_SONG_SAMPLES]={0};
    incoming2[0].wave.pcm=malloc(sizeof(float));
    assert(incoming2[0].wave.pcm);
    incoming2[0].wave.frames=1;incoming2[0].wave.rate=8000;
    strcpy(incoming2[0].wave.name,"New song");
    tt_phrase_init(&incoming2[0].phrase,1);
    e=(TtEditEvent){.type=TT_EDIT_DOCUMENT_REPLACE};
    e.replacement=(TtEditDocumentReplace){&replacement2,incoming2,1};
    assert(tt_edit_apply_recorded(history,&doc,&e));
    strcpy(song_path,"new.song");
    assert(!strcmp(wave.name,"New song") && sample_count==1);
    assert(tt_edit_undo(history,&doc) && sample_count==2);
    assert(wave.pcm==original_pcm && song.bpm==140 && !strcmp(song_path,"old.song"));
    assert(tt_edit_redo(history,&doc) && !strcmp(wave.name,"New song"));
    assert(!strcmp(song_path,"new.song"));
    for(int i=0;i<35;++i) {
        e=(TtEditEvent){.type=TT_EDIT_BPM};e.bpm=100+i;
        assert(tt_edit_apply_recorded(history,&doc,&e));
    }
    for(int i=0;i<32;++i)assert(tt_edit_undo(history,&doc));
    assert(!tt_edit_can_undo(history));
    for(int i=0;i<32;++i)assert(tt_edit_redo(history,&doc));
    assert(song.bpm==134);
    tt_edit_history_free(history);
    puts("edit events, undo/redo, and sample renumbering passed");
    return 0;
}
