#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tt_edit.h"

static void reject(TtEditDoc *doc,const char *text) {
    if(doc->msg && doc->msg_size>0)snprintf(doc->msg,doc->msg_size,"%s",text);
}
static int clamp(int value,int low,int high) {
    return value<low?low:value>high?high:value;
}
static void clear_cell(TtCell *cell) {
    *cell=(TtCell){-1,-1,TT_EFFECT_NONE,-1};
}
static int apply_cell_set(TtEditDoc *doc,const TtEditCellSet *e) {
    if(e->row<0 || e->row>=TT_MAX_STEPS || e->channel<0 || e->channel>=TT_CHANNELS) {
        reject(doc,"Cell out of range");return 0;
    }
    doc->active->cell[e->row][e->channel]=e->cell;
    return 1;
}

static int apply_cells_bulk(TtEditDoc *doc,const TtEditCellsBulk *e) {
    if(e->top<0 || e->left<0 || e->bottom>=TT_MAX_STEPS || e->right>=TT_CHANNELS ||
       e->top>e->bottom || e->left>e->right) {
        reject(doc,"Selection out of range");return 0;
    }
    if(e->op!=TT_BULK_CLEAR && e->op!=TT_BULK_PASTE &&
       e->op!=TT_BULK_TRANSPOSE) {
        reject(doc,"Unsupported selection edit");return 0;
    }
    if(e->op==TT_BULK_PASTE && (!e->data || e->data_cols<e->right-e->left+1)) {
        reject(doc,"Nothing to paste");return 0;
    }
    for(int row=e->top;row<=e->bottom;++row)
        for(int channel=e->left;channel<=e->right;++channel) {
            TtCell *cell=&doc->active->cell[row][channel];
            if(e->op==TT_BULK_CLEAR)clear_cell(cell);
            else if(e->op==TT_BULK_TRANSPOSE) {
                if(cell->note>=0)cell->note=clamp(cell->note+e->delta,12,127);
            } else {
                int r=row-e->top,c=channel-e->left;
                *cell=e->data[r*e->data_cols+c];
            }
        }
    return 1;
}

void tt_edit_clear_pattern(TtPattern *pattern) {
    for(int row=0;row<TT_MAX_STEPS;++row)for(int channel=0;channel<TT_CHANNELS;++channel)
        clear_cell(&pattern->cell[row][channel]);
}
static int apply_pattern_new(TtEditDoc *doc) {
    TtSong *song=doc->song;
    if(song->pattern_count>=TT_SONG_PATTERNS) { reject(doc,"Pattern bank is full");return 0; }
    int index=song->pattern_count++;
    tt_edit_clear_pattern(&song->pattern[index]);
    snprintf(song->name[index],TT_SONG_NAME,"Pattern %02X",index);
    return 1;
}
static int apply_pattern_append_order(TtEditDoc *doc) {
    if(doc->song->pattern_count>=TT_SONG_PATTERNS) {
        reject(doc,"Pattern bank is full");return 0;
    }
    if(doc->song->order_count>=TT_SONG_ORDER) {
        reject(doc,"Order is full");return 0;
    }
    int index=doc->song->pattern_count;
    if(!apply_pattern_new(doc))return 0;
    doc->song->order[doc->song->order_count++]=(unsigned char)index;
    return 1;
}
static int apply_pattern_rename(TtEditDoc *doc,const TtEditPattern *e) {
    if(e->index<0 || e->index>=doc->song->pattern_count) {
        reject(doc,"Pattern out of range");return 0;
    }
    if(!e->name[0]) { reject(doc,"Enter a pattern name");return 0; }
    snprintf(doc->song->name[e->index],TT_SONG_NAME,"%.63s",e->name);
    return 1;
}
static int apply_pattern_select(TtEditDoc *doc,int index) {
    if(!doc->active_index || index<0 || index>=doc->song->pattern_count) {
        reject(doc,"Pattern out of range");return 0;
    }
    if(index==*doc->active_index)return 1;
    doc->song->pattern[*doc->active_index]=*doc->active;
    *doc->active=doc->song->pattern[index];
    *doc->active_index=index;
    return 1;
}
static int apply_pattern_sync(TtEditDoc *doc) {
    if(!doc->active_index || *doc->active_index<0 ||
       *doc->active_index>=doc->song->pattern_count) {
        reject(doc,"Pattern out of range");return 0;
    }
    doc->song->pattern[*doc->active_index]=*doc->active;
    return 1;
}
static int apply_order_insert(TtEditDoc *doc,const TtEditOrder *e) {
    TtSong *song=doc->song;
    if(song->order_count>=TT_SONG_ORDER) { reject(doc,"Order is full");return 0; }
    if(e->pattern<0 || e->pattern>=song->pattern_count) {
        reject(doc,"Pattern out of range");return 0;
    }
    int at=e->position;
    if(at<0 || at>song->order_count)at=song->order_count;
    memmove(song->order+at+1,song->order+at,(size_t)(song->order_count-at));
    song->order[at]=(unsigned char)e->pattern;
    ++song->order_count;
    return 1;
}
static int apply_order_remove(TtEditDoc *doc,const TtEditOrder *e) {
    TtSong *song=doc->song;
    if(song->order_count<=1) { reject(doc,"Keep at least one order entry");return 0; }
    int at=e->position;
    if(at<0 || at>=song->order_count) { reject(doc,"Order entry out of range");return 0; }
    memmove(song->order+at,song->order+at+1,(size_t)(song->order_count-at-1));
    --song->order_count;
    return 1;
}
static int apply_order_swap(TtEditDoc *doc,const TtEditOrder *e) {
    TtSong *song=doc->song;
    int a=e->position,b=e->other;
    if(a<0 || b<0 || a>=song->order_count || b>=song->order_count) {
        reject(doc,"Order entry out of range");return 0;
    }
    unsigned char temp=song->order[a];song->order[a]=song->order[b];song->order[b]=temp;
    return 1;
}
static int apply_settings(TtEditDoc *doc,const TtEditSettings *e) {
    TtSong *song=doc->song;
    if(e->bpm<30 || e->bpm>300 || e->pattern_rows<16 || e->pattern_rows>TT_MAX_STEPS ||
       e->pattern_rows%16 || e->rows_per_beat<1 || e->rows_per_beat>16 ||
       e->ticks_per_row<1 || e->ticks_per_row>TT_MAX_TICKS_PER_ROW) {
        reject(doc,"Use BPM 30-300, pattern 16-256 by 16, beat 1-16, ticks 1-48");
        return 0;
    }
    /* Shrinking clears rows past the new length in every pattern,
       including the live copy of the active pattern. */
    if(e->pattern_rows<song->pattern_rows)
        for(int pattern=0;pattern<song->pattern_count;++pattern) {
            TtPattern *current=doc->active_index && pattern==*doc->active_index?
                doc->active:&song->pattern[pattern];
            for(int row=e->pattern_rows;row<TT_MAX_STEPS;++row)
                for(int channel=0;channel<TT_CHANNELS;++channel)
                    clear_cell(&current->cell[row][channel]);
        }
    song->bpm=e->bpm;
    song->pattern_rows=e->pattern_rows;
    song->rows_per_beat=e->rows_per_beat;
    song->ticks_per_row=e->ticks_per_row;
    return 1;
}

static int table_ok(TtEditDoc *doc,int table) {
    if(table<0 || table>=TT_SONG_TABLES) { reject(doc,"Table out of range");return 0; }
    return 1;
}
static int apply_table_tick_set(TtEditDoc *doc,const TtEditTableTickSet *e) {
    if(!table_ok(doc,e->table))return 0;
    if(e->tick<0 || e->tick>=TT_TABLE_MAX_TICKS) { reject(doc,"Tick out of range");return 0; }
    doc->song->table[e->table].tick[e->tick]=e->value;
    return 1;
}
static int apply_table_ticks_bulk(TtEditDoc *doc,const TtEditTableTicksBulk *e) {
    if(!table_ok(doc,e->table))return 0;
    if(e->top<0 || e->bottom>=TT_TABLE_MAX_TICKS || e->top>e->bottom ||
       e->left<0 || e->right>=2+TT_TABLE_EFFECTS || e->left>e->right) {
        reject(doc,"Table selection out of range");return 0;
    }
    if(e->op!=TT_BULK_PASTE && e->op!=TT_BULK_CLEAR) {
        reject(doc,"Unsupported table edit");return 0;
    }
    if(e->op==TT_BULK_PASTE && !e->data) { reject(doc,"Nothing to paste");return 0; }
    TtTable *table=&doc->song->table[e->table];
    for(int row=e->top;row<=e->bottom;++row)
        for(int field=e->left;field<=e->right;++field) {
            TtTableTick *dst=&table->tick[row];
            if(e->op==TT_BULK_CLEAR) {
                if(field==0)dst->note=-1;
                else if(field==1)dst->instrument=-1;
                else dst->effect[field-2]=(TtTableEffect){0,-1};
            } else {
                const TtTableTick *src=&e->data[row-e->top];
                if(field==0)dst->note=src->note;
                else if(field==1)dst->instrument=src->instrument;
                else dst->effect[field-2]=src->effect[field-2];
            }
        }
    return 1;
}
static int apply_table_length(TtEditDoc *doc,const TtEditTable *e) {
    if(!table_ok(doc,e->table))return 0;
    doc->song->table[e->table].length=clamp(e->length,TT_TABLE_MIN_TICKS,TT_TABLE_MAX_TICKS);
    return 1;
}
static int apply_table_name(TtEditDoc *doc,const TtEditTable *e) {
    if(!table_ok(doc,e->table))return 0;
    if(!e->name[0]) { reject(doc,"Enter a table name");return 0; }
    snprintf(doc->song->table[e->table].name,TT_SONG_NAME,"%s",e->name);
    return 1;
}

static int sample_ok(TtEditDoc *doc,int index) {
    if(!doc->samples || !doc->active_wave || !doc->active_phrase ||
       !doc->sample_count || !doc->sample_index ||
       index<0 || index>=*doc->sample_count) {
        reject(doc,"Sample out of range");return 0;
    }
    return 1;
}
static TtWave *sample_wave(TtEditDoc *doc,int index) {
    return index==*doc->sample_index?doc->active_wave:&doc->samples[index].wave;
}
static TtPhrase *sample_phrase(TtEditDoc *doc,int index) {
    return index==*doc->sample_index?doc->active_phrase:&doc->samples[index].phrase;
}
static void store_active_sample(TtEditDoc *doc) {
    TtStored *slot=&doc->samples[*doc->sample_index];
    slot->wave=*doc->active_wave;slot->phrase=*doc->active_phrase;
    snprintf(slot->loaded_path,sizeof(slot->loaded_path),"%s",doc->active_loaded_path);
    snprintf(slot->csv_path,sizeof(slot->csv_path),"%s",doc->active_csv_path);
    memset(doc->active_wave,0,sizeof(*doc->active_wave));
}
static void restore_active_sample(TtEditDoc *doc,int index) {
    TtStored *slot=&doc->samples[index];
    *doc->active_wave=slot->wave;*doc->active_phrase=slot->phrase;
    snprintf(doc->active_loaded_path,512,"%s",slot->loaded_path);
    snprintf(doc->active_csv_path,256,"%s",slot->csv_path);
    memset(slot,0,sizeof(*slot));
    *doc->sample_index=index;
}
static TtPattern *bank_pattern(TtEditDoc *doc,int index) {
    return doc->active_index && index==*doc->active_index?
        doc->active:&doc->song->pattern[index];
}
static int apply_sample(TtEditDoc *doc,TtEditType type,const TtEditSample *e) {
    if(!sample_ok(doc,e->index))return 0;
    if(type==TT_EDIT_SAMPLE_RENAME) {
        if(!e->name[0]) { reject(doc,"Enter a sample name");return 0; }
        snprintf(sample_wave(doc,e->index)->name,256,"%s",e->name);
        return 1;
    }
    if(type==TT_EDIT_SAMPLE_REFERENCE) {
        tt_phrase_set_reference_note(sample_phrase(doc,e->index),clamp(e->value,0,127));
        return 1;
    }
    if(!doc->active_loaded_path || !doc->active_csv_path) {
        reject(doc,"Sample paths unavailable");return 0;
    }
    if(type==TT_EDIT_SAMPLE_SWAP) {
        if(!sample_ok(doc,e->other))return 0;
        store_active_sample(doc);
        TtStored temp=doc->samples[e->index];
        doc->samples[e->index]=doc->samples[e->other];doc->samples[e->other]=temp;
        int selected=*doc->sample_index;
        if(selected==e->index)selected=e->other;
        else if(selected==e->other)selected=e->index;
        restore_active_sample(doc,selected);
        for(int pattern=0;pattern<doc->song->pattern_count;++pattern)
            for(int row=0;row<doc->song->pattern_rows;++row)
                for(int channel=0;channel<TT_CHANNELS;++channel) {
                    TtCell *cell=&bank_pattern(doc,pattern)->cell[row][channel];
                    if(cell->instrument==e->index+1)cell->instrument=e->other+1;
                    else if(cell->instrument==e->other+1)cell->instrument=e->index+1;
                }
        return 1;
    }
    if(*doc->sample_count<=1) { reject(doc,"Keep at least one sample");return 0; }
    store_active_sample(doc);
    if(!doc->retain_waves)tt_free(&doc->samples[e->index].wave);
    for(int i=e->index;i<*doc->sample_count-1;++i)doc->samples[i]=doc->samples[i+1];
    memset(&doc->samples[--*doc->sample_count],0,sizeof(TtStored));
    int selected=*doc->sample_index;
    if(selected==e->index)selected=clamp(e->index,0,*doc->sample_count-1);
    else if(selected>e->index)--selected;
    restore_active_sample(doc,selected);
    for(int pattern=0;pattern<doc->song->pattern_count;++pattern)
        for(int row=0;row<doc->song->pattern_rows;++row)
            for(int channel=0;channel<TT_CHANNELS;++channel) {
                TtCell *cell=&bank_pattern(doc,pattern)->cell[row][channel];
                if(cell->instrument==e->index+1)clear_cell(cell);
                else if(cell->instrument>e->index+1)--cell->instrument;
            }
    return 1;
}
static int apply_marker(TtEditDoc *doc,const TtEditMarker *e) {
    if(!doc->active_phrase || !doc->active_wave ||
       e->marker<0 || e->marker>=TT_MARKERS) {
        reject(doc,"Marker out of range");return 0;
    }
    tt_move_marker(doc->active_phrase,doc->active_wave->frames,e->marker,e->frame);
    return 1;
}
static int apply_slice(TtEditDoc *doc,const TtEditSlice *e) {
    if(!doc->active_phrase || e->slice<0 || e->slice>=TT_SLICES ||
       e->note < -1 || e->note>127) {
        reject(doc,"Slice note out of range");return 0;
    }
    tt_slice_set_note(doc->active_phrase,e->slice,e->note);
    return 1;
}
static int apply_sample_select(TtEditDoc *doc,const TtEditSample *e) {
    if(!sample_ok(doc,e->index) || !doc->active_loaded_path || !doc->active_csv_path)return 0;
    if(e->index==*doc->sample_index)return 1;
    store_active_sample(doc);
    restore_active_sample(doc,e->index);
    return 1;
}
static int apply_sample_append(TtEditDoc *doc,const TtEditLoadedWave *e) {
    if(!doc->sample_count || !doc->sample_index || !doc->samples ||
       !doc->active_wave || !doc->active_phrase || !doc->active_loaded_path ||
       !doc->active_csv_path || !e->wave || !e->wave->pcm || !e->path) {
        reject(doc,"Invalid WAV");return 0;
    }
    if(*doc->sample_count>=TT_SONG_SAMPLES) {
        reject(doc,"Sample slots full; restart to load a different set of WAVs");return 0;
    }
    store_active_sample(doc);
    *doc->active_wave=*e->wave;
    memset(e->wave,0,sizeof(*e->wave));
    tt_phrase_init(doc->active_phrase,doc->active_wave->frames);
    *doc->sample_index=(*doc->sample_count)++;
    snprintf(doc->active_loaded_path,512,"%s",e->path);
    doc->active_csv_path[0]=0;
    return 1;
}
static int apply_sample_recover(TtEditDoc *doc,const TtEditLoadedWave *e) {
    if(!sample_ok(doc,e->index) || !e->wave || !e->wave->pcm || !e->path ||
       !doc->active_loaded_path)return 0;
    TtWave *wave=sample_wave(doc,e->index);
    if(wave->pcm || wave->frames!=e->wave->frames || wave->rate!=e->wave->rate) {
        reject(doc,"WAV mismatch");return 0;
    }
    snprintf(e->wave->name,sizeof(e->wave->name),"%s",wave->name);
    snprintf(e->wave->source_name,sizeof(e->wave->source_name),"%s",wave->source_name);
    *wave=*e->wave;
    memset(e->wave,0,sizeof(*e->wave));
    char *path=e->index==*doc->sample_index?
        doc->active_loaded_path:doc->samples[e->index].loaded_path;
    snprintf(path,512,"%s",e->path);
    return 1;
}
static int apply_document_replace(TtEditDoc *doc,const TtEditDocumentReplace *e) {
    if(!e->song || !e->samples || e->count<1 || e->count>TT_SONG_SAMPLES ||
       !doc->sample_count || !doc->sample_index || !doc->samples ||
       !doc->active_wave || !doc->active_phrase || !doc->active_index ||
       !doc->active_loaded_path || !doc->active_csv_path) {
        reject(doc,"Invalid song document");return 0;
    }
    if(!doc->retain_waves) {
        tt_free(doc->active_wave);
        for(int i=0;i<*doc->sample_count;++i)tt_free(&doc->samples[i].wave);
    }
    memset(doc->samples,0,TT_SONG_SAMPLES*sizeof(TtStored));
    *doc->song=*e->song;
    *doc->active_index=0;
    *doc->active=e->song->pattern[0];
    *doc->sample_count=e->count;*doc->sample_index=0;
    *doc->active_wave=e->samples[0].wave;
    *doc->active_phrase=e->samples[0].phrase;
    snprintf(doc->active_loaded_path,512,"%s",e->samples[0].loaded_path);
    doc->active_csv_path[0]=0;
    for(int i=1;i<e->count;++i)doc->samples[i]=e->samples[i];
    memset(e->samples,0,(size_t)e->count*sizeof(TtStored));
    return 1;
}

int tt_edit_apply(TtEditDoc *doc,const TtEditEvent *event) {
    if(!doc || !event || !doc->active || !doc->song)return 0;
    switch(event->type) {
    case TT_EDIT_CELL_SET:
        return apply_cell_set(doc,&event->cell_set);
    case TT_EDIT_CELLS_BULK:
        return apply_cells_bulk(doc,&event->cells);
    case TT_EDIT_PATTERN_NEW:
        return apply_pattern_new(doc);
    case TT_EDIT_PATTERN_APPEND_ORDER:
        return apply_pattern_append_order(doc);
    case TT_EDIT_PATTERN_RENAME:
        return apply_pattern_rename(doc,&event->pattern);
    case TT_EDIT_PATTERN_SELECT:
        return apply_pattern_select(doc,event->pattern.index);
    case TT_EDIT_PATTERN_SYNC:
        return apply_pattern_sync(doc);
    case TT_EDIT_ORDER_INSERT:
        return apply_order_insert(doc,&event->order);
    case TT_EDIT_ORDER_REMOVE:
        return apply_order_remove(doc,&event->order);
    case TT_EDIT_ORDER_SWAP:
        return apply_order_swap(doc,&event->order);
    case TT_EDIT_BPM:
        doc->song->bpm=clamp(event->bpm,30,300);
        return 1;
    case TT_EDIT_SETTINGS:
        return apply_settings(doc,&event->settings);
    case TT_EDIT_TABLE_TICK_SET:
        return apply_table_tick_set(doc,&event->table_tick);
    case TT_EDIT_TABLE_TICKS_BULK:
        return apply_table_ticks_bulk(doc,&event->table_ticks);
    case TT_EDIT_TABLE_LENGTH:
        return apply_table_length(doc,&event->table);
    case TT_EDIT_TABLE_NAME:
        return apply_table_name(doc,&event->table);
    case TT_EDIT_SAMPLE_RENAME:case TT_EDIT_SAMPLE_REFERENCE:
    case TT_EDIT_SAMPLE_SWAP:case TT_EDIT_SAMPLE_DELETE:
        return apply_sample(doc,event->type,&event->sample);
    case TT_EDIT_MARKER_MOVE:
        return apply_marker(doc,&event->marker);
    case TT_EDIT_SLICE_NOTE:
        return apply_slice(doc,&event->slice);
    case TT_EDIT_SAMPLE_SELECT:
        return apply_sample_select(doc,&event->sample);
    case TT_EDIT_SAMPLE_APPEND:
        return apply_sample_append(doc,&event->loaded_wave);
    case TT_EDIT_SAMPLE_RECOVER:
        return apply_sample_recover(doc,&event->loaded_wave);
    case TT_EDIT_DOCUMENT_REPLACE:
        return apply_document_replace(doc,&event->replacement);
    default:
        reject(doc,"Unknown edit");
        return 0;
    }
}

/* Snapshots copy the document metadata and pattern bank. Wave PCM and peaks are
   kept once in an asset pool, so a one-cell edit does not copy audio buffers. */
enum { TT_EDIT_HISTORY_LIMIT=32 };
typedef struct {
    TtSong song;
    TtPattern active;
    TtStored samples[TT_SONG_SAMPLES];
    TtWave wave;
    TtPhrase phrase;
    int active_index,sample_count,sample_index,restore_song_path;
    char loaded_path[512],csv_path[256],song_path[512];
} TtEditSnapshot;
struct TtEditHistory {
    TtEditSnapshot *undo[TT_EDIT_HISTORY_LIMIT],*redo[TT_EDIT_HISTORY_LIMIT];
    int undo_count,redo_count;
    TtWave *assets;
    size_t asset_count,asset_capacity;
};
static TtEditSnapshot *snapshot_new(const TtEditDoc *doc) {
    if(!doc || !doc->song || !doc->active || !doc->active_index ||
       !doc->samples || !doc->active_wave || !doc->active_phrase ||
       !doc->sample_count || !doc->sample_index ||
       !doc->active_loaded_path || !doc->active_csv_path)return NULL;
    TtEditSnapshot *s=malloc(sizeof(*s));
    if(!s)return NULL;
    s->song=*doc->song;s->active=*doc->active;
    memcpy(s->samples,doc->samples,sizeof(s->samples));
    s->wave=*doc->active_wave;s->phrase=*doc->active_phrase;
    s->active_index=*doc->active_index;
    s->sample_count=*doc->sample_count;s->sample_index=*doc->sample_index;
    s->restore_song_path=0;
    snprintf(s->loaded_path,sizeof(s->loaded_path),"%s",doc->active_loaded_path);
    snprintf(s->csv_path,sizeof(s->csv_path),"%s",doc->active_csv_path);
    snprintf(s->song_path,sizeof(s->song_path),"%s",doc->song_path?doc->song_path:"");
    return s;
}
static void snapshot_restore(TtEditDoc *doc,const TtEditSnapshot *s) {
    *doc->song=s->song;*doc->active=s->active;
    memcpy(doc->samples,s->samples,sizeof(s->samples));
    *doc->active_wave=s->wave;*doc->active_phrase=s->phrase;
    *doc->active_index=s->active_index;
    *doc->sample_count=s->sample_count;*doc->sample_index=s->sample_index;
    snprintf(doc->active_loaded_path,512,"%s",s->loaded_path);
    snprintf(doc->active_csv_path,256,"%s",s->csv_path);
    if(s->restore_song_path && doc->song_path)
        snprintf(doc->song_path,512,"%s",s->song_path);
}
static void stack_clear(TtEditSnapshot **stack,int *count) {
    for(int i=0;i<*count;++i)free(stack[i]);
    *count=0;
}
static void stack_push(TtEditSnapshot **stack,int *count,TtEditSnapshot *s) {
    if(*count==TT_EDIT_HISTORY_LIMIT) {
        free(stack[0]);
        memmove(stack,stack+1,(TT_EDIT_HISTORY_LIMIT-1)*sizeof(*stack));
        --*count;
    }
    stack[(*count)++]=s;
}
static int assets_reserve(TtEditHistory *history,size_t additional) {
    size_t needed=history->asset_count+additional;
    if(needed<=history->asset_capacity)return 1;
    size_t capacity=history->asset_capacity?history->asset_capacity*2:16;
    while(capacity<needed)capacity*=2;
    TtWave *assets=realloc(history->assets,capacity*sizeof(*assets));
    if(!assets)return 0;
    history->assets=assets;history->asset_capacity=capacity;
    return 1;
}
static void asset_register(TtEditHistory *history,const TtWave *wave) {
    if(!wave->pcm && !wave->peaks)return;
    for(size_t i=0;i<history->asset_count;++i)
        if(history->assets[i].pcm==wave->pcm &&
           history->assets[i].peaks==wave->peaks)return;
    history->assets[history->asset_count++]=*wave;
}
static void assets_register_document(TtEditHistory *history,const TtEditDoc *doc) {
    asset_register(history,doc->active_wave);
    for(int i=0;i<*doc->sample_count;++i)
        if(i!=*doc->sample_index)asset_register(history,&doc->samples[i].wave);
}
static int same_asset(const TtWave *a,const TtWave *b) {
    return a->pcm==b->pcm && a->peaks==b->peaks;
}
static int snapshot_uses_asset(const TtEditSnapshot *s,const TtWave *asset) {
    if(same_asset(&s->wave,asset))return 1;
    for(int i=0;i<s->sample_count;++i)
        if(same_asset(&s->samples[i].wave,asset))return 1;
    return 0;
}
static int document_uses_asset(const TtEditDoc *doc,const TtWave *asset) {
    if(same_asset(doc->active_wave,asset))return 1;
    for(int i=0;i<*doc->sample_count;++i)
        if(same_asset(&doc->samples[i].wave,asset))return 1;
    return 0;
}
static void assets_collect(TtEditHistory *history,const TtEditDoc *doc) {
    for(size_t i=0;i<history->asset_count;) {
        TtWave *asset=&history->assets[i];
        int used=document_uses_asset(doc,asset);
        for(int j=0;!used && j<history->undo_count;++j)
            used=snapshot_uses_asset(history->undo[j],asset);
        for(int j=0;!used && j<history->redo_count;++j)
            used=snapshot_uses_asset(history->redo[j],asset);
        if(used) { ++i;continue; }
        tt_free(asset);
        --history->asset_count;
        memmove(asset,asset+1,(history->asset_count-i)*sizeof(*asset));
    }
}
TtEditHistory *tt_edit_history_new(const TtEditDoc *doc) {
    TtEditHistory *history=calloc(1,sizeof(*history));
    if(!history)return NULL;
    if(!doc || !doc->sample_count || !assets_reserve(history,TT_SONG_SAMPLES)) {
        free(history->assets);free(history);return NULL;
    }
    assets_register_document(history,doc);
    return history;
}
void tt_edit_history_free(TtEditHistory *history) {
    if(!history)return;
    stack_clear(history->undo,&history->undo_count);
    stack_clear(history->redo,&history->redo_count);
    for(size_t i=0;i<history->asset_count;++i)tt_free(&history->assets[i]);
    free(history->assets);free(history);
}
static int recordable(TtEditType type) {
    return type!=TT_EDIT_NONE && type!=TT_EDIT_PATTERN_SELECT &&
           type!=TT_EDIT_PATTERN_SYNC && type!=TT_EDIT_SAMPLE_SELECT;
}
int tt_edit_apply_recorded(TtEditHistory *history,TtEditDoc *doc,
                           const TtEditEvent *event) {
    if(!history)return tt_edit_apply(doc,event);
    if(!doc || !event)return 0;
    TtEditSnapshot *before=NULL;
    if(recordable(event->type)) {
        before=snapshot_new(doc);
        if(!before) { reject(doc,"Undo history unavailable");return 0; }
        before->restore_song_path=event->type==TT_EDIT_DOCUMENT_REPLACE;
    }
    if(!assets_reserve(history,TT_SONG_SAMPLES)) {
        free(before);reject(doc,"Undo history unavailable");return 0;
    }
    int old_retain=doc->retain_waves;
    doc->retain_waves=1;
    int applied=tt_edit_apply(doc,event);
    doc->retain_waves=old_retain;
    if(!applied) { free(before);return 0; }
    assets_register_document(history,doc);
    if(before) {
        stack_push(history->undo,&history->undo_count,before);
        stack_clear(history->redo,&history->redo_count);
    }
    assets_collect(history,doc);
    return 1;
}
int tt_edit_can_undo(const TtEditHistory *history) {
    return history && history->undo_count>0;
}
int tt_edit_can_redo(const TtEditHistory *history) {
    return history && history->redo_count>0;
}
static int history_step(TtEditHistory *history,TtEditDoc *doc,int redo) {
    if(!history || !(redo?history->redo_count:history->undo_count)) {
        reject(doc,redo?"Nothing to redo":"Nothing to undo");return 0;
    }
    TtEditSnapshot *current=snapshot_new(doc);
    if(!current) { reject(doc,"Undo history unavailable");return 0; }
    TtEditSnapshot **source=redo?history->redo:history->undo;
    int *source_count=redo?&history->redo_count:&history->undo_count;
    TtEditSnapshot *prior=source[--*source_count];
    current->restore_song_path=prior->restore_song_path;
    stack_push(redo?history->undo:history->redo,
               redo?&history->undo_count:&history->redo_count,current);
    snapshot_restore(doc,prior);
    free(prior);
    assets_collect(history,doc);
    return 1;
}
int tt_edit_undo(TtEditHistory *history,TtEditDoc *doc) {
    return history_step(history,doc,0);
}
int tt_edit_redo(TtEditHistory *history,TtEditDoc *doc) {
    return history_step(history,doc,1);
}
