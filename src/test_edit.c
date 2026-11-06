#include <assert.h>
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
static char loaded_path[512],csv_path[256],message[192];
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
    strcpy(loaded_path,"one.wav");csv_path[0]=0;message[0]=0;
    doc=(TtEditDoc){.song=&song,.active=&active,.active_index=&active_index,
        .samples=samples,.active_wave=&wave,.active_phrase=&phrase,
        .sample_count=&sample_count,.sample_index=&sample_index,
        .active_loaded_path=loaded_path,.active_csv_path=csv_path,
        .msg=message,.msg_size=sizeof(message)};
}
static int apply(TtEditEvent event) {
    message[0]=0;
    return tt_edit_apply(&doc,&event);
}
int main(void) {
    setup();
    TtEditEvent e={.type=TT_EDIT_CELL_SET};
    e.cell_set=(TtEditCellSet){2,1,{60,1,TT_EFFECT_S,0x12}};
    assert(apply(e) && active.cell[2][1].note==60);
    e.cell_set.row=TT_MAX_STEPS;
    assert(!apply(e) && message[0]);
    TtCell paste[1]={{72,2,0,-1}};
    e=(TtEditEvent){.type=TT_EDIT_CELLS_BULK};
    e.cells=(TtEditCellsBulk){TT_BULK_PASTE,2,1,2,1,0,paste,1};
    assert(apply(e) && active.cell[2][1].note==72);
    e.cells.op=TT_BULK_TRANSPOSE;e.cells.delta=3;
    assert(apply(e) && active.cell[2][1].note==75);
    e.cells.op=TT_BULK_CLEAR;
    assert(apply(e) && active.cell[2][1].note==-1);
    e.cells.left=-1;assert(!apply(e));
    e.cells.left=1;e.cells.op=(TtBulkOp)99;assert(!apply(e));

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
    e.order.other=TT_SONG_ORDER;assert(!apply(e));
    e.type=TT_EDIT_ORDER_REMOVE;e.order.position=0;
    assert(apply(e) && song.order_count==old_orders);
    e.order.position=TT_SONG_ORDER;assert(!apply(e));
    e=(TtEditEvent){.type=TT_EDIT_BPM};e.bpm=400;
    assert(apply(e) && song.bpm==300);
    e=(TtEditEvent){.type=TT_EDIT_SETTINGS};
    e.settings=(TtEditSettings){120,16,4,12};
    assert(apply(e));e.settings.pattern_rows=15;assert(!apply(e));

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
    puts("edit events, rejection, and sample renumbering passed");
    return 0;
}
