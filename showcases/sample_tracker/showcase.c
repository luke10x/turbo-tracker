#include <curses.h>
#include <locale.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "f80.h"
#include "f80_components.h"
#include "f80_curses.h"
#ifndef __EMSCRIPTEN__
#include "f80_file_dialog.h"
#endif
#include "audio.h"
#include "csv.h"
#include "pattern.h"
#include "sample.h"
#include "song.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, choose_web_wav, (), {
    if(window.f80ChooseWav)window.f80ChooseWav();
});
EM_JS(void, choose_web_song, (), {
    if(window.f80ChooseSong)window.f80ChooseSong();
});
EM_JS(void, download_web_song, (const char *name), {
    if(window.f80DownloadSong)window.f80DownloadSong(UTF8ToString(name));
});
EM_JS(void, sample_web_grid_rect, (int x,int y,int width,int height), {
    Module.f80GridRect = {x,y,width,height};
});
EM_JS(void, sample_web_editable_grid_rect, (int x,int y,int width,int height), {
    Module.f80EditableGridRect = {x,y,width,height};
});
EM_JS(void, sample_web_wave_rect, (int x,int y,int width,int height), {
    Module.f80WaveRect = {x,y,width,height};
});
EM_JS(void, sample_web_order_rect, (int x,int y,int width,int height), {
    Module.f80OrderRect = {x,y,width,height};
});
EM_JS(void, sample_web_table_rect, (int x,int y,int width,int height,int horizontal,int vertical), {
    Module.f80TableRect = {x,y,width,height,horizontal,vertical};
});
EM_JS(void, sample_web_wrap_rect, (int x,int y,int width,int height), {
    Module.f80WrapRect = {x,y,width,height};
});
EM_JS(int, sample_web_compact_editing, (), {
    return !!window.f80CompactEditing;
});
#endif

enum { FOCUS_WAVE, FOCUS_PHRASE, FOCUS_REFERENCE };
enum { PLAY_SONG, PLAY_PATTERN };
enum { PATH_WAV, PATH_SONG_LOAD, PATH_SONG_SAVE };
enum { SAMPLE_HISTORY_MAX=8 };
enum { SONG_LOOKAHEAD_FRAMES=4096, SONG_DISPLAY_ROWS=64 };
typedef struct {
    SampleWave wave;
    SamplePhrase phrase;
    char loaded_path[512],csv_path[256];
} StoredSample;
typedef struct {
    int active,table,tick,next_table,delay,pending;
    int delayed_glide;
    SampleCell delayed_note;
} TableVoice;
typedef struct {
    Uint64 frame;
    int row,pattern,order;
} SongDisplayRow;
typedef struct {
    F80App app;
    SampleWave wave;
    SamplePhrase phrase;
    SamplePattern pattern;
    SampleSong song;
    F80Widget *root,*wave_row,*order_frame,*order_list,*wave_frame,*wave_canvas;
    F80Widget *phrase_frame,*phrase_canvas;
    F80Widget *header,*header_title,*play_button,*stop_button,*follow_button,*mode_button;
    F80Widget *progress,*bpm_label,*details,*status;
    int focus,marker,zoom,view_start,step,channel,field,entry_octave;
    int selecting,anchor_step,anchor_channel,clip_rows,clip_channels;
    SampleCell clipboard[SAMPLE_MAX_STEPS][SAMPLE_CHANNELS];
    int phrase_scroll,field_scroll,path_action,edit_order,reveal_cursor;
    int compact_editing;
    int playing,started,follow,play_step,play_order,active_step,active_pattern_index;
    int active_order_index,loop,pattern_index,play_mode,loop_pattern_index;
    Uint64 next_audition_tick_at;
    int audition_effect_active;
    int tick_phase,tick_remainder;
    int frames_to_tick,song_finished,rendering_song;
    Uint64 song_written_frames;
    SongDisplayRow display_rows[SONG_DISPLAY_ROWS];
    int display_head,display_tail;
    TableVoice table_voice[SAMPLE_CHANNELS];
    F80Widget *table_dialog,*table_panel,*table_canvas,*table_name,*table_name_label;
    F80Widget *table_scrollbar,*table_buttons[5];
    F80Grid table_grid;
    F80Wrap table_actions;
    int table_index;
    SampleTableTick table_clip[SAMPLE_TABLE_MAX_TICKS];
    int table_clip_rows,table_clip_left,table_clip_right;
    char table_title[32];
    char header_text[256],bpm_text[16],details_text[256],wave_title[160],phrase_title[96];
    char message[192],loaded_path[512],csv_path[256],song_path[512];
    StoredSample samples[SAMPLE_HISTORY_MAX];
    int sample_count,sample_index;
    F80Widget *manage_dialog,*manage_list_bg,*manage_list,*manage_name,*manage_info;
    F80Widget *slices_dialog,*slices_list_bg,*slices_list,*slices_note,*slices_info;
    F80Widget *order_dialog,*order_bank_label,*order_bank_bg,*order_bank;
    F80Widget *order_seq_label,*order_seq_bg,*order_seq,*order_name;
    F80Widget *settings_dialog,*settings_label[4],*settings_input[4];
    F80Widget *settings_apply,*settings_close;
    char bank_rows[SAMPLE_SONG_PATTERNS][100],order_rows[SAMPLE_SONG_ORDER][100];
    char order_panel_rows[SAMPLE_SONG_ORDER][8];
    char order_heading[64];
    const char *bank_items[SAMPLE_SONG_PATTERNS],*order_items[SAMPLE_SONG_ORDER];
    const char *order_panel_items[SAMPLE_SONG_ORDER];
    char manage_rows[SAMPLE_HISTORY_MAX][290],slice_rows[SAMPLE_SLICES][96];
    const char *manage_items[SAMPLE_HISTORY_MAX],*slice_items[SAMPLE_SLICES];
    char manage_details[320],slice_details[320];
    int delete_armed;
} SampleTracker;
static SampleTracker tracker;
#ifndef __EMSCRIPTEN__
static F80FileDialog file_dialog;
#endif

static int clamp(int value,int low,int high) {
    return value<low?low:value>high?high:value;
}
static void message(const char *text) {
    snprintf(tracker.message,sizeof(tracker.message),"%s",text);
}
static const SamplePattern *playing_pattern(void) {
    if(tracker.play_mode==PLAY_PATTERN)
        return tracker.loop_pattern_index==tracker.pattern_index?
               &tracker.pattern:&tracker.song.pattern[tracker.loop_pattern_index];
    int index=tracker.song.order[tracker.play_order];
    return index==tracker.pattern_index?&tracker.pattern:&tracker.song.pattern[index];
}
static int order_for_pattern(int pattern,int near) {
    int found=-1,best=SAMPLE_SONG_ORDER+1;
    for(int i=0;i<tracker.song.order_count;++i)if(tracker.song.order[i]==pattern) {
        int distance=abs(i-near);
        if(distance<best) { found=i;best=distance; }
    }
    return found;
}
static int grid_total_rows(void) {
    return tracker.edit_order>=0?tracker.song.order_count*tracker.song.pattern_rows:tracker.song.pattern_rows;
}
static int grid_edit_row(void) {
    return (tracker.edit_order>=0?tracker.edit_order*tracker.song.pattern_rows:0)+tracker.step;
}
static int grid_order_for_row(int global) {
    return tracker.edit_order>=0?global/tracker.song.pattern_rows:-1;
}
static int grid_pattern_for_row(int global) {
    int order=grid_order_for_row(global);
    return order>=0?tracker.song.order[order]:tracker.pattern_index;
}
static void select_edit_pattern(int index) {
    if(index==tracker.pattern_index)return;
    tracker.song.pattern[tracker.pattern_index]=tracker.pattern;
    tracker.pattern=tracker.song.pattern[index];
    tracker.pattern_index=index;
    tracker.edit_order=order_for_pattern(index,tracker.edit_order);
    tracker.phrase_scroll=tracker.edit_order>=0?
        tracker.edit_order*tracker.song.pattern_rows:0;
    tracker.reveal_cursor=1;
    tracker.selecting=0;
}
static void selection_bounds(int *top,int *left,int *bottom,int *right) {
    *top=tracker.anchor_step<tracker.step?tracker.anchor_step:tracker.step;
    *bottom=tracker.anchor_step>tracker.step?tracker.anchor_step:tracker.step;
    *left=tracker.anchor_channel<tracker.channel?tracker.anchor_channel:tracker.channel;
    *right=tracker.anchor_channel>tracker.channel?tracker.anchor_channel:tracker.channel;
}
static int selected_cell(int row,int channel) {
    if(!tracker.selecting || tracker.focus!=FOCUS_PHRASE)return 0;
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    return row>=top && row<=bottom && channel>=left && channel<=right;
}
static int pattern_is_empty(const SamplePattern *pattern) {
    for(int row=0;row<tracker.song.pattern_rows;++row)
        for(int channel=0;channel<SAMPLE_CHANNELS;++channel) {
            const SampleCell *cell=&pattern->cell[row][channel];
            if(cell->note>=0 || cell->effect!=SAMPLE_EFFECT_NONE)return 0;
        }
    return 1;
}
static int visible_frames(void) {
    int count=(tracker.wave.frames+((1<<tracker.zoom)-1))>>tracker.zoom;
    if(count<32)count=32;
    if(count>tracker.wave.frames)count=tracker.wave.frames;
    return count;
}
static void clamp_view(void) {
    int maximum=tracker.wave.frames-visible_frames();
    tracker.view_start=clamp(tracker.view_start,0,maximum);
}
static void center_on_marker(void) {
    tracker.view_start=tracker.phrase.marker[tracker.marker]-visible_frames()/2;
    clamp_view();
}
static void focus_marker(int marker) {
    tracker.marker=clamp(marker,0,SAMPLE_MARKERS-1);
    center_on_marker();
}
static void zoom_wave(int direction) {
    int next=tracker.zoom+direction;
    if(next>=0 && next<=16 && (tracker.wave.frames>>next)>=32)
        tracker.zoom=next;
    center_on_marker();
}
static void pan_wave(int direction) {
    int shift=visible_frames()/4;
    if(shift<1)shift=1;
    tracker.view_start+=direction*shift;
    clamp_view();
}
static void stop_playback(F80Widget *button,void *user) {
    (void)button;(void)user;
    tracker.playing=tracker.started=0;tracker.play_step=tracker.play_order=0;
    tracker.active_step=-1;
    tracker.active_pattern_index=tracker.active_order_index=-1;
    tracker.song_finished=tracker.frames_to_tick=0;
    tracker.display_head=tracker.display_tail=0;
    sample_audio_stop();
    memset(tracker.table_voice,0,sizeof(tracker.table_voice));
    message("Stopped");
}
static void start_playback(F80Widget *button,void *user);
static void order_panel_selected(F80Widget *list,void *user) {
    (void)user;
    int at=clamp(list->cursor,0,tracker.song.order_count-1);
    tracker.play_mode=PLAY_SONG;
    tracker.play_order=at;
    tracker.play_step=0;
    if(tracker.playing)start_playback(NULL,NULL);
    char status[64];
    snprintf(status,sizeof(status),"Order %02X plays pattern %02X",
             at,tracker.song.order[at]);
    message(status);
}
#ifdef __EMSCRIPTEN__
static void order_panel_scroll(int rows) {
    if(!tracker.order_list || tracker.app.modal || !rows)return;
    int next=clamp(tracker.order_list->cursor+rows,0,tracker.song.order_count-1);
    if(next!=tracker.order_list->cursor) {
        tracker.order_list->cursor=next;
        order_panel_selected(tracker.order_list,NULL);
    }
}
#endif
static const SampleWave *instrument_wave(const SampleCell *cell, const SamplePhrase **phrase) {
    int index=cell->instrument==0?tracker.sample_index:cell->instrument-1;
    if(index<0 || index>=tracker.sample_count)return NULL;
    if(index==tracker.sample_index) { *phrase=&tracker.phrase;return tracker.wave.pcm?&tracker.wave:NULL; }
    *phrase=&tracker.samples[index].phrase;
    return tracker.samples[index].wave.pcm?&tracker.samples[index].wave:NULL;
}
static int cell_reference_note(const SamplePhrase *phrase,const SampleCell *cell) {
    if(cell->effect!=SAMPLE_EFFECT_S || cell->value<0)return phrase->reference_note;
    int slice=cell->value>>4;
    return slice<15?sample_slice_base_note(phrase,slice):phrase->reference_note;
}
static void audition_cell(int step,int channel) {
    int first,last;
    SampleCell *cell=&tracker.pattern.cell[step][channel];
    const SamplePhrase *phrase=NULL;
    const SampleWave *wave=instrument_wave(cell,&phrase);
    if(!wave || !sample_cell_range(phrase,cell,wave->frames,&first,&last)) {
        sample_audio_stop();
        message("Set a loaded instrument, note, and playable effect value first");
        return;
    }
    int base=cell_reference_note(phrase,cell);
    int glided=cell->effect==SAMPLE_EFFECT_G && cell->value>0 &&
               sample_audio_glide_to(0,cell->note,base,cell->value);
    if(!glided && !sample_audio_play(wave,first,last,cell->note,base)) {
        message("Audio unavailable: check the output device or browser permission");
        return;
    }
    tracker.audition_effect_active=0;
    if(cell->effect==SAMPLE_EFFECT_M && cell->value>=0)
        sample_audio_set_volume(0,cell->value);
    else if(cell->effect==SAMPLE_EFFECT_STRETCH && cell->value>=0)
        sample_audio_set_stretch(0,cell->value);
    else if(cell->effect && cell->value>=0 &&
            strchr("GIJKNOPRVWr",cell->effect)) {
        sample_audio_command(0,(char)cell->effect,cell->value);
        tracker.audition_effect_active=1;
        tracker.next_audition_tick_at=SDL_GetTicks();
    }
    tracker.active_step=step;
    tracker.active_pattern_index=tracker.pattern_index;
}
static void play_row(int row) {
    if(!tracker.rendering_song && tracker.play_mode==PLAY_SONG &&
       tracker.follow && tracker.order_list)
        tracker.order_list->cursor=tracker.play_order;
    const SamplePattern *pattern=playing_pattern();
    int pattern_index=tracker.play_mode==PLAY_PATTERN?tracker.loop_pattern_index:
                      tracker.song.order[tracker.play_order];
    if(row==0) {
        char info[192];
        if(tracker.play_mode==PLAY_PATTERN)
            snprintf(info,sizeof(info),"Looping pattern %02X: %.90s%s",
                     pattern_index,tracker.song.name[pattern_index],
                     pattern_is_empty(pattern)?" [empty]":"");
        else
            snprintf(info,sizeof(info),"Order %02X/%02X: %.90s%s",
                     tracker.play_order,tracker.song.order_count-1,
                     tracker.song.name[pattern_index],
                     pattern_is_empty(pattern)?" [empty]":"");
        message(info);
    }
    for(int channel=0;channel<SAMPLE_CHANNELS;++channel) {
        const SampleCell *cell=&pattern->cell[row][channel];
        if(cell->effect==SAMPLE_EFFECT_T && cell->value>=0 &&
           cell->value<SAMPLE_SONG_TABLES) {
            TableVoice *voice=&tracker.table_voice[channel];
            voice->active=1;voice->table=cell->value;voice->tick=0;
            voice->next_table=-1;voice->pending=0;
        }
        if(cell->effect==SAMPLE_EFFECT_M && cell->value>=0)
            sample_audio_set_volume(channel,cell->value);
        else if(cell->effect && cell->value>=0 && strchr("GIJKNOPRVWr",cell->effect))
            sample_audio_command(channel,(char)cell->effect,cell->value);
        int first,last;
        const SamplePhrase *phrase=NULL;
        const SampleWave *wave=instrument_wave(cell,&phrase);
        if(!wave || !sample_cell_range(phrase,cell,wave->frames,&first,&last))continue;
        int base=cell_reference_note(phrase,cell);
        if(cell->effect==SAMPLE_EFFECT_G && cell->value>0 &&
           sample_audio_glide_to(channel,cell->note,base,cell->value))continue;
        if(!sample_audio_play_channel(channel,wave,first,last,cell->note,base))
            message("Audio unavailable: check the output device or browser permission");
    }
    if(tracker.rendering_song) {
        int next=(tracker.display_tail+1)%SONG_DISPLAY_ROWS;
        if(next==tracker.display_head)
            tracker.display_head=(tracker.display_head+1)%SONG_DISPLAY_ROWS;
        tracker.display_rows[tracker.display_tail]=(SongDisplayRow){
            tracker.song_written_frames,row,pattern_index,
            tracker.play_mode==PLAY_SONG?tracker.play_order:-1};
        tracker.display_tail=next;
    } else {
        tracker.active_step=row;
        tracker.active_pattern_index=pattern_index;
        tracker.active_order_index=tracker.play_mode==PLAY_SONG?tracker.play_order:-1;
    }
}
static void table_play_note(int channel,const SampleCell *cell,int glide_rate) {
    const SamplePhrase *phrase=NULL;
    const SampleWave *wave=instrument_wave(cell,&phrase);
    int first,last;
    if(!wave || !sample_cell_range(phrase,cell,wave->frames,&first,&last))return;
    int base=cell_reference_note(phrase,cell);
    if(glide_rate>0 && sample_audio_glide_to(channel,cell->note,base,glide_rate))return;
    sample_audio_play_channel(channel,wave,first,last,cell->note,base);
}
static void play_table_ticks(void) {
    for(int channel=0;channel<SAMPLE_CHANNELS;++channel) {
        TableVoice *voice=&tracker.table_voice[channel];
        if(voice->pending && --voice->delay<=0) {
            table_play_note(channel,&voice->delayed_note,voice->delayed_glide);
            voice->pending=0;
        }
        if(!voice->active)continue;
        const SampleTable *table=&tracker.song.table[voice->table];
        if(voice->tick>=table->length) {
            if(voice->next_table>=0) {
                voice->table=voice->next_table;voice->next_table=-1;
                voice->tick=0;table=&tracker.song.table[voice->table];
            } else { voice->active=0;continue; }
        }
        const SampleTableTick *entry=&table->tick[voice->tick++];
        SampleCell note={entry->note,entry->instrument,SAMPLE_EFFECT_NONE,-1};
        int delay=0,glide_rate=0,jump_table=-1,jump_tick=-1;
        for(int fx=0;fx<SAMPLE_TABLE_EFFECTS;++fx) {
            const SampleTableEffect *effect=&entry->effect[fx];
            if(!effect->command || effect->value<0)continue;
            switch(effect->command) {
            case 'M':sample_audio_set_volume(channel,effect->value);break;
            case 'S':note.effect=SAMPLE_EFFECT_S;note.value=effect->value;break;
            case 'U':delay=effect->value;break;
            case 'G':glide_rate=effect->value;
                sample_audio_command(channel,'G',effect->value);break;
            case 'I':case 'J':case 'K':case 'N':case 'O':
            case 'P':case 'R':case 'V':case 'W':case 'r':
                sample_audio_command(channel,effect->command,effect->value);break;
            case 'T':
                if(effect->value<SAMPLE_SONG_TABLES)
                    voice->next_table=effect->value;
                break;
            case 'X':
                if(effect->value<SAMPLE_SONG_TABLES)jump_table=effect->value;
                break;
            case 'Y':jump_tick=effect->value;break;
            default:break;
            }
        }
        if(note.note>=0 && note.instrument>=0) {
            voice->pending=0;
            if(delay) {
                voice->delayed_note=note;voice->delayed_glide=glide_rate;
                voice->delay=delay;voice->pending=1;
            } else table_play_note(channel,&note,glide_rate);
        }
        if(jump_table>=0) {
            voice->table=jump_table;voice->tick=0;voice->next_table=-1;
        } else if(jump_tick>=0 && jump_tick<table->length)voice->tick=jump_tick;
    }
}
static void audition(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.focus==FOCUS_WAVE) {
        int marker=tracker.marker;
        int first=tracker.phrase.marker[marker];
        int last=marker<15?tracker.phrase.marker[marker+1]:tracker.wave.frames;
        if(last<=first) { message("Move the next marker to make a slice");return; }
        sample_audio_set_volume(0,255);
        int base=sample_slice_base_note(&tracker.phrase,marker<15?marker:14);
        if(!sample_audio_play(&tracker.wave,first,last,base,base))
            message("Audio unavailable");
        else message("Audition marker slice");
    } else audition_cell(tracker.step,tracker.channel);
}
static void start_playback(F80Widget *button,void *user) {
    (void)button;(void)user;
    sample_audio_stop();
    tracker.playing=tracker.started=0;
    if(!sample_audio_begin_song()) {
        message("Audio unavailable: check the output device or browser permission");
        return;
    }
    for(int channel=0;channel<SAMPLE_CHANNELS;++channel)
        sample_audio_set_volume(channel,255);
    tracker.playing=tracker.started=1;
    tracker.audition_effect_active=0;
    memset(tracker.table_voice,0,sizeof(tracker.table_voice));
    tracker.tick_phase=tracker.tick_remainder=tracker.frames_to_tick=0;
    tracker.song_written_frames=0;
    tracker.display_head=tracker.display_tail=0;
    tracker.song_finished=0;
    tracker.play_order=tracker.order_list?tracker.order_list->cursor:0;
    tracker.play_step=0;
    if(tracker.play_mode==PLAY_PATTERN)tracker.loop_pattern_index=tracker.pattern_index;
    tracker.active_step=tracker.active_pattern_index=tracker.active_order_index=-1;
    message(tracker.play_mode==PLAY_PATTERN?"Looping current pattern":"Playing song order");
}
static void start_song(F80Widget *button,void *user) {
    tracker.play_mode=PLAY_SONG;start_playback(button,user);
}
static void start_pattern_loop(F80Widget *button,void *user) {
    tracker.play_mode=PLAY_PATTERN;start_playback(button,user);
}
static void toggle_play(F80Widget *button,void *user) {
    if(!tracker.started) { start_playback(button,user);return; }
    if(tracker.playing) {
        tracker.playing=0;sample_audio_stop();message("Paused");
    } else {
        if(tracker.song_finished) { start_playback(button,user);return; }
        if(!sample_audio_begin_song()) {
            message("Audio unavailable: check the output device or browser permission");
            return;
        }
        tracker.playing=1;
        tracker.frames_to_tick=0;
        tracker.song_written_frames=0;
        tracker.display_head=tracker.display_tail=0;
        message("Resumed");
    }
}
static int next_tick_frames(int divisor) {
    tracker.tick_remainder+=SAMPLE_AUDIO_OUTPUT_RATE*60;
    int frames=tracker.tick_remainder/divisor;
    tracker.tick_remainder%=divisor;
    return frames;
}
static void advance_playback(void) {
    if(!tracker.playing)return;
    int divisor=tracker.song.bpm*tracker.song.rows_per_beat*tracker.song.ticks_per_row;
    int buffered=sample_audio_song_buffered_frames();
    int needed=SONG_LOOKAHEAD_FRAMES-buffered;
    if(needed<0)needed=0;
    tracker.rendering_song=1;
    while(needed>0 && !tracker.song_finished) {
        if(!tracker.frames_to_tick) {
            if(tracker.tick_phase==0)play_row(tracker.play_step);
            play_table_ticks();
            sample_audio_tick();
            if(++tracker.tick_phase>=tracker.song.ticks_per_row) {
                tracker.tick_phase=0;
                if(++tracker.play_step>=tracker.song.pattern_rows) {
                    tracker.play_step=0;
                    if(tracker.play_mode==PLAY_SONG &&
                       ++tracker.play_order>=tracker.song.order_count) {
                        if(tracker.loop)tracker.play_order=0;
                        else tracker.song_finished=1;
                    }
                }
            }
            tracker.frames_to_tick=next_tick_frames(divisor);
        }
        int frames=needed<tracker.frames_to_tick?needed:tracker.frames_to_tick;
        sample_audio_render_song(frames);
        tracker.song_written_frames+=(Uint64)frames;
        tracker.frames_to_tick-=frames;
        needed-=frames;
    }
    tracker.rendering_song=0;
    buffered=sample_audio_song_buffered_frames();
    Uint64 played=tracker.song_written_frames>
        (Uint64)buffered?tracker.song_written_frames-(Uint64)buffered:0;
    while(tracker.display_head!=tracker.display_tail &&
          tracker.display_rows[tracker.display_head].frame<=played) {
        SongDisplayRow row=tracker.display_rows[tracker.display_head];
        tracker.active_step=row.row;
        tracker.active_pattern_index=row.pattern;
        tracker.active_order_index=row.order;
        if(tracker.follow && row.order>=0 && tracker.order_list)
            tracker.order_list->cursor=row.order;
        tracker.display_head=(tracker.display_head+1)%SONG_DISPLAY_ROWS;
    }
    if(tracker.song_finished && buffered<512) {
        sample_audio_end_song();
        tracker.playing=tracker.started=0;
    }
}
static void advance_audition(void) {
    if(tracker.playing || !tracker.audition_effect_active)return;
    if(!sample_audio_active()) { tracker.audition_effect_active=0;return; }
    Uint64 now=SDL_GetTicks();
    int interval=60000/(tracker.song.bpm*tracker.song.rows_per_beat*
                        tracker.song.ticks_per_row);
    if(interval<1)interval=1;
    for(int tick=0;tick<64 && now>=tracker.next_audition_tick_at;++tick) {
        sample_audio_tick();
        tracker.next_audition_tick_at+=interval;
    }
}
static int load_path(const char *path,const char *original_name) {
    if(tracker.sample_count>=SAMPLE_HISTORY_MAX) {
        message("Sample slots full; restart to load a different set of WAVs");
        return 0;
    }
    SampleWave next={0};
    char error[160];
    if(!sample_load_wav(path,&next,error,sizeof(error))) {
        message(error);return 0;
    }
    stop_playback(NULL,NULL);
    StoredSample *previous=&tracker.samples[tracker.sample_index];
    previous->wave=tracker.wave;
    previous->phrase=tracker.phrase;
    snprintf(previous->loaded_path,sizeof(previous->loaded_path),"%s",tracker.loaded_path);
    snprintf(previous->csv_path,sizeof(previous->csv_path),"%s",tracker.csv_path);
    tracker.wave=next;
    if(original_name && *original_name) {
        snprintf(tracker.wave.name,sizeof(tracker.wave.name),"%s",original_name);
        snprintf(tracker.wave.source_name,sizeof(tracker.wave.source_name),"%s",original_name);
    }
    sample_phrase_init(&tracker.phrase,tracker.wave.frames);
    tracker.zoom=tracker.view_start=tracker.marker=0;
    tracker.sample_index=tracker.sample_count++;
    snprintf(tracker.loaded_path,sizeof(tracker.loaded_path),"%s",path);
    tracker.csv_path[0]='\0';
    message("WAV loaded; markers reset across the sample");
    return 1;
}
static void select_sample(int direction) {
    if(tracker.sample_count<2) {
        center_on_marker();
        message("Only one sample; load another WAV from File");
        return;
    }
    int target=(tracker.sample_index+direction+tracker.sample_count)%tracker.sample_count;
    stop_playback(NULL,NULL);
    StoredSample *current=&tracker.samples[tracker.sample_index];
    current->wave=tracker.wave;
    current->phrase=tracker.phrase;
    snprintf(current->loaded_path,sizeof(current->loaded_path),"%s",tracker.loaded_path);
    snprintf(current->csv_path,sizeof(current->csv_path),"%s",tracker.csv_path);
    StoredSample *next=&tracker.samples[target];
    tracker.wave=next->wave;
    tracker.phrase=next->phrase;
    snprintf(tracker.loaded_path,sizeof(tracker.loaded_path),"%s",next->loaded_path);
    snprintf(tracker.csv_path,sizeof(tracker.csv_path),"%s",next->csv_path);
    memset(next,0,sizeof(*next));
    tracker.sample_index=target;
    tracker.zoom=tracker.view_start=tracker.marker=0;
    center_on_marker();
    message("Selected sample");
}
static void previous_sample(F80Widget *button,void *user) {
    (void)button;(void)user;select_sample(-1);
}
static void next_sample(F80Widget *button,void *user) {
    (void)button;(void)user;select_sample(1);
}
static int save_song_at(const char *path) {
    SampleSongSample meta[SAMPLE_HISTORY_MAX]={0};
    tracker.song.pattern[tracker.pattern_index]=tracker.pattern;
    for(int i=0;i<tracker.sample_count;++i) {
        const SampleWave *wave=i==tracker.sample_index?&tracker.wave:&tracker.samples[i].wave;
        const SamplePhrase *phrase=i==tracker.sample_index?&tracker.phrase:&tracker.samples[i].phrase;
        const char *loaded=i==tracker.sample_index?tracker.loaded_path:tracker.samples[i].loaded_path;
        SampleSongSample *entry=&meta[i];
        snprintf(entry->name,sizeof(entry->name),"%s",wave->name);
        snprintf(entry->source_name,sizeof(entry->source_name),"%s",wave->source_name);
        entry->frames=wave->frames;entry->rate=wave->rate;entry->phrase=*phrase;
        if(!loaded[0])snprintf(entry->path,sizeof(entry->path),"builtin:demo");
        else {
#ifdef __EMSCRIPTEN__
            loaded=wave->source_name;
#else
            char absolute[PATH_MAX];
            if(realpath(loaded,absolute))loaded=absolute;
#endif
            snprintf(entry->path,sizeof(entry->path),"%s",loaded);
        }
    }
    char error[192];
    if(!sample_song_save(path,&tracker.song,meta,tracker.sample_count,error,sizeof(error))) {
        message(error);return 0;
    }
    snprintf(tracker.song_path,sizeof(tracker.song_path),"%s",path);
    message("Song saved with patterns, order, sample paths and slices");
    return 1;
}
static int load_song_at(const char *path) {
    SampleSong *next=malloc(sizeof(*next));
    if(!next) { message("Out of memory loading song");return 0; }
    SampleSongSample meta[SAMPLE_HISTORY_MAX];
    StoredSample loaded[SAMPLE_HISTORY_MAX]={0};
    int count=0,missing=0;char error[192];
    if(!sample_song_load(path,next,meta,&count,error,sizeof(error))) {
        free(next);message(error);return 0;
    }
    for(int i=0;i<count;++i) {
        char resolved[512];
        if(!sample_song_resolve_path(path,meta[i].path,resolved,sizeof(resolved))) {
            snprintf(error,sizeof(error),"Sample path too long");goto failed;
        }
        if(!strcmp(resolved,"builtin:demo")) {
            if(!sample_make_demo(&loaded[i].wave)) {
                snprintf(error,sizeof(error),"Could not recreate demo sample");goto failed;
            }
        } else {
            if(!sample_load_wav(resolved,&loaded[i].wave,error,sizeof(error))) {
#ifdef __EMSCRIPTEN__
                const char *basename=strrchr(resolved,'/');
                if(basename && basename[1]) {
                    char fallback[512];snprintf(fallback,sizeof(fallback),"/%s",basename+1);
                    if(sample_load_wav(fallback,&loaded[i].wave,error,sizeof(error)))
                        snprintf(resolved,sizeof(resolved),"%s",fallback);
                }
#endif
                if(!loaded[i].wave.pcm)++missing;
            }
            snprintf(loaded[i].loaded_path,sizeof(loaded[i].loaded_path),"%s",resolved);
        }
        if(loaded[i].wave.pcm &&
           (loaded[i].wave.frames!=meta[i].frames || loaded[i].wave.rate!=meta[i].rate)) {
            snprintf(error,sizeof(error),"WAV mismatch: %.120s",meta[i].source_name);goto failed;
        }
        if(!loaded[i].wave.pcm) {
            loaded[i].wave.frames=meta[i].frames;
            loaded[i].wave.rate=meta[i].rate;
        }
        snprintf(loaded[i].wave.name,sizeof(loaded[i].wave.name),"%s",meta[i].name);
        snprintf(loaded[i].wave.source_name,sizeof(loaded[i].wave.source_name),"%s",meta[i].source_name);
        loaded[i].phrase=meta[i].phrase;
    }
    stop_playback(NULL,NULL);
    sample_free(&tracker.wave);
    for(int i=0;i<tracker.sample_count;++i)sample_free(&tracker.samples[i].wave);
    memset(tracker.samples,0,sizeof(tracker.samples));
    tracker.song=*next;tracker.pattern_index=0;tracker.pattern=next->pattern[0];
    tracker.edit_order=order_for_pattern(0,0);
    tracker.phrase_scroll=tracker.edit_order>=0?
        tracker.edit_order*tracker.song.pattern_rows:0;
    tracker.reveal_cursor=1;
    tracker.play_mode=PLAY_SONG;
    tracker.sample_count=count;tracker.sample_index=0;
    tracker.wave=loaded[0].wave;tracker.phrase=loaded[0].phrase;
    snprintf(tracker.loaded_path,sizeof(tracker.loaded_path),"%s",loaded[0].loaded_path);
    tracker.csv_path[0]=0;
    for(int i=1;i<count;++i)tracker.samples[i]=loaded[i];
    tracker.zoom=tracker.view_start=tracker.marker=tracker.step=tracker.play_order=0;
    snprintf(tracker.song_path,sizeof(tracker.song_path),"%s",path);
    free(next);
    if(missing) {
        char info[192];
        snprintf(info,sizeof(info),"Song loaded; %d WAV file%s missing",missing,missing==1?"":"s");
        message(info);
    } else message("Song loaded");
    return 1;
failed:
    for(int i=0;i<count;++i)sample_free(&loaded[i].wave);
    free(next);message(error);return 0;
}
static int recover_missing(const char *name) {
    int recovered=0;
    for(int i=0;i<tracker.sample_count;++i) {
        SampleWave *wave=i==tracker.sample_index?&tracker.wave:&tracker.samples[i].wave;
        const char *path=i==tracker.sample_index?tracker.loaded_path:tracker.samples[i].loaded_path;
        if(wave->pcm || (name && strcmp(wave->source_name,name)))continue;
        char candidate[512];
        snprintf(candidate,sizeof(candidate),"%s",path);
        SampleWave next={0};char error[160];
        if(!sample_load_wav(candidate,&next,error,sizeof(error))) {
            const char *base=strrchr(candidate,'/');
            if(!base || !base[1])continue;
            snprintf(candidate,sizeof(candidate),"/%s",base+1);
            if(!sample_load_wav(candidate,&next,error,sizeof(error)))continue;
        }
        if(next.frames!=wave->frames || next.rate!=wave->rate) {
            sample_free(&next);continue;
        }
        snprintf(next.name,sizeof(next.name),"%s",wave->name);
        snprintf(next.source_name,sizeof(next.source_name),"%s",wave->source_name);
        *wave=next;
        if(i==tracker.sample_index)
            snprintf(tracker.loaded_path,sizeof(tracker.loaded_path),"%s",candidate);
        else snprintf(tracker.samples[i].loaded_path,
                      sizeof(tracker.samples[i].loaded_path),"%s",candidate);
        ++recovered;
    }
    if(recovered)message("Missing WAV restored");
    return recovered;
}
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE int sample_tracker_recover_wav(const char *name) {
    return recover_missing(name);
}
EMSCRIPTEN_KEEPALIVE int sample_tracker_load_wav(const char *original_name) {
    return load_path("/sample-tracker-upload.wav",original_name);
}
EMSCRIPTEN_KEEPALIVE int sample_tracker_load_song(void) {
    return load_song_at("/sample-tracker-import.song");
}
EMSCRIPTEN_KEEPALIVE int sample_tracker_save_song(void) {
    if(!save_song_at("/sample-tracker-export.song"))return 0;
    download_web_song("sample.song");return 1;
}
#endif
#ifndef __EMSCRIPTEN__
static void file_chosen(const char *path,void *user) {
    (void)user;
    if(tracker.path_action==PATH_WAV)load_path(path,NULL);
    else if(tracker.path_action==PATH_SONG_LOAD)load_song_at(path);
    else save_song_at(path);
}
#endif
static void open_path_dialog(int action) {
#ifdef __EMSCRIPTEN__
    if(action==PATH_WAV)choose_web_wav();
    else if(action==PATH_SONG_LOAD)choose_web_song();
    else sample_tracker_save_song();
#else
    tracker.path_action=action;
    int save=action==PATH_SONG_SAVE;
    const char *title=action==PATH_WAV?"Open WAV":save?"Save Song As":"Open Song";
    const char *initial=action==PATH_WAV?tracker.loaded_path:
        tracker.song_path[0]?tracker.song_path:"sample.song";
    const char *filter=action==PATH_WAV?".wav":".song,.txt";
    if(!f80_file_dialog_show(&file_dialog,&tracker.app,
                             save?F80_FILE_SAVE:F80_FILE_OPEN,title,initial,
                             filter,file_chosen,NULL))
        message("Could not open file dialog");
#endif
}
static void open_load(F80Widget *button,void *user) {
    (void)button;(void)user;open_path_dialog(PATH_WAV);
}
static void open_song(F80Widget *button,void *user) {
    (void)button;(void)user;open_path_dialog(PATH_SONG_LOAD);
}
static void save_song(F80Widget *button,void *user) {
    (void)button;(void)user;open_path_dialog(PATH_SONG_SAVE);
}
static void quit(F80Widget *button,void *user) {
    (void)button;(void)user;tracker.app.running=0;
}
static void zoom_out(F80Widget *button,void *user) {
    (void)button;(void)user;zoom_wave(-1);
}
static void zoom_in(F80Widget *button,void *user) {
    (void)button;(void)user;zoom_wave(1);
}
static void previous_marker(F80Widget *button,void *user) {
    (void)button;(void)user;
    focus_marker(tracker.marker-1);
    message("Previous marker selected");
}
static void next_marker(F80Widget *button,void *user) {
    (void)button;(void)user;
    focus_marker(tracker.marker+1);
    message("Next marker selected");
}
static void set_effect(SampleCell *cell,int effect) {
    if(effect==SAMPLE_EFFECT_NONE) {
        cell->effect=effect;cell->value=-1;
    } else if(cell->effect!=effect) {
        cell->effect=effect;
        cell->value=-1;
    }
}
static void adjust_value(int delta) {
    if(tracker.focus==FOCUS_WAVE) {
        int cells=tracker.wave_canvas->rect.w*2;
        if(cells<1)cells=1;
        int distance=visible_frames()/cells;
        if(distance<1)distance=1;
        sample_move_marker(&tracker.phrase,tracker.wave.frames,tracker.marker,
                           tracker.phrase.marker[tracker.marker]+delta*distance);
        center_on_marker();
        message("Marker moved; hold on the waveform to select and drag one");
    } else if(tracker.focus==FOCUS_REFERENCE) {
        sample_phrase_set_reference_note(&tracker.phrase,
            clamp(tracker.phrase.reference_note+delta,12,127));
        message("Reference note sets the sample's original pitch");
    } else {
        SampleCell *cell=&tracker.pattern.cell[tracker.step][tracker.channel];
        if(tracker.field==0)
            cell->note=clamp((cell->note<0?tracker.phrase.reference_note:cell->note)+delta,
                             12,127);
        else if(tracker.field==1)cell->instrument=tracker.sample_index+1;
        else if(tracker.field==2) {
            const char *effects="GIJKMNOPRSTVW";
            const char *current=cell->effect?strchr(effects,cell->effect):NULL;
            int index=current?(int)(current-effects):-1;
            int next=clamp(index+(delta>0?1:-1),-1,(int)strlen(effects)-1);
            set_effect(cell,next<0?SAMPLE_EFFECT_NONE:effects[next]);
        } else {
            if(cell->effect==SAMPLE_EFFECT_NONE)set_effect(cell,SAMPLE_EFFECT_S);
            cell->value=clamp(cell->value+delta,0,255);
        }
        message("Pattern cell changed");
    }
}
static void value_down(F80Widget *button,void *user) {
    (void)button;(void)user;adjust_value(-1);
}
static void value_up(F80Widget *button,void *user) {
    (void)button;(void)user;adjust_value(1);
}
static void value_down_16(F80Widget *button,void *user) {
    (void)button;(void)user;adjust_value(-16);
}
static void value_up_16(F80Widget *button,void *user) {
    (void)button;(void)user;adjust_value(16);
}
static void focus_phrase(F80Widget *button,void *user) {
    (void)button;(void)user;tracker.focus=FOCUS_PHRASE;
}
static void focus_wave(F80Widget *button,void *user) {
    (void)button;(void)user;tracker.focus=FOCUS_WAVE;
}
static void toggle_loop(F80Widget *button,void *user) {
    (void)button;(void)user;tracker.loop=!tracker.loop;
    message(tracker.loop?"Song loop on":"Song loop off");
}
static void toggle_follow(F80Widget *button,void *user) {
    (void)button;(void)user;
    tracker.follow=!tracker.follow;
    message(tracker.follow?"Follow playhead on":"Follow playhead off");
}
static void toggle_mode(F80Widget *button,void *user) {
    (void)button;(void)user;
    tracker.play_mode=tracker.play_mode==PLAY_SONG?PLAY_PATTERN:PLAY_SONG;
    start_playback(NULL,NULL);
}
static void sample_extrema(int first,int last,float *low,float *high) {
    *low=1;*high=-1;
    if(!tracker.wave.pcm)return;
    if(first<0)first=0;
    if(last>tracker.wave.frames)last=tracker.wave.frames;
    if(last<=first)last=first+1;
    if(last-first>=128 && tracker.wave.peaks) {
        int begin=first/64,finish=(last-1)/64;
        for(int block=begin;block<=finish && block<tracker.wave.peak_count;++block) {
            SamplePeak peak=tracker.wave.peaks[block];
            if(peak.low<*low)*low=peak.low;
            if(peak.high>*high)*high=peak.high;
        }
    } else for(int frame=first;frame<last;++frame) {
        float value=tracker.wave.pcm[frame];
        if(value<*low)*low=value;
        if(value>*high)*high=value;
    }
}
static int wave_x_from_frame(int frame,int width) {
    int visible=visible_frames();
    long long offset=(long long)frame-tracker.view_start;
    if(offset<0 || offset>visible)return -1;
    int x=(int)(offset*width/visible);
    return x>=width?width-1:x;
}
static int wave_marker_at(int x) {
    F80Rect r=tracker.wave_canvas->rect;
    int nearest=-1,best=2;
    for(int marker=0;marker<SAMPLE_MARKERS;++marker) {
        int dot=wave_x_from_frame(tracker.phrase.marker[marker],r.w*2);
        if(dot<0)continue;
        int distance=abs(x-(r.x+dot/2));
        if(distance<best) { best=distance;nearest=marker; }
    }
    return nearest;
}
static void paint_wave(F80Widget *canvas,void *user) {
    (void)user;
    F80Rect r=canvas->rect;
    if(r.w<8 || r.h<1)return;
    if(!tracker.wave.pcm) {
        f80_fill(r,15);
        f80_put(r.y+(r.h>1),r.x+2,"WAV FILE MISSING",r.w-4,15,1);
        if(r.h>=5)f80_put(r.y+2,r.x+2,tracker.loaded_path,r.w-4,15,0);
        if(r.h>=4)f80_put(r.y+r.h-2,r.x+2,"[ Retry loading WAV ]",r.w-4,15,1);
        return;
    }
    f80_fill(r,1);
    int visible=visible_frames();
    char label[160];
    snprintf(label,sizeof(label),"%d/%d %s | %.2fs–%.2fs / %.2fs | zoom %dx | slice %X",
             tracker.sample_index+1,tracker.sample_count,tracker.wave.name,
             (double)tracker.view_start/tracker.wave.rate,
             (double)(tracker.view_start+visible)/tracker.wave.rate,
             (double)tracker.wave.frames/tracker.wave.rate,1<<tracker.zoom,
             tracker.marker);
    if(r.h>1)f80_put(r.y,r.x,label,r.w,4,1);
    int compact=r.h<6;
    int graph_y=compact?r.y+(r.h>1):r.y+2;
    int graph_h=compact?r.h-(r.h>1)-(r.h>2):r.h-4;
    int graph_w=r.w;
    F80Raster raster=f80_raster(f80_rect(r.x,graph_y,graph_w,graph_h));
    if(!raster.dots)return;
    int pixels=graph_w*2,vertical=graph_h*4;
    for(int x=0;x<pixels;++x) {
        int first=tracker.view_start+(int)((long long)x*visible/pixels);
        int last=tracker.view_start+(int)((long long)(x+1)*visible/pixels);
        if(last<=first)last=first+1;
        float low,high;
        sample_extrema(first,last,&low,&high);
        if(low<-1)low=-1;if(high>1)high=1;
        int top=(int)((1-high)*(vertical-1)/2);
        int bottom=(int)((1-low)*(vertical-1)/2);
        f80_raster_line(&raster,x,top,x,bottom,10,2);
    }
    for(int marker=0;marker<SAMPLE_MARKERS;++marker) {
        int x=wave_x_from_frame(tracker.phrase.marker[marker],pixels);
        if(x<0)continue;
        f80_raster_line(&raster,x,0,x,vertical-1,
                        marker==tracker.marker?4:8,marker==tracker.marker?4:3);
    }
    f80_raster_draw_quadrants(&raster);
    f80_raster_free(&raster);
    if(!compact)for(int marker=0;marker<SAMPLE_MARKERS;++marker) {
        int x=wave_x_from_frame(tracker.phrase.marker[marker],graph_w);
        if(x<0)continue;
        char digit[2]={"0123456789ABCDEF"[marker],0};
        f80_put(r.y+1,r.x+x,digit,1,marker==tracker.marker?3:8,1);
    }
    if(r.h>2) {
        int track=r.w-2,maximum=tracker.wave.frames-visible;
        int thumb=track*visible/tracker.wave.frames;
        if(thumb<1)thumb=1;
        int at=maximum?(track-thumb)*tracker.view_start/maximum:0;
        f80_put(r.y+r.h-1,r.x,"◄",1,8,1);
        for(int x=0;x<track;++x)
            f80_put(r.y+r.h-1,r.x+1+x,x>=at && x<at+thumb?"█":"▒",1,10,0);
        f80_put(r.y+r.h-1,r.x+r.w-1,"►",1,8,1);
    }
}
static int pattern_field_width(int field) {
    return field==0?4:field==1 || field==2?3:2;
}
static int pattern_field_offset(int field) {
    return field==0?0:field==1?4:field==2?7:10;
}
static int pattern_field_span(int first,int last) {
    int width=0;
    for(int index=first;index<=last;++index) {
        if(index>first && index%4==0)++width;
        width+=pattern_field_width(index%4);
    }
    return width;
}
static int pattern_max_field_scroll(int available) {
    int first=SAMPLE_CHANNELS*4-1;
    while(first>0 && pattern_field_span(first-1,SAMPLE_CHANNELS*4-1)<=available)
        --first;
    return first;
}
static void ensure_pattern_visible(F80Rect r) {
    int body=r.h-3;
    if(body<1)body=1;
    if(tracker.edit_order<0 || tracker.edit_order>=tracker.song.order_count ||
       tracker.song.order[tracker.edit_order]!=tracker.pattern_index) {
        int found=order_for_pattern(tracker.pattern_index,tracker.edit_order);
        if(found!=tracker.edit_order) { tracker.edit_order=found;tracker.reveal_cursor=1; }
    }
    int maximum=grid_total_rows()-body;
    if(maximum<0)maximum=0;
    if(tracker.playing && tracker.follow &&
       tracker.play_mode==PLAY_SONG &&
       tracker.active_order_index>=0 && tracker.edit_order>=0) {
        int playhead=tracker.active_order_index*tracker.song.pattern_rows+tracker.active_step;
        tracker.phrase_scroll=playhead-body/2;
    } else if(tracker.reveal_cursor) {
        int edit=grid_edit_row();
        if(edit<tracker.phrase_scroll)tracker.phrase_scroll=edit;
        if(edit>=tracker.phrase_scroll+body)
            tracker.phrase_scroll=edit-body+1;
        tracker.reveal_cursor=0;
    }
    tracker.phrase_scroll=clamp(tracker.phrase_scroll,0,maximum);
    int available=r.w-6;
    if(available<1)available=1;
    int selected=tracker.channel*4+tracker.field;
    tracker.field_scroll=clamp(tracker.field_scroll,0,pattern_max_field_scroll(available));
    if(selected<tracker.field_scroll)tracker.field_scroll=selected;
    while(tracker.field_scroll<selected &&
          pattern_field_span(tracker.field_scroll,selected)>available)
        ++tracker.field_scroll;
}
static void paint_phrase(F80Widget *canvas,void *user) {
    (void)user;
    F80Rect r=canvas->rect;
    if(r.w<7 || r.h<4)return;
    ensure_pattern_visible(r);
    f80_fill(r,1);
    int right=r.x+r.w-1,x=r.x+5;
    int start[SAMPLE_CHANNELS],end[SAMPLE_CHANNELS];
    for(int channel=0;channel<SAMPLE_CHANNELS;++channel)
        start[channel]=end[channel]=-1;
    for(int index=tracker.field_scroll;index<SAMPLE_CHANNELS*4 && x<right;++index) {
        int channel=index/4,field=index%4;
        if(index>tracker.field_scroll && field==0) {
            for(int y=r.y;y<r.y+r.h;++y)f80_put(y,x,"│",1,10,0);
            if(++x>=right)break;
        }
        int width=pattern_field_width(field),visible=width;
        if(visible>right-x)visible=right-x;
        if(start[channel]<0)start[channel]=x;
        end[channel]=x+visible;
        float level=sample_audio_level(channel);
        if(level>1)level=1;
        int pixels=(int)(level*24+.5f);
        for(int column=0;column<visible;++column) {
            int pixel=(pattern_field_offset(field)+column)*2;
            f80_put(r.y+1,x+column,pixels>pixel+1?"█":pixels>pixel?"▌":"▒",
                    1,pixels>pixel?8:10,1);
        }
        for(int line=0;line<r.h-3;++line) {
            int global=tracker.phrase_scroll+line;
            if(global>=grid_total_rows())break;
            int order=grid_order_for_row(global),row=global%tracker.song.pattern_rows;
            int pattern_index=grid_pattern_for_row(global);
            int editable=order<0 || order==tracker.edit_order;
            const SamplePattern *pattern=pattern_index==tracker.pattern_index?
                &tracker.pattern:&tracker.song.pattern[pattern_index];
            const SampleCell *cell=&pattern->cell[row][channel];
            char compact[11],shown[13];
            sample_cell_text(cell,compact);
            snprintf(shown,sizeof(shown),"%.3s %.2s %.5s",compact,compact+3,compact+5);
            int editrow=editable && tracker.focus==FOCUS_PHRASE && row==tracker.step;
            int editcol=tracker.focus==FOCUS_PHRASE && channel==tracker.channel;
            int selected=editrow && editcol && field==tracker.field;
            int empty=field==0?cell->note<0:field==1?cell->instrument<0:
                      cell->effect==SAMPLE_EFFECT_NONE;
            int playhead=row==tracker.active_step &&
                (order>=0?order==tracker.active_order_index:
                 pattern_index==tracker.active_pattern_index);
            int pair=!editable?66:editrow && editcol?4:editrow?2:
                     playhead?4:empty?1:
                     field==0?4:field==1?8:9;
            if(editable && selected_cell(row,channel))pair=65;
            const char *text=shown+pattern_field_offset(field);
            f80_put(r.y+3+line,x,text,visible,pair,
                    editable && (editrow || playhead));
            if(selected && !tracker.selecting) {
                int highlight=field==0?3:field==1?2:width;
                if(highlight>visible)highlight=visible;
                f80_put(r.y+3+line,x,text,highlight,3,1);
            }
        }
        x+=width;
    }
    for(int channel=0;channel<SAMPLE_CHANNELS;++channel)if(start[channel]>=0) {
        int width=end[channel]-start[channel];
        char label[8];int length=snprintf(label,sizeof(label),"CH%d",channel+1);
        if(length>width)length=width;
        f80_put(r.y+2,start[channel]+(width-length)/2,label,length,
                channel==tracker.channel?4:10,1);
    }
    int drawn_end=x<right?x:right;
    if(drawn_end>r.x+5) {
        int span=drawn_end-(r.x+5);
        for(int at=r.x+5;at<drawn_end;++at)f80_put(r.y,at,"─",1,10,0);
        const char *title=tracker.song.name[tracker.pattern_index];
        int length=(int)strlen(title);
        if(length>span)length=span;
        f80_put(r.y,r.x+5+(span-length)/2,title,length,4,1);
    }
    for(int line=0;line<r.h-3;++line) {
        int global=tracker.phrase_scroll+line;
        if(global>=grid_total_rows())break;
        int order=grid_order_for_row(global),row=global%tracker.song.pattern_rows;
        int pattern_index=grid_pattern_for_row(global);
        int editable=order<0 || order==tracker.edit_order;
        int playhead=row==tracker.active_step &&
            (order>=0?order==tracker.active_order_index:
             pattern_index==tracker.active_pattern_index);
        char gutter[8];
        if(row==0 && order>=0)snprintf(gutter,sizeof(gutter),"P%02X ",pattern_index);
        else snprintf(gutter,sizeof(gutter)," %02X ",row);
        int y=r.y+3+line;
        f80_put(y,r.x,gutter,4,editable && row==tracker.step?3:
                playhead?4:editable?(row%4==0?8:2):66,editable);
        if(editable && row==tracker.step)f80_put(y,r.x,">",1,3,1);
        if(playhead)
            f80_put(y,r.x+3,"*",1,4,1);
    }
    for(int y=r.y;y<r.y+r.h;++y)f80_put(y,r.x+4,"│",1,10,0);
    if(tracker.field_scroll>0)f80_put(r.y+2,r.x+4,"◄",1,8,1);
    if(pattern_field_span(tracker.field_scroll,SAMPLE_CHANNELS*4-1)>r.w-6)
        f80_put(r.y+2,right-1,"►",1,8,1);
    int total=grid_total_rows();
    int track=r.h-2,body=r.h-3,maximum=total-body;
    if(maximum<0)maximum=0;
    int thumb=total>body?track*body/total:track;
    if(thumb<1)thumb=1;
    int at=maximum?(track-thumb)*tracker.phrase_scroll/maximum:0;
    f80_put(r.y,right,"▲",1,10,1);
    for(int line=0;line<track;++line)
        f80_put(r.y+1+line,right,line>=at && line<at+thumb?"█":"▒",1,10,0);
    f80_put(r.y+r.h-1,right,"▼",1,10,1);
}
static void close_help(F80Widget *button,void *user) {
    (void)button;(void)user;f80_hide_modal(&tracker.app);
}
static void store_sample(void) {
    StoredSample *slot=&tracker.samples[tracker.sample_index];
    slot->wave=tracker.wave;slot->phrase=tracker.phrase;
    snprintf(slot->loaded_path,sizeof(slot->loaded_path),"%s",tracker.loaded_path);
    snprintf(slot->csv_path,sizeof(slot->csv_path),"%s",tracker.csv_path);
    memset(&tracker.wave,0,sizeof(tracker.wave));
}
static void restore_sample(int index) {
    StoredSample *slot=&tracker.samples[index];
    tracker.wave=slot->wave;tracker.phrase=slot->phrase;
    snprintf(tracker.loaded_path,sizeof(tracker.loaded_path),"%s",slot->loaded_path);
    snprintf(tracker.csv_path,sizeof(tracker.csv_path),"%s",slot->csv_path);
    memset(slot,0,sizeof(*slot));
    tracker.sample_index=index;
    tracker.zoom=tracker.view_start=tracker.marker=0;
}
static void manage_refresh(void) {
    if(!tracker.manage_dialog)return;
    for(int i=0;i<tracker.sample_count;++i) {
        const SampleWave *wave=i==tracker.sample_index?&tracker.wave:&tracker.samples[i].wave;
        char note[4];const SamplePhrase *phrase=i==tracker.sample_index?
            &tracker.phrase:&tracker.samples[i].phrase;
        sample_note_name(phrase->reference_note,note);
        snprintf(tracker.manage_rows[i],sizeof(tracker.manage_rows[i]),
                 "%02X  %-36.36s  Ref %s",i+1,wave->name,note);
        tracker.manage_items[i]=tracker.manage_rows[i];
    }
    tracker.manage_list->item_count=tracker.sample_count;
    int selected=clamp(tracker.manage_list->cursor,0,tracker.sample_count-1);
    const SampleWave *wave=selected==tracker.sample_index?&tracker.wave:
        &tracker.samples[selected].wave;
    const SamplePhrase *phrase=selected==tracker.sample_index?&tracker.phrase:
        &tracker.samples[selected].phrase;
    char note[4];sample_note_name(phrase->reference_note,note);
    snprintf(tracker.manage_details,sizeof(tracker.manage_details),
             "Source: %.50s | %d frames | %d Hz | Base %s",
             wave->source_name,wave->frames,wave->rate,note);
}
static void manage_selected(F80Widget *list,void *user) {
    (void)user;
    int index=clamp(list->cursor,0,tracker.sample_count-1);
    const SampleWave *wave=index==tracker.sample_index?&tracker.wave:
        &tracker.samples[index].wave;
    snprintf(tracker.manage_name->input,sizeof(tracker.manage_name->input),"%s",wave->name);
    tracker.manage_name->input_pos=(int)strlen(tracker.manage_name->input);
    tracker.delete_armed=0;manage_refresh();
}
static void manage_move(F80Widget *button,void *user) {
    (void)button;int direction=(int)(long)user;
    int a=tracker.manage_list->cursor,b=a+direction;
    if(b<0 || b>=tracker.sample_count)return;
    stop_playback(NULL,NULL);
    store_sample();
    StoredSample temp=tracker.samples[a];tracker.samples[a]=tracker.samples[b];
    tracker.samples[b]=temp;
    int selected=tracker.sample_index==a?b:tracker.sample_index==b?a:tracker.sample_index;
    restore_sample(selected);
    for(int pattern=0;pattern<tracker.song.pattern_count;++pattern)
    for(int row=0;row<tracker.song.pattern_rows;++row)for(int channel=0;channel<SAMPLE_CHANNELS;++channel) {
        SamplePattern *bank=pattern==tracker.pattern_index?&tracker.pattern:
                            &tracker.song.pattern[pattern];
        SampleCell *cell=&bank->cell[row][channel];
        if(cell->instrument==a+1)cell->instrument=b+1;
        else if(cell->instrument==b+1)cell->instrument=a+1;
    }
    tracker.manage_list->cursor=b;manage_selected(tracker.manage_list,NULL);
    message("Samples reordered; pattern instrument numbers updated");
}
static void manage_delete(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.sample_count<=1) { message("Keep at least one sample");return; }
    if(!tracker.delete_armed) { tracker.delete_armed=1;message("Press Delete again to remove this sample");return; }
    int removed=tracker.manage_list->cursor;
    stop_playback(NULL,NULL);store_sample();
    sample_free(&tracker.samples[removed].wave);
    for(int i=removed;i<tracker.sample_count-1;++i)tracker.samples[i]=tracker.samples[i+1];
    memset(&tracker.samples[--tracker.sample_count],0,sizeof(StoredSample));
    int next=tracker.sample_index;
    if(next==removed)next=clamp(removed,0,tracker.sample_count-1);
    else if(next>removed)--next;
    restore_sample(next);
    for(int pattern=0;pattern<tracker.song.pattern_count;++pattern)
    for(int row=0;row<tracker.song.pattern_rows;++row)for(int channel=0;channel<SAMPLE_CHANNELS;++channel) {
        SamplePattern *bank=pattern==tracker.pattern_index?&tracker.pattern:
                            &tracker.song.pattern[pattern];
        SampleCell *cell=&bank->cell[row][channel];
        if(cell->instrument==removed+1)*cell=(SampleCell){-1,-1,SAMPLE_EFFECT_NONE,-1};
        else if(cell->instrument>removed+1)--cell->instrument;
    }
    tracker.manage_list->cursor=clamp(removed,0,tracker.sample_count-1);
    manage_selected(tracker.manage_list,NULL);
    message("Sample deleted; pattern references updated");
}
static void manage_rename(F80Widget *button,void *user) {
    (void)button;(void)user;
    tracker.delete_armed=0;
    if(!tracker.manage_name->input[0]) { message("Enter a sample name");return; }
    int index=tracker.manage_list->cursor;
    SampleWave *wave=index==tracker.sample_index?&tracker.wave:&tracker.samples[index].wave;
    snprintf(wave->name,sizeof(wave->name),"%s",tracker.manage_name->input);
    manage_refresh();message("Sample renamed; original WAV name retained for CSV");
}
static void manage_ref(F80Widget *button,void *user) {
    (void)button;int delta=(int)(long)user,index=tracker.manage_list->cursor;
    tracker.delete_armed=0;
    SamplePhrase *phrase=index==tracker.sample_index?&tracker.phrase:
        &tracker.samples[index].phrase;
    sample_phrase_set_reference_note(phrase,clamp(phrase->reference_note+delta,0,127));
    manage_refresh();message("Sample base changed; slice bases transposed");
}
static void close_manage(F80Widget *button,void *user) {
    (void)button;(void)user;f80_hide_modal(&tracker.app);
    tracker.manage_dialog=tracker.manage_list_bg=tracker.manage_list=
        tracker.manage_name=tracker.manage_info=NULL;
}
static void show_manage(F80Widget *button,void *user) {
    (void)button;(void)user;
    F80Widget *dialog=f80_dialog("Manage samples",NULL,NULL,0,0);
    dialog->dialog_default=3;
    tracker.manage_dialog=dialog;
    tracker.manage_list_bg=f80_add(dialog,f80_window(F80_WINDOW_CYAN,F80_COL));
    tracker.manage_list=f80_add(dialog,f80_listbox(tracker.manage_items,tracker.sample_count,
                                                   manage_selected,NULL));
    tracker.manage_list->window_role=F80_WINDOW_CYAN;
    tracker.manage_list->cursor=tracker.sample_index;
    tracker.manage_name=f80_add(dialog,f80_input());
    tracker.manage_info=f80_add(dialog,f80_label(tracker.manage_details));
    tracker.manage_info->style=F80_COLOR_TEXT;
    f80_add(dialog,f80_button("~U~p",manage_move,(void *)(long)-1));
    f80_add(dialog,f80_button("Dow~n~",manage_move,(void *)(long)1));
    f80_add(dialog,f80_button("~D~elete",manage_delete,NULL));
    f80_add(dialog,f80_button("~R~ename",manage_rename,NULL));
    f80_add(dialog,f80_button("Ref ~-~",manage_ref,(void *)(long)-1));
    f80_add(dialog,f80_button("Ref ~+~",manage_ref,(void *)(long)1));
    f80_add(dialog,f80_button("~C~lose",close_manage,NULL));
    manage_selected(tracker.manage_list,NULL);
    f80_show_modal(&tracker.app,dialog);
}
static int parse_note(const char *text) {
    char candidate[4];
    for(int note=0;note<128;++note) {
        sample_note_name(note,candidate);
        if(!strcmp(candidate,text))return note;
    }
    return -1;
}
static void slices_refresh(void) {
    if(!tracker.slices_dialog)return;
    for(int i=0;i<SAMPLE_SLICES;++i) {
        char note[4];sample_note_name(sample_slice_base_note(&tracker.phrase,i),note);
        snprintf(tracker.slice_rows[i],sizeof(tracker.slice_rows[i]),
                 "%X-%X  %8.3f - %8.3fs  %s%s",i,i+1,
                 (double)tracker.phrase.marker[i]/tracker.wave.rate,
                 (double)tracker.phrase.marker[i+1]/tracker.wave.rate,note,
                 tracker.phrase.slice_note_offset[i]==SAMPLE_SLICE_INHERIT?" (sample)":" (override)");
        tracker.slice_items[i]=tracker.slice_rows[i];
    }
    int i=clamp(tracker.slices_list->cursor,0,SAMPLE_SLICES-1);
    snprintf(tracker.slice_details,sizeof(tracker.slice_details),
             "Slice %X: frames %d-%d, duration %.3fs; source %.55s",i,
             tracker.phrase.marker[i],tracker.phrase.marker[i+1],
             (double)(tracker.phrase.marker[i+1]-tracker.phrase.marker[i])/tracker.wave.rate,
             tracker.wave.source_name);
}
static void slices_selected(F80Widget *list,void *user) {
    (void)user;char note[4];
    sample_note_name(sample_slice_base_note(&tracker.phrase,list->cursor),note);
    snprintf(tracker.slices_note->input,sizeof(tracker.slices_note->input),"%s",note);
    tracker.slices_note->input_pos=(int)strlen(tracker.slices_note->input);
    slices_refresh();
}
static void slices_apply(F80Widget *button,void *user) {
    (void)button;(void)user;
    int note=parse_note(tracker.slices_note->input);
    if(note<0) { message("Enter a note like C-4 or F#3");return; }
    sample_slice_set_note(&tracker.phrase,tracker.slices_list->cursor,note);
    slices_refresh();message("Slice base note set");
}
static void slices_inherit(F80Widget *button,void *user) {
    (void)button;(void)user;
    sample_slice_set_note(&tracker.phrase,tracker.slices_list->cursor,-1);
    slices_selected(tracker.slices_list,NULL);message("Slice inherits sample base note");
}
static void slices_step(F80Widget *button,void *user) {
    (void)button;
    int index=tracker.slices_list->cursor;
    sample_slice_set_note(&tracker.phrase,index,
        clamp(sample_slice_base_note(&tracker.phrase,index)+(int)(long)user,0,127));
    slices_selected(tracker.slices_list,NULL);message("Slice base note changed");
}
static void close_slices(F80Widget *button,void *user) {
    (void)button;(void)user;f80_hide_modal(&tracker.app);
    tracker.slices_dialog=tracker.slices_list_bg=tracker.slices_list=
        tracker.slices_note=tracker.slices_info=NULL;
}
static void show_slices(F80Widget *button,void *user) {
    (void)button;(void)user;
    F80Widget *dialog=f80_dialog("Slices",NULL,NULL,0,0);
    tracker.slices_dialog=dialog;
    tracker.slices_list_bg=f80_add(dialog,f80_window(F80_WINDOW_CYAN,F80_COL));
    tracker.slices_list=f80_add(dialog,f80_listbox(tracker.slice_items,SAMPLE_SLICES,
                                                   slices_selected,NULL));
    tracker.slices_list->window_role=F80_WINDOW_CYAN;
    tracker.slices_list->cursor=clamp(tracker.marker,0,SAMPLE_SLICES-1);
    tracker.slices_note=f80_add(dialog,f80_input());
    tracker.slices_info=f80_add(dialog,f80_label(tracker.slice_details));
    tracker.slices_info->style=F80_COLOR_TEXT;
    f80_add(dialog,f80_button("~A~pply",slices_apply,NULL));
    f80_add(dialog,f80_button("~I~nherit",slices_inherit,NULL));
    f80_add(dialog,f80_button("Note ~-~",slices_step,(void *)(long)-1));
    f80_add(dialog,f80_button("Note ~+~",slices_step,(void *)(long)1));
    f80_add(dialog,f80_button("~C~lose",close_slices,NULL));
    slices_selected(tracker.slices_list,NULL);
    f80_show_modal(&tracker.app,dialog);
}
static void order_refresh(void) {
    if(!tracker.order_dialog)return;
    snprintf(tracker.order_heading,sizeof(tracker.order_heading),
             "PLAY ORDER  |  BPM %d",tracker.song.bpm);
    for(int i=0;i<tracker.song.pattern_count;++i) {
        const SamplePattern *pattern=i==tracker.pattern_index?&tracker.pattern:
            &tracker.song.pattern[i];
        snprintf(tracker.bank_rows[i],sizeof(tracker.bank_rows[i]),
                 "%02X  %.64s%s%s",i,tracker.song.name[i],
                 i==tracker.pattern_index?"  [editing]":"",
                 pattern_is_empty(pattern)?"  [empty]":"");
        tracker.bank_items[i]=tracker.bank_rows[i];
    }
    for(int i=0;i<tracker.song.order_count;++i) {
        int pattern=tracker.song.order[i];
        snprintf(tracker.order_rows[i],sizeof(tracker.order_rows[i]),
                 "%02X  ->  %02X  %.72s",i,pattern,tracker.song.name[pattern]);
        tracker.order_items[i]=tracker.order_rows[i];
    }
    tracker.order_bank->item_count=tracker.song.pattern_count;
    tracker.order_seq->item_count=tracker.song.order_count;
    tracker.order_bank->cursor=clamp(tracker.order_bank->cursor,0,tracker.song.pattern_count-1);
    tracker.order_seq->cursor=clamp(tracker.order_seq->cursor,0,tracker.song.order_count-1);
}
static void order_bank_selected(F80Widget *list,void *user) {
    (void)user;
    snprintf(tracker.order_name->input,sizeof(tracker.order_name->input),"%s",
             tracker.song.name[list->cursor]);
    tracker.order_name->input_pos=(int)strlen(tracker.order_name->input);
}
static void empty_pattern(SamplePattern *pattern) {
    for(int row=0;row<SAMPLE_MAX_STEPS;++row)for(int channel=0;channel<SAMPLE_CHANNELS;++channel)
        pattern->cell[row][channel]=(SampleCell){-1,-1,SAMPLE_EFFECT_NONE,-1};
}
static void order_new_pattern(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.song.pattern_count>=SAMPLE_SONG_PATTERNS) {
        message("Pattern bank is full");return;
    }
    int index=tracker.song.pattern_count++;
    empty_pattern(&tracker.song.pattern[index]);
    snprintf(tracker.song.name[index],SAMPLE_SONG_NAME,"Pattern %02X",index);
    select_edit_pattern(index);
    tracker.order_bank->cursor=index;
    order_refresh();order_bank_selected(tracker.order_bank,NULL);
    message("New pattern selected for editing; add it to the play order");
}
static void order_rename(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(!tracker.order_name->input[0]) { message("Enter a pattern name");return; }
    int index=tracker.order_bank->cursor;
    snprintf(tracker.song.name[index],SAMPLE_SONG_NAME,"%.63s",tracker.order_name->input);
    order_refresh();message("Pattern renamed");
}
static void order_edit(F80Widget *button,void *user) {
    (void)button;(void)user;
    int index=tracker.order_bank->cursor;
    stop_playback(NULL,NULL);
    select_edit_pattern(index);
    tracker.step=tracker.channel=tracker.field=0;
    order_refresh();message("Selected pattern for editing");
}
static void order_add(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.song.order_count>=SAMPLE_SONG_ORDER) { message("Order is full");return; }
    stop_playback(NULL,NULL);
    int at=tracker.order_seq->cursor+1;
    if(at<0 || at>tracker.song.order_count)at=tracker.song.order_count;
    memmove(tracker.song.order+at+1,tracker.song.order+at,
            (size_t)(tracker.song.order_count-at));
    tracker.song.order[at]=(unsigned char)tracker.order_bank->cursor;
    ++tracker.song.order_count;tracker.order_seq->cursor=at;
    order_refresh();message("Pattern inserted in play order");
}
static void order_remove(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.song.order_count<=1) { message("Keep at least one order entry");return; }
    stop_playback(NULL,NULL);
    int at=tracker.order_seq->cursor;
    memmove(tracker.song.order+at,tracker.song.order+at+1,
            (size_t)(tracker.song.order_count-at-1));
    --tracker.song.order_count;order_refresh();message("Order entry removed");
}
static void order_move(F80Widget *button,void *user) {
    (void)button;int a=tracker.order_seq->cursor,b=a+(int)(long)user;
    if(b<0 || b>=tracker.song.order_count)return;
    stop_playback(NULL,NULL);
    unsigned char temp=tracker.song.order[a];tracker.song.order[a]=tracker.song.order[b];
    tracker.song.order[b]=temp;tracker.order_seq->cursor=b;
    order_refresh();message("Order entry moved");
}
static void order_bpm(F80Widget *button,void *user) {
    (void)button;
    tracker.song.bpm=clamp(tracker.song.bpm+(int)(long)user,30,300);
    order_refresh();message("Song BPM changed");
}
static int setting_number(const F80Widget *input,int low,int high,int *value) {
    char *end;
    long number=strtol(input->input,&end,10);
    if(!input->input[0] || *end || number<low || number>high)return 0;
    *value=(int)number;
    return 1;
}
static void close_settings(F80Widget *button,void *user) {
    (void)button;(void)user;
    f80_hide_modal(&tracker.app);
    tracker.settings_dialog=NULL;
}
static void apply_settings(F80Widget *button,void *user) {
    (void)button;(void)user;
    int bpm,rows,beat_rows,ticks;
    if(!setting_number(tracker.settings_input[0],30,300,&bpm) ||
       !setting_number(tracker.settings_input[1],16,SAMPLE_MAX_STEPS,&rows) ||
       rows%16 || !setting_number(tracker.settings_input[2],1,16,&beat_rows) ||
       !setting_number(tracker.settings_input[3],1,SAMPLE_MAX_TICKS_PER_ROW,&ticks)) {
        message("Use BPM 30-300, pattern 16-256 by 16, beat 1-16, ticks 1-48");
        return;
    }
    stop_playback(NULL,NULL);
    if(rows<tracker.song.pattern_rows)
        for(int pattern=0;pattern<tracker.song.pattern_count;++pattern) {
            SamplePattern *current=pattern==tracker.pattern_index?
                &tracker.pattern:&tracker.song.pattern[pattern];
            for(int row=rows;row<SAMPLE_MAX_STEPS;++row)
                for(int channel=0;channel<SAMPLE_CHANNELS;++channel)
                    current->cell[row][channel]=
                        (SampleCell){-1,-1,SAMPLE_EFFECT_NONE,-1};
        }
    tracker.song.bpm=bpm;
    tracker.song.pattern_rows=rows;
    tracker.song.rows_per_beat=beat_rows;
    tracker.song.ticks_per_row=ticks;
    tracker.step=clamp(tracker.step,0,rows-1);
    tracker.anchor_step=clamp(tracker.anchor_step,0,rows-1);
    tracker.selecting=0;
    tracker.reveal_cursor=1;
    close_settings(NULL,NULL);
    message("Song settings updated");
}
static void show_settings(F80Widget *button,void *user) {
    (void)button;(void)user;
    static const char *labels[]={"BPM (30-300)","Pattern rows (16-256)",
                                 "Rows per beat (1-16)","Ticks per row (1-48)"};
    int values[]={tracker.song.bpm,tracker.song.pattern_rows,
                  tracker.song.rows_per_beat,tracker.song.ticks_per_row};
    F80Widget *dialog=f80_dialog("Song settings",NULL,NULL,0,0);
    tracker.settings_dialog=dialog;
    for(int i=0;i<4;++i) {
        tracker.settings_label[i]=f80_add(dialog,f80_label(labels[i]));
        tracker.settings_label[i]->style=F80_COLOR_TEXT;
        tracker.settings_input[i]=f80_add(dialog,f80_input());
        snprintf(tracker.settings_input[i]->input,sizeof(tracker.settings_input[i]->input),
                 "%d",values[i]);
        tracker.settings_input[i]->input_pos=
            (int)strlen(tracker.settings_input[i]->input);
    }
    tracker.settings_apply=f80_add(dialog,f80_button("~A~pply",apply_settings,NULL));
    tracker.settings_close=f80_add(dialog,f80_button("~C~ancel",close_settings,NULL));
    f80_show_modal(&tracker.app,dialog);
}
static void close_order(F80Widget *button,void *user) {
    (void)button;(void)user;f80_hide_modal(&tracker.app);
    tracker.order_dialog=tracker.order_bank_label=tracker.order_bank_bg=tracker.order_bank=
        tracker.order_seq_label=tracker.order_seq_bg=tracker.order_seq=tracker.order_name=NULL;
}
static void show_order(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.playing)stop_playback(NULL,NULL);
    F80Widget *dialog=f80_dialog("Patterns and play order",NULL,NULL,0,0);
    dialog->dialog_default=1;
    tracker.order_dialog=dialog;
    tracker.order_bank_label=f80_add(dialog,f80_label("PATTERN BANK"));
    tracker.order_bank_label->style=F80_COLOR_TEXT;
    tracker.order_bank_bg=f80_add(dialog,f80_window(F80_WINDOW_CYAN,F80_COL));
    tracker.order_bank=f80_add(dialog,f80_listbox(tracker.bank_items,
                        tracker.song.pattern_count,order_bank_selected,NULL));
    tracker.order_bank->window_role=F80_WINDOW_CYAN;
    tracker.order_bank->cursor=tracker.pattern_index;
    tracker.order_seq_label=f80_add(dialog,f80_label(tracker.order_heading));
    tracker.order_seq_label->style=F80_COLOR_TEXT;
    tracker.order_seq_bg=f80_add(dialog,f80_window(F80_WINDOW_CYAN,F80_COL));
    tracker.order_seq=f80_add(dialog,f80_listbox(tracker.order_items,
                       tracker.song.order_count,NULL,NULL));
    tracker.order_seq->window_role=F80_WINDOW_CYAN;
    tracker.order_name=f80_add(dialog,f80_input());
    f80_add(dialog,f80_button("~N~ew pattern",order_new_pattern,NULL));
    f80_add(dialog,f80_button("~R~ename",order_rename,NULL));
    f80_add(dialog,f80_button("~E~dit",order_edit,NULL));
    f80_add(dialog,f80_button("~A~dd to order",order_add,NULL));
    f80_add(dialog,f80_button("Remo~v~e",order_remove,NULL));
    f80_add(dialog,f80_button("~U~p",order_move,(void *)(long)-1));
    f80_add(dialog,f80_button("~D~own",order_move,(void *)(long)1));
    f80_add(dialog,f80_button("BPM -",order_bpm,(void *)(long)-1));
    f80_add(dialog,f80_button("BPM +",order_bpm,(void *)(long)1));
    f80_add(dialog,f80_button("~C~lose",close_order,NULL));
    order_refresh();order_bank_selected(tracker.order_bank,NULL);
    f80_show_modal(&tracker.app,dialog);
}
static void table_title(void) {
    snprintf(tracker.table_title,sizeof(tracker.table_title),"Table %02X / %d ticks",
             tracker.table_index,tracker.song.table[tracker.table_index].length);
}
static void table_cell_text(void *user,int tick,int field,char *out,size_t size) {
    (void)user;
    const SampleTableTick *entry=&tracker.song.table[tracker.table_index].tick[tick];
    if(field==0) {
        if(entry->note<0)snprintf(out,size,"...");
        else { char note[4];sample_note_name(entry->note,note);snprintf(out,size,"%s",note); }
    } else if(field==1) {
        if(entry->instrument<0)snprintf(out,size,"..");
        else snprintf(out,size,"%02X",entry->instrument);
    } else {
        const SampleTableEffect *fx=&entry->effect[field-2];
        if(!fx->command)snprintf(out,size,"...");
        else if(fx->value<0)snprintf(out,size,"%c..",fx->command);
        else snprintf(out,size,"%c%02X",fx->command,fx->value);
    }
}
static void paint_table(F80Widget *canvas,void *user) {
    (void)user;
    f80_grid_layout(&tracker.table_grid,canvas->rect);
    f80_grid_draw(&tracker.table_grid,table_cell_text,NULL);
}
static void paint_table_scrollbar(F80Widget *canvas,void *user) {
    (void)canvas;(void)user;
    f80_wrap_draw_scrollbar(&tracker.table_actions);
}
static void table_select(F80Widget *button,void *user) {
    (void)button;
    tracker.table_index=clamp(tracker.table_index+(int)(long)user,
                              0,SAMPLE_SONG_TABLES-1);
    tracker.table_grid.row=tracker.table_grid.scroll_y=0;
    tracker.table_grid.selecting=0;
    f80_grid_resize(&tracker.table_grid,tracker.song.table[tracker.table_index].length);
    if(tracker.table_name) {
        snprintf(tracker.table_name->input,sizeof(tracker.table_name->input),"%s",
                 tracker.song.table[tracker.table_index].name);
        tracker.table_name->input_pos=(int)strlen(tracker.table_name->input);
    }
    table_title();
}
static void table_resize(F80Widget *button,void *user) {
    (void)button;
    SampleTable *table=&tracker.song.table[tracker.table_index];
    table->length=clamp(table->length+(int)(long)user*SAMPLE_TABLE_MIN_TICKS,
                        SAMPLE_TABLE_MIN_TICKS,SAMPLE_TABLE_MAX_TICKS);
    f80_grid_resize(&tracker.table_grid,table->length);
    table_title();
}
static void close_table(F80Widget *button,void *user) {
    (void)button;(void)user;
    f80_hide_modal(&tracker.app);
    tracker.table_dialog=tracker.table_canvas=tracker.table_name=NULL;
}
static void show_table(F80Widget *button,void *user) {
    (void)button;(void)user;
    table_title();
    F80Widget *dialog=f80_dialog(tracker.table_title,NULL,NULL,0,0);
    tracker.table_dialog=dialog;
    tracker.table_name_label=f80_add(dialog,f80_label("Name"));
    tracker.table_name=f80_add(dialog,f80_input());
    snprintf(tracker.table_name->input,sizeof(tracker.table_name->input),"%s",
             tracker.song.table[tracker.table_index].name);
    tracker.table_name->input_pos=(int)strlen(tracker.table_name->input);
    tracker.table_panel=f80_add(dialog,f80_window(F80_WINDOW_BLUE,F80_COL));
    tracker.table_canvas=f80_add(tracker.table_panel,f80_canvas(paint_table,NULL));
    tracker.table_canvas->focusable=1;
    static const int widths[]={3,2,3,3,3,3,3,3};
    static const char *const headings[]={"NTE","IN","FX1","FX2","FX3","FX4","FX5","FX6"};
    f80_grid_init(&tracker.table_grid,tracker.song.table[tracker.table_index].length,
                  8,widths,headings);
    f80_wrap_init(&tracker.table_actions,1,0);
    tracker.table_buttons[0]=f80_add(dialog,f80_button("Tbl -",table_select,(void *)(long)-1));
    tracker.table_buttons[1]=f80_add(dialog,f80_button("Tbl +",table_select,(void *)(long)1));
    tracker.table_buttons[2]=f80_add(dialog,f80_button("Len -",table_resize,(void *)(long)-1));
    tracker.table_buttons[3]=f80_add(dialog,f80_button("Len +",table_resize,(void *)(long)1));
    tracker.table_buttons[4]=f80_add(dialog,f80_button("~C~lose",close_table,NULL));
    tracker.table_scrollbar=f80_add(dialog,f80_canvas(paint_table_scrollbar,NULL));
    f80_show_modal(&tracker.app,dialog);
    tracker.app.focus=tracker.table_canvas;
}
static void show_help(F80Widget *button,void *user) {
    (void)button;(void)user;
    static const char *lines[]={
        "File > Load WAV adds a sample; Open/Save Song handles the whole song.",
        "Tab switches Waveform and Pattern.",
        "Wave: F1/F2 previous/next sample; F3/F4 previous/next marker.",
        "Wave: Ctrl+Left/Right selects markers; Shift+Left/Right moves one.",
        "Wave: F7/F8 or Down/Up zoom; Left/Right pans; 0-F selects.",
        "Pattern: Up/Down row; Left/Right moves note, instrument, FX, value.",
        "FX: G I J K M N O P R S T V W; values 00-FF.",
        "Note: Z-M and Q-U play two keyboard octaves; [/] change octave.",
        "Delete clears the selected field. Enter auditions. F9 menu; F10 exits.",
        "SLC04 plays 0 to 4; SFA plays F to A backward.",
        "Sampler > Manage reorders, renames, deletes and retunes samples.",
        "Sampler > Slices sets a base note for each marker-to-marker slice.",
        "Song > Settings changes BPM, pattern rows, rows per beat, and ticks.",
        "Song > Patterns and order edits the bank and playback sequence.",
        "Song > Tables edits one tick lane with note, instrument, six FX.",
        "TBLxx on a pattern channel starts table xx on that channel.",
        "Table: Tab name/grid; swipe to scroll; hold or Shift+arrows selects."
    };
    F80Widget *dialog=f80_dialog("Sample tracker keys",lines,NULL,17,0);
    dialog->dialog_align=1;
    f80_add(dialog,f80_button("~C~lose",close_help,NULL));
    f80_show_modal(&tracker.app,dialog);
}
static void pull_menu(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.app.modal) {
#ifndef __EMSCRIPTEN__
        if(f80_file_dialog_active(&file_dialog))f80_file_dialog_close(&file_dialog);
        else
#endif
            if(tracker.app.modal==tracker.manage_dialog)close_manage(NULL,NULL);
            else if(tracker.app.modal==tracker.slices_dialog)close_slices(NULL,NULL);
            else if(tracker.app.modal==tracker.order_dialog)close_order(NULL,NULL);
            else if(tracker.app.modal==tracker.table_dialog)close_table(NULL,NULL);
            else if(tracker.app.modal==tracker.settings_dialog)close_settings(NULL,NULL);
            else f80_hide_modal(&tracker.app);
    }
    f80_menu_open(tracker.app.menu,0);
}
static const F80MenuItem file_items[]={
    {"~L~oad WAV...",NULL,open_load,1},
    {"~O~pen song...",NULL,open_song,1},
    {"~S~ave song...",NULL,save_song,1},
    {"~Q~uit",NULL,quit,1}
};
static const F80MenuItem play_items[]={
    {"~S~tart song",NULL,start_song,1},
    {"Loop ~c~urrent pattern",NULL,start_pattern_loop,1},
    {"~P~ause / resume",NULL,toggle_play,1},
    {"S~t~op",NULL,stop_playback,1},
    {"~L~oop song order",NULL,toggle_loop,1}
};
static const F80MenuItem sampler_items[]={
    {"~M~anage...",NULL,show_manage,1},
    {"~S~lices...",NULL,show_slices,1}
};
static const F80MenuItem song_items[]={
    {"~S~ettings...",NULL,show_settings,1},
    {"~P~atterns and order...",NULL,show_order,1},
    {"~T~ables...",NULL,show_table,1}
};
static const F80MenuItem view_items[]={
    {"Zoom ~o~ut",NULL,zoom_out,1},{"Zoom ~i~n",NULL,zoom_in,1},
    {"~W~aveform",NULL,focus_wave,1},{"P~a~ttern",NULL,focus_phrase,1}
};
static const F80MenuItem help_items[]={ {"~K~eys and format",NULL,show_help,1} };
static const F80Menu menus[]={
    {"~F~ile",file_items,sizeof(file_items)/sizeof(*file_items)},
    {"~S~ampler",sampler_items,sizeof(sampler_items)/sizeof(*sampler_items)},
    {"S~o~ng",song_items,sizeof(song_items)/sizeof(*song_items)},
    {"~P~lay",play_items,sizeof(play_items)/sizeof(*play_items)},
    {"~V~iew",view_items,sizeof(view_items)/sizeof(*view_items)},
    {"~H~elp",help_items,sizeof(help_items)/sizeof(*help_items)}
};
static const F80StatusItem wave_status[]={
    {"1","Sm-",previous_sample},{"2","Sm+",next_sample},
    {"3","Mk-",previous_marker},{"4","Mk+",next_marker},
    {"5","Mv-",value_down},{"6","Mv+",value_up},
    {"7","Zm+",zoom_in},{"8","Zm-",zoom_out},
    {"9","Men",pull_menu},{"10","Xit",quit}
};
static void prev_pattern(F80Widget *button,void *user) {
    (void)button;(void)user;
    select_edit_pattern(clamp(tracker.pattern_index-1,0,tracker.song.pattern_count-1));
}
static void next_pattern(F80Widget *button,void *user) {
    (void)button;(void)user;
    select_edit_pattern(clamp(tracker.pattern_index+1,0,tracker.song.pattern_count-1));
}
static void clear_field(F80Widget *button,void *user) {
    (void)button;(void)user;
    SampleCell *cell=&tracker.pattern.cell[tracker.step][tracker.channel];
    if(tracker.field==0)cell->note=-1;
    else if(tracker.field==1)cell->instrument=-1;
    else if(tracker.field==2)set_effect(cell,SAMPLE_EFFECT_NONE);
    else if(cell->effect!=SAMPLE_EFFECT_NONE)cell->value=0;
}
static void sample_down(F80Widget *button,void *user) {
    (void)button;(void)user;
    SampleCell *cell=&tracker.pattern.cell[tracker.step][tracker.channel];
    cell->instrument=clamp((cell->instrument<1?tracker.sample_index+1:cell->instrument)-1,
                           1,tracker.sample_count);
}
static void sample_up(F80Widget *button,void *user) {
    (void)button;(void)user;
    SampleCell *cell=&tracker.pattern.cell[tracker.step][tracker.channel];
    cell->instrument=clamp((cell->instrument<1?tracker.sample_index+1:cell->instrument)+1,
                           1,tracker.sample_count);
}
static void use_m(F80Widget *button,void *user) {
    (void)button;(void)user;
    set_effect(&tracker.pattern.cell[tracker.step][tracker.channel],SAMPLE_EFFECT_M);
}
static void use_s(F80Widget *button,void *user) {
    (void)button;(void)user;
    set_effect(&tracker.pattern.cell[tracker.step][tracker.channel],SAMPLE_EFFECT_S);
}
static void repeat_above(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.step==0)return;
    SampleCell *dst=&tracker.pattern.cell[tracker.step][tracker.channel];
    const SampleCell *src=&tracker.pattern.cell[tracker.step-1][tracker.channel];
    if(tracker.field==2) { dst->effect=src->effect;dst->value=src->value; }
    else dst->value=src->value;
}
static void select_transpose(int delta) {
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    for(int row=top;row<=bottom;++row)for(int channel=left;channel<=right;++channel) {
        SampleCell *cell=&tracker.pattern.cell[row][channel];
        if(cell->note>=0)cell->note=clamp(cell->note+delta,12,127);
    }
}
static void transpose_down(F80Widget *button,void *user) {
    (void)button;(void)user;select_transpose(-1);
}
static void transpose_up(F80Widget *button,void *user) {
    (void)button;(void)user;select_transpose(1);
}
static void copy_selection(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(!tracker.selecting)return;
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    tracker.clip_rows=bottom-top+1;tracker.clip_channels=right-left+1;
    for(int row=0;row<tracker.clip_rows;++row)
        for(int channel=0;channel<tracker.clip_channels;++channel)
            tracker.clipboard[row][channel]=tracker.pattern.cell[top+row][left+channel];
    message("Selection copied");
}
static void clear_selection(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(!tracker.selecting)return;
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    for(int row=top;row<=bottom;++row)
        for(int channel=left;channel<=right;++channel)
            tracker.pattern.cell[row][channel]=(SampleCell){-1,-1,SAMPLE_EFFECT_NONE,-1};
    message("Selection cleared");
}
static void cut_selection(F80Widget *button,void *user) {
    copy_selection(button,user);clear_selection(button,user);message("Selection cut");
}
static int paste_available(void) {
    if(!tracker.selecting || !tracker.clip_rows)return 0;
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    return bottom-top+1==tracker.clip_rows && right-left+1==tracker.clip_channels;
}
static void paste_selection(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(!paste_available()) { message("Select an area matching the clipboard size");return; }
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    for(int row=0;row<tracker.clip_rows;++row)
        for(int channel=0;channel<tracker.clip_channels;++channel)
            tracker.pattern.cell[top+row][left+channel]=tracker.clipboard[row][channel];
    message("Selection pasted");
}
static void oct_down(F80Widget *button,void *user);
static void oct_up(F80Widget *button,void *user);
static const F80StatusItem note_status[]={
    {"1","Pt-",prev_pattern},{"2","Pt+",next_pattern},
    {"3","Oc-",oct_down},{"4","Oc+",oct_up},
    {"5","St-",value_down},{"6","St+",value_up},
    {"7","Ord",show_order},{"8","Clr",clear_field},
    {"9","Men",pull_menu},{"10","Xit",quit}
};
static const F80StatusItem instrument_status[]={
    {"1","Pt-",prev_pattern},{"2","Pt+",next_pattern},
    {"3","Sm-",sample_down},{"4","Sm+",sample_up},
    {"5","Mrk",show_slices},{"6","Mgr",show_manage},
    {"7","New",open_load},{"8","Clr",clear_field},
    {"9","Men",pull_menu},{"10","Xit",quit}
};
static const F80StatusItem effect_status[]={
    {"1","Pt-",prev_pattern},{"2","Pt+",next_pattern},
    {"3","FX-",value_down},{"4","FX+",value_up},
    {"5","MVL",use_m},{"6","SLC",use_s},
    {"7","Rpt",repeat_above},{"8","Clr",clear_field},
    {"9","Men",pull_menu},{"10","Xit",quit}
};
static const F80StatusItem value_status[]={
    {"1","Pt-",prev_pattern},{"2","Pt+",next_pattern},
    {"3","-16",value_down_16},{"4","+16",value_up_16},
    {"5","-1",value_down},{"6","+1",value_up},
    {"7","Rpt",repeat_above},{"8","Clr",clear_field},
    {"9","Men",pull_menu},{"10","Xit",quit}
};
static const F80StatusItem selection_status[]={
    {"1","Pt-",prev_pattern},{"2","Pt+",next_pattern},
    {"3","Tr-",transpose_down},{"4","Tr+",transpose_up},
    {"5","Cpy",copy_selection},{"6","Cut",cut_selection},
    {"7","Pst",paste_selection},{"8","Clr",clear_selection},
    {"9","Men",pull_menu},{"10","Xit",quit}
};
static const F80StatusItem reference_status[]={
    {"1","Hlp",show_help},{"2","Lod",open_load},
    {"3","Oc-",oct_down},{"4","Oc+",oct_up},
    {"5","Rf-",value_down},{"6","Rf+",value_up},
    {"7","Aud",audition},{"8","Ply",toggle_play},
    {"9","Men",pull_menu},{"10","Xit",quit}
};
static F80StatusItem selection_visible[10];
static const F80StatusItem *current_status(void) {
    if(tracker.focus==FOCUS_WAVE)return wave_status;
    if(tracker.focus==FOCUS_REFERENCE)return reference_status;
    if(tracker.selecting) {
        memcpy(selection_visible,selection_status,sizeof(selection_visible));
        if(!paste_available()) {
            selection_visible[6].label="---";
            selection_visible[6].action=NULL;
        }
        return selection_visible;
    }
    return tracker.field==0?note_status:tracker.field==1?instrument_status:
           tracker.field==2?effect_status:value_status;
}
static void change_octave(int delta) {
    tracker.entry_octave=clamp(tracker.entry_octave+delta,0,8);
    message("Keyboard entry octave changed");
}
static void oct_down(F80Widget *button,void *user) {
    (void)button;(void)user;change_octave(-1);
}
static void oct_up(F80Widget *button,void *user) {
    (void)button;(void)user;change_octave(1);
}
static int hex_digit(int key) {
    if(key>='0' && key<='9')return key-'0';
    if(key>='a' && key<='f')return key-'a'+10;
    if(key>='A' && key<='F')return key-'A'+10;
    return -1;
}
static int piano_offset(int key) {
    const char *low="zsxdcvgbhnjm";
    const int low_note[]={0,1,2,3,4,5,6,7,8,9,10,11};
    const char *high="q2w3er5t6y7u";
    const int high_note[]={12,13,14,15,16,17,18,19,20,21,22,23};
    const char *at=strchr(low,key);
    if(at)return low_note[at-low];
    at=strchr(high,key);
    return at?high_note[at-high]:-1;
}
static void table_assign_field(SampleTableTick *dst,const SampleTableTick *src,int field) {
    if(field==0)dst->note=src->note;
    else if(field==1)dst->instrument=src->instrument;
    else dst->effect[field-2]=src->effect[field-2];
}
static void table_clear_area(void) {
    int top,left,bottom,right;
    f80_grid_bounds(&tracker.table_grid,&top,&left,&bottom,&right);
    SampleTable *table=&tracker.song.table[tracker.table_index];
    for(int row=top;row<=bottom;++row)
        for(int field=left;field<=right;++field) {
            SampleTableTick *entry=&table->tick[row];
            if(field==0)entry->note=-1;
            else if(field==1)entry->instrument=-1;
            else entry->effect[field-2]=(SampleTableEffect){0,-1};
        }
}
static void table_copy_area(void) {
    int top,left,bottom,right;
    f80_grid_bounds(&tracker.table_grid,&top,&left,&bottom,&right);
    SampleTable *table=&tracker.song.table[tracker.table_index];
    tracker.table_clip_rows=bottom-top+1;
    tracker.table_clip_left=left;tracker.table_clip_right=right;
    for(int row=0;row<tracker.table_clip_rows;++row)
        tracker.table_clip[row]=table->tick[top+row];
    message("Table selection copied");
}
static void table_paste_area(void) {
    int top,left,bottom,right;
    f80_grid_bounds(&tracker.table_grid,&top,&left,&bottom,&right);
    if(!tracker.table_clip_rows || left!=tracker.table_clip_left ||
       right!=tracker.table_clip_right || bottom-top+1!=tracker.table_clip_rows) {
        message("Select matching table fields and ticks to paste");return;
    }
    SampleTable *table=&tracker.song.table[tracker.table_index];
    for(int row=0;row<tracker.table_clip_rows;++row)
        for(int field=left;field<=right;++field)
            table_assign_field(&table->tick[top+row],&tracker.table_clip[row],field);
    message("Table selection pasted");
}
static void handle_table_key(int key) {
    if(key==9 || key==F80_KEY_BTAB) {
        tracker.app.focus=tracker.app.focus==tracker.table_name?
                          tracker.table_canvas:tracker.table_name;
        return;
    }
    if(tracker.app.focus==tracker.table_name) {
        f80_key(&tracker.app,key);
        if(strlen(tracker.table_name->input)>=SAMPLE_SONG_NAME) {
            tracker.table_name->input[SAMPLE_SONG_NAME-1]=0;
            tracker.table_name->input_pos=clamp(tracker.table_name->input_pos,0,
                                                SAMPLE_SONG_NAME-1);
        }
        if(tracker.table_name->input[0])
            snprintf(tracker.song.table[tracker.table_index].name,SAMPLE_SONG_NAME,
                     "%s",tracker.table_name->input);
        return;
    }
    if(key==2005) { table_copy_area();return; }
    if(key==2006) { table_copy_area();table_clear_area();message("Table selection cut");return; }
    if(key==2007) { table_paste_area();return; }
    if(key==2008) { table_clear_area();message("Table selection cleared");return; }
    if(f80_grid_key(&tracker.table_grid,key))return;
    SampleTable *table=&tracker.song.table[tracker.table_index];
    SampleTableTick *entry=&table->tick[tracker.table_grid.row];
    if(key=='[')table_select(NULL,(void *)(long)-1);
    else if(key==']')table_select(NULL,(void *)(long)1);
    else if(key=='-')table_resize(NULL,(void *)(long)-1);
    else if(key=='+' || key=='=')table_resize(NULL,(void *)(long)1);
    else if(key==F80_KEY_DELETE || key==F80_KEY_BACKSPACE || key=='.')
        table_clear_area();
    else if(tracker.table_grid.field==0) {
        int note=piano_offset(key);
        if(note>=0) {
            entry->note=clamp((tracker.entry_octave+1)*12+note,0,127);
        }
    } else if(tracker.table_grid.field==1) {
        int digit=hex_digit(key);
        if(digit>=0)entry->instrument=((entry->instrument<0?0:entry->instrument)<<4 & 0xf0)|digit;
    } else {
        SampleTableEffect *fx=&entry->effect[tracker.table_grid.field-2];
        int digit=hex_digit(key);
        if(key>='g' && key<='z')key-=32;
        if(key>='G' && key<='Z' && strchr("GIJKMNOPRSTUVWXY",key)) {
            if(fx->command!=key) { fx->command=(char)key;fx->value=-1; }
        }
        else if(digit>=0 && fx->command)
            fx->value=((fx->value<0?0:fx->value)<<4 & 0xf0)|digit;
    }
}
static void update_labels(void) {
    tracker.wave_frame->style=tracker.wave.pcm?0:15;
    if(tracker.order_list) {
        tracker.order_list->item_count=tracker.song.order_count;
        tracker.order_list->cursor=clamp(tracker.order_list->cursor,0,
                                        tracker.song.order_count-1);
        for(int i=0;i<tracker.song.order_count;++i) {
            snprintf(tracker.order_panel_rows[i],sizeof(tracker.order_panel_rows[i]),
                     "%c%02X->%02X",tracker.playing &&
                     tracker.active_order_index==i?'>':' ',
                     i,tracker.song.order[i]);
            tracker.order_panel_items[i]=tracker.order_panel_rows[i];
        }
    }
    char reference[4];sample_note_name(tracker.phrase.reference_note,reference);
    int rows,cols;getmaxyx(stdscr,rows,cols);(void)rows;
    snprintf(tracker.header_text,sizeof(tracker.header_text),
             "%s",cols<70?"Tracker":"Sample Tracker");
    tracker.play_button->text=tracker.playing?"~P~ause":"~P~lay";
    tracker.follow_button->text=tracker.follow?"~F~ollow":"~H~old";
    tracker.mode_button->text=tracker.play_mode==PLAY_SONG?"~S~ong":"~P~attern";
    snprintf(tracker.bpm_text,sizeof(tracker.bpm_text),"BPM %d",tracker.song.bpm);
    int total=tracker.play_mode==PLAY_SONG?
              tracker.song.order_count*tracker.song.pattern_rows:tracker.song.pattern_rows;
    int current=tracker.active_step>=0?
                (tracker.play_mode==PLAY_SONG && tracker.active_order_index>=0?
                 tracker.active_order_index*tracker.song.pattern_rows:0)+
                tracker.active_step:0;
    tracker.progress->value=total>0?(float)current/total:0;
    snprintf(tracker.wave_title,sizeof(tracker.wave_title),"%02d. %.145s",
             tracker.sample_index+1,tracker.wave.name);
    tracker.wave_frame->title=tracker.wave_title;
    if(tracker.playing && tracker.play_mode==PLAY_PATTERN)
        snprintf(tracker.phrase_title,sizeof(tracker.phrase_title),
                 "%02X. %.48s / loop %02X",tracker.pattern_index,
                 tracker.song.name[tracker.pattern_index],tracker.loop_pattern_index);
    else if(tracker.playing && tracker.active_order_index>=0)
        snprintf(tracker.phrase_title,sizeof(tracker.phrase_title),
                 "%02X. %.42s / Order %02X/%02X / %s",tracker.pattern_index,
                 tracker.song.name[tracker.pattern_index],
                 tracker.active_order_index,tracker.song.order_count-1,
                 tracker.loop?"loop":"once");
    else
        snprintf(tracker.phrase_title,sizeof(tracker.phrase_title),
                 "%02X. %.50s / %d channels / %s",tracker.pattern_index,
                 tracker.song.name[tracker.pattern_index],SAMPLE_CHANNELS,
                 tracker.loop?"loop":"once");
    tracker.phrase_frame->title=tracker.phrase_title;
    if(tracker.focus==FOCUS_WAVE)
        snprintf(tracker.details_text,sizeof(tracker.details_text),
                 "WAVE  marker %X %.3fs  |  %s",tracker.marker,
                 (double)tracker.phrase.marker[tracker.marker]/tracker.wave.rate,
                 tracker.message);
    else if(tracker.focus==FOCUS_REFERENCE)
        snprintf(tracker.details_text,sizeof(tracker.details_text),
                 "REFERENCE  %s  |  %s",reference,tracker.message);
    else if(tracker.selecting) {
        int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
        snprintf(tracker.details_text,sizeof(tracker.details_text),
                 "SELECT R%02X-%02X CH%d-%d (%d x %d) | CLIP %d x %d%s | %s",
                 top,bottom,left+1,right+1,bottom-top+1,right-left+1,
                 tracker.clip_rows,tracker.clip_channels,
                 paste_available()?" PASTE READY":"",tracker.message);
    } else {
        char compact[11],shown[13];
        sample_cell_text(&tracker.pattern.cell[tracker.step][tracker.channel],compact);
        snprintf(shown,sizeof(shown),"%.3s %.2s %.5s",compact,compact+3,compact+5);
        snprintf(tracker.details_text,sizeof(tracker.details_text),
                 "ROW %02X CH%d  %s  %s  Oct %d  |  %s",tracker.step,tracker.channel+1,
                 shown,(const char *const[]){"NOTE","INST","FX","VALUE"}[tracker.field],
                 tracker.entry_octave,
                 tracker.message);
    }
}
static void handle_key(int key) {
    if(key==2010) { quit(NULL,NULL);return; }
    if(key==2009) { pull_menu(NULL,NULL);return; }
    if(tracker.app.modal) {
#ifndef __EMSCRIPTEN__
        if(f80_file_dialog_active(&file_dialog)) {
            f80_file_dialog_key(&file_dialog,key);
            return;
        }
#endif
        if(key==27) {
            if(tracker.app.modal==tracker.manage_dialog)close_manage(NULL,NULL);
            else if(tracker.app.modal==tracker.slices_dialog)close_slices(NULL,NULL);
            else if(tracker.app.modal==tracker.order_dialog)close_order(NULL,NULL);
            else if(tracker.app.modal==tracker.table_dialog)close_table(NULL,NULL);
            else close_help(NULL,NULL);
        } else if(tracker.app.modal==tracker.table_dialog) {
            handle_table_key(key);
        } else if(tracker.app.modal==tracker.manage_dialog &&
                  tracker.app.focus==tracker.manage_list &&
                  (key==F80_KEY_UP || key==F80_KEY_DOWN || key==F80_KEY_HOME || key==F80_KEY_END)) {
            tracker.manage_list->cursor=key==F80_KEY_HOME?0:key==F80_KEY_END?
                tracker.sample_count-1:clamp(tracker.manage_list->cursor+
                (key==F80_KEY_UP?-1:1),0,tracker.sample_count-1);
            manage_selected(tracker.manage_list,NULL);
        } else if(tracker.app.modal==tracker.slices_dialog &&
                  tracker.app.focus==tracker.slices_list &&
                  (key==F80_KEY_UP || key==F80_KEY_DOWN || key==F80_KEY_HOME || key==F80_KEY_END)) {
            tracker.slices_list->cursor=key==F80_KEY_HOME?0:key==F80_KEY_END?
                SAMPLE_SLICES-1:clamp(tracker.slices_list->cursor+
                (key==F80_KEY_UP?-1:1),0,SAMPLE_SLICES-1);
            slices_selected(tracker.slices_list,NULL);
        } else if(tracker.app.modal==tracker.order_dialog &&
                  (tracker.app.focus==tracker.order_bank ||
                   tracker.app.focus==tracker.order_seq) &&
                  (key==F80_KEY_UP || key==F80_KEY_DOWN ||
                   key==F80_KEY_HOME || key==F80_KEY_END)) {
            F80Widget *list=tracker.app.focus;
            int last=list->item_count-1;
            list->cursor=key==F80_KEY_HOME?0:key==F80_KEY_END?last:
                clamp(list->cursor+(key==F80_KEY_UP?-1:1),0,last);
            if(list==tracker.order_bank)order_bank_selected(list,NULL);
        } else f80_key(&tracker.app,key);
        return;
    }
    if(tracker.app.menu && tracker.app.menu->menu_active) {
        f80_key(&tracker.app,key);return;
    }
    switch(key) {
    case 10:case 13:audition(NULL,NULL);return;
    case ' ':toggle_play(NULL,NULL);return;
    case 9:case F80_KEY_BTAB:
        tracker.focus=tracker.focus==FOCUS_PHRASE?FOCUS_WAVE:FOCUS_PHRASE;
        return;
    case 27:case 'Q':quit(NULL,NULL);return;
    case '[':change_octave(-1);return;
    case ']':change_octave(1);return;
    case 'L':toggle_loop(NULL,NULL);return;
    default:break;
    }
    if(key>=2001 && key<=2008) {
        F80Action action=current_status()[key-2001].action;
        if(action)action(NULL,NULL);
        return;
    }
    if(tracker.focus==FOCUS_WAVE) {
        int digit=hex_digit(key);
        if(digit>=0)focus_marker(digit);
        else if(key==F80_KEY_CTRL_LEFT)previous_marker(NULL,NULL);
        else if(key==F80_KEY_CTRL_RIGHT)next_marker(NULL,NULL);
        else if(key==F80_KEY_SHIFT_LEFT)value_down(NULL,NULL);
        else if(key==F80_KEY_SHIFT_RIGHT)value_up(NULL,NULL);
        else if(key==F80_KEY_UP)zoom_wave(-1);
        else if(key==F80_KEY_DOWN)zoom_wave(1);
        else if(key==F80_KEY_LEFT)pan_wave(-1);
        else if(key==F80_KEY_RIGHT)pan_wave(1);
        else if(key=='+' || key=='=')zoom_wave(1);
        else if(key=='-')zoom_wave(-1);
        else f80_key(&tracker.app,key);
    } else if(tracker.focus==FOCUS_PHRASE) {
        if(key==F80_KEY_SHIFT_UP || key==F80_KEY_SHIFT_DOWN ||
           key==F80_KEY_SHIFT_LEFT || key==F80_KEY_SHIFT_RIGHT) {
            if(!tracker.selecting) {
                tracker.anchor_step=tracker.step;
                tracker.anchor_channel=tracker.channel;
                tracker.selecting=1;
            }
            if(key==F80_KEY_SHIFT_UP)tracker.step=clamp(tracker.step-1,0,tracker.song.pattern_rows-1);
            else if(key==F80_KEY_SHIFT_DOWN)tracker.step=clamp(tracker.step+1,0,tracker.song.pattern_rows-1);
            else if(key==F80_KEY_SHIFT_LEFT)tracker.channel=clamp(tracker.channel-1,0,SAMPLE_CHANNELS-1);
            else tracker.channel=clamp(tracker.channel+1,0,SAMPLE_CHANNELS-1);
            if(key==F80_KEY_SHIFT_UP || key==F80_KEY_SHIFT_DOWN)
                tracker.reveal_cursor=1;
            return;
        }
        if(key==F80_KEY_UP || key==F80_KEY_DOWN || key==F80_KEY_LEFT ||
           key==F80_KEY_RIGHT || key==27)tracker.selecting=0;
        if(tracker.selecting)return;
        SampleCell *entry=&tracker.pattern.cell[tracker.step][tracker.channel];
        int digit=hex_digit(key);
        if(key==F80_KEY_UP) {
            tracker.step=clamp(tracker.step-1,0,tracker.song.pattern_rows-1);tracker.reveal_cursor=1;
        } else if(key==F80_KEY_DOWN) {
            tracker.step=clamp(tracker.step+1,0,tracker.song.pattern_rows-1);tracker.reveal_cursor=1;
        }
        else if(key==F80_KEY_LEFT) {
            if(tracker.field)tracker.field--;
            else if(tracker.channel) { tracker.channel--;tracker.field=3; }
        } else if(key==F80_KEY_RIGHT) {
            if(tracker.field<3)tracker.field++;
            else if(tracker.channel<SAMPLE_CHANNELS-1) {
                tracker.channel++;tracker.field=0;
            }
        } else if(key==F80_KEY_DELETE || key==F80_KEY_BACKSPACE || key=='.') {
            if(tracker.field==0)entry->note=-1;
            else if(tracker.field==1)entry->instrument=-1;
            else if(tracker.field==2)set_effect(entry,SAMPLE_EFFECT_NONE);
            else if(entry->effect!=SAMPLE_EFFECT_NONE)entry->value=0;
        } else if(tracker.field==1 && digit>=0) {
            entry->instrument=digit;
        } else if(tracker.field==2 &&
                  strchr("GIJKMNOPRSTVW",key>='a' && key<='z'?key-32:key))
            set_effect(entry,key>='a' && key<='z'?key-32:key);
        else if(tracker.field==3 && digit>=0) {
            if(entry->effect==SAMPLE_EFFECT_NONE)set_effect(entry,SAMPLE_EFFECT_S);
            entry->value=(((entry->value<0?0:entry->value)<<4)&0xf0)|digit;
        }
        else if(tracker.field==0) {
            int offset=piano_offset(key);
            if(offset>=0) {
                entry->note=clamp((tracker.entry_octave+1)*12+offset,12,127);
                if(entry->instrument>=0)audition_cell(tracker.step,tracker.channel);
            } else if(key=='+' || key=='=')adjust_value(1);
            else if(key=='-')adjust_value(-1);
            else f80_key(&tracker.app,key);
        } else f80_key(&tracker.app,key);
    } else {
        if(key==F80_KEY_UP || key==F80_KEY_RIGHT || key=='+')adjust_value(1);
        else if(key==F80_KEY_DOWN || key==F80_KEY_LEFT || key=='-')adjust_value(-1);
        else f80_key(&tracker.app,key);
    }
}
static void handle_mouse(int x,int y,int wheel) {
    if(wheel==3)return;
    if(tracker.app.modal) {
        if(tracker.app.modal==tracker.table_dialog &&
           f80_wrap_mouse(&tracker.table_actions,x,y,wheel,
                          tracker.table_buttons,5))return;
        if(tracker.app.modal==tracker.table_dialog && tracker.table_canvas &&
           f80_contains(tracker.table_canvas->rect,x,y)) {
            f80_grid_mouse(&tracker.table_grid,x,y,wheel);
            tracker.app.focus=tracker.table_canvas;
            return;
        }
#ifndef __EMSCRIPTEN__
        if(f80_file_dialog_active(&file_dialog))
            f80_file_dialog_mouse(&file_dialog,x,y,wheel);
        else
#endif
            f80_mouse(&tracker.app,x,y,wheel);
        return;
    }
    if(tracker.app.menu && !wheel &&
       (tracker.app.menu->menu_active || y==tracker.app.menu->rect.y)) {
        f80_mouse(&tracker.app,x,y,wheel);return;
    }
    F80Rect r=tracker.wave_canvas->rect;
    if(f80_contains(tracker.header->rect,x,y)) {
        f80_mouse(&tracker.app,x,y,wheel);
        return;
    }
    if(tracker.order_list && f80_contains(tracker.order_list->rect,x,y)) {
        f80_mouse(&tracker.app,x,y,wheel);
        return;
    }
    if(f80_contains(r,x,y)) {
        tracker.focus=FOCUS_WAVE;
        if(!tracker.wave.pcm) {
            if(!wheel && y==r.y+r.h-2) {
                if(!recover_missing(NULL))message("WAV still missing; check its saved path");
            }
            return;
        }
        if(wheel) { zoom_wave(wheel<0?1:-1);return; }
        if(y==r.y+r.h-1) {
            int local=x-r.x,visible=visible_frames();
            if(local==0)pan_wave(-1);
            else if(local==r.w-1)pan_wave(1);
            else {
                tracker.view_start=(int)((long long)(local-1)*
                    (tracker.wave.frames-visible)/(r.w-2));
                clamp_view();
            }
        } else if(y>r.y) {
            int marker=wave_marker_at(x);
            if(marker>=0) {
                tracker.marker=marker;
                char status[48];
                snprintf(status,sizeof(status),"Marker %X selected",marker);
                message(status);
            }
        }
        return;
    }
    r=tracker.phrase_canvas->rect;
    if(f80_contains(r,x,y)) {
        tracker.focus=FOCUS_PHRASE;
        int body=r.h-3,maximum=grid_total_rows()-body;
        if(maximum<0)maximum=0;
        if(wheel && wheel!=2) {
            tracker.phrase_scroll=clamp(tracker.phrase_scroll+wheel*3,0,maximum);
            return;
        }
        if(x==r.x+r.w-1) {
            if(y==r.y)tracker.phrase_scroll=clamp(tracker.phrase_scroll-1,0,maximum);
            else if(y==r.y+r.h-1)
                tracker.phrase_scroll=clamp(tracker.phrase_scroll+1,0,maximum);
            else tracker.phrase_scroll=clamp((y-r.y-1)*maximum/(r.h-2),0,maximum);
            return;
        }
        if(y==r.y+2) {
            int direction=x==r.x+4?-1:x==r.x+r.w-2?1:0;
            if(direction) {
                int maximum_field=pattern_max_field_scroll(r.w-6);
                tracker.field_scroll=clamp(tracker.field_scroll+direction,0,maximum_field);
                tracker.channel=tracker.field_scroll/4;
                tracker.field=tracker.field_scroll%4;
            }
            return;
        }
        if(y>=r.y+3) {
            int global=tracker.phrase_scroll+y-r.y-3;
            if(global>=grid_total_rows() ||
               (tracker.edit_order>=0 && global/tracker.song.pattern_rows!=tracker.edit_order))
                return;
            int old_step=tracker.step,old_channel=tracker.channel;
            tracker.step=global%tracker.song.pattern_rows;
            int position=r.x+5;
            for(int index=tracker.field_scroll;index<SAMPLE_CHANNELS*4 &&
                position<r.x+r.w-1;++index) {
                int field=index%4;
                if(index>tracker.field_scroll && field==0)++position;
                int width=pattern_field_width(field);
                if(x>=position && x<position+width) {
                    tracker.channel=index/4;
                    tracker.field=field;
                    break;
                }
                position+=width;
            }
            if(wheel==2) {
                if(!tracker.selecting) {
                    tracker.anchor_step=old_step;
                    tracker.anchor_channel=old_channel;
                    tracker.selecting=1;
                }
            } else tracker.selecting=0;
        }
        return;
    }
    f80_mouse(&tracker.app,x,y,wheel);
}
static int begin_touch_selection(int x,int y) {
    F80Rect r=tracker.phrase_canvas->rect;
    if(tracker.app.modal || x<r.x+5 || x>=r.x+r.w-1 ||
       y<r.y+3 || y>=r.y+r.h)return 0;
    int global=tracker.phrase_scroll+y-r.y-3;
    if(global>=grid_total_rows() ||
       (tracker.edit_order>=0 && global/tracker.song.pattern_rows!=tracker.edit_order))
        return 0;
    handle_mouse(x,y,0);
    tracker.anchor_step=tracker.step;
    tracker.anchor_channel=tracker.channel;
    tracker.selecting=1;
    return 1;
}
static void touch_scroll_rows(int rows) {
    F80Rect r=tracker.phrase_canvas->rect;
    int body=r.h-3;
    if(tracker.app.modal || body<1)return;
    int maximum=grid_total_rows()-body;
    if(maximum<0)maximum=0;
    tracker.phrase_scroll=clamp(tracker.phrase_scroll+rows,0,maximum);
}
static void touch_scroll_fields(int fields) {
    F80Rect r=tracker.phrase_canvas->rect;
    if(tracker.app.modal || r.w<7)return;
    int maximum=pattern_max_field_scroll(r.w-6);
    tracker.field_scroll=clamp(tracker.field_scroll+fields,0,maximum);
    /* Keep the cursor in the visible field so painting does not snap back. */
    tracker.channel=tracker.field_scroll/4;
    tracker.field=tracker.field_scroll%4;
    tracker.focus=FOCUS_PHRASE;
}
static struct {
    int x,view_start,visible,marker_frame;
} wave_touch;
#ifndef __EMSCRIPTEN__
static int wave_touch_area(int x,int y) {
    F80Rect r=tracker.wave_canvas->rect;
    return !tracker.app.modal && tracker.wave.pcm &&
           x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h-1;
}
#endif
static void begin_wave_gesture(int x) {
    tracker.focus=FOCUS_WAVE;
    wave_touch.x=x;
    wave_touch.view_start=tracker.view_start;
    wave_touch.visible=visible_frames();
}
static void pan_wave_touch(int x) {
    F80Rect r=tracker.wave_canvas->rect;
    if(tracker.app.modal || !tracker.wave.pcm || r.w<1)return;
    tracker.view_start=wave_touch.view_start+
        (int)((long long)(wave_touch.x-x)*wave_touch.visible/r.w);
    clamp_view();
}
static void hold_wave_marker(int x) {
    F80Rect r=tracker.wave_canvas->rect;
    if(tracker.app.modal || !tracker.wave.pcm || r.w<1)return;
    int frame=tracker.view_start+(int)((long long)(x-r.x)*visible_frames()/r.w);
    int nearest=0;
    long long best=(long long)tracker.wave.frames+1;
    for(int marker=0;marker<SAMPLE_MARKERS;++marker) {
        long long distance=llabs((long long)tracker.phrase.marker[marker]-frame);
        if(distance<best) { best=distance;nearest=marker; }
    }
    tracker.focus=FOCUS_WAVE;
    tracker.marker=nearest;
    wave_touch.marker_frame=tracker.phrase.marker[nearest];
    char status[64];
    snprintf(status,sizeof(status),"Marker %X selected; drag to move",nearest);
    message(status);
}
static void move_wave_marker_touch(int x) {
    F80Rect r=tracker.wave_canvas->rect;
    if(tracker.app.modal || !tracker.wave.pcm || r.w<1)return;
    x=clamp(x,r.x,r.x+r.w-1);
    int frame=wave_touch.marker_frame+
        (int)((long long)(x-wave_touch.x)*wave_touch.visible/r.w);
    sample_move_marker(&tracker.phrase,tracker.wave.frames,tracker.marker,frame);
}
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE void sample_tracker_begin_touch_selection(int x,int y) {
    begin_touch_selection(x,y);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_touch_scroll(int rows) {
    touch_scroll_rows(rows);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_touch_scroll_fields(int fields) {
    touch_scroll_fields(fields);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_scroll_order(int rows) {
    order_panel_scroll(rows);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_scroll_table(int ticks) {
    if(tracker.app.modal!=tracker.table_dialog || !tracker.table_canvas)return;
    f80_grid_scroll(&tracker.table_grid,0,ticks);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_scroll_table_horizontal(int cells) {
    if(tracker.app.modal!=tracker.table_dialog || !tracker.table_canvas)return;
    f80_grid_scroll(&tracker.table_grid,cells,0);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_scroll_table_actions(int rows) {
    if(tracker.app.modal!=tracker.table_dialog)return;
    f80_wrap_scroll(&tracker.table_actions,rows,tracker.table_buttons,5);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_hold_table_selection(int x,int y) {
    if(tracker.app.modal!=tracker.table_dialog || !tracker.table_canvas)return;
    f80_grid_mouse(&tracker.table_grid,x,y,0);
    f80_grid_hold(&tracker.table_grid);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_drag_table_selection(int x,int y) {
    if(tracker.app.modal!=tracker.table_dialog || !tracker.table_canvas)return;
    f80_grid_drag(&tracker.table_grid,x,y);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_begin_wave_gesture(int x) {
    begin_wave_gesture(x);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_pan_wave_touch(int x) {
    pan_wave_touch(x);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_hold_wave_marker(int x) {
    hold_wave_marker(x);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_move_wave_marker(int x) {
    move_wave_marker_touch(x);
}
EMSCRIPTEN_KEEPALIVE void sample_tracker_drag_selection(int x,int y) {
    handle_mouse(x,y,2);
}
/* Process browser key and pointer events inside their user-activation event.
   The File menu can then open the native browser picker synchronously. */
EMSCRIPTEN_KEEPALIVE void sample_tracker_flush_input(void) {
    for(int pending=0;pending<256;++pending) {
        int key=getch();
        if(key==ERR)break;
        if(key==KEY_MOUSE) {
            int x,y,wheel;
            if(f80_curses_mouse(&x,&y,&wheel)==OK)handle_mouse(x,y,wheel);
        } else handle_key(f80_curses_key(key));
    }
}
#endif
#ifndef __EMSCRIPTEN__
enum { NATIVE_TOUCH_GRID=1, NATIVE_TOUCH_WAVE=2,
       NATIVE_TOUCH_TABLE=3, NATIVE_TOUCH_WRAP=4 };
static struct {
    int down,kind,selecting,moved,x,y,current_x,current_y,last_x,last_y;
    Uint64 started_at;
} native_touch;
static int native_other_button_down;
static void native_poll_hold(void) {
    if(!native_touch.down || native_touch.moved || native_touch.selecting ||
       SDL_GetTicks()-native_touch.started_at<1000)return;
    if(native_touch.kind==NATIVE_TOUCH_WRAP)return;
    if(native_touch.kind==NATIVE_TOUCH_GRID)
        native_touch.selecting=begin_touch_selection(native_touch.current_x,native_touch.current_y);
    else if(native_touch.kind==NATIVE_TOUCH_TABLE) {
        f80_grid_mouse(&tracker.table_grid,native_touch.current_x,native_touch.current_y,0);
        f80_grid_hold(&tracker.table_grid);
        native_touch.selecting=1;
    }
    else {
        begin_wave_gesture(native_touch.current_x);
        hold_wave_marker(native_touch.current_x);
        native_touch.selecting=1;
    }
    native_touch.last_x=native_touch.current_x;
    native_touch.last_y=native_touch.current_y;
}
static void native_move(int x,int y) {
    native_touch.current_x=x;
    native_touch.current_y=y;
    if(x!=native_touch.x || y!=native_touch.y)
        native_touch.moved=1;
    if(!native_touch.moved && !native_touch.selecting)return;
    if(native_touch.kind==NATIVE_TOUCH_GRID) {
        if(native_touch.selecting)handle_mouse(x,y,2);
        else if(native_touch.moved) {
            if(y!=native_touch.last_y)touch_scroll_rows(native_touch.last_y-y);
            if(x!=native_touch.last_x)touch_scroll_fields(native_touch.last_x-x);
        }
    } else if(native_touch.kind==NATIVE_TOUCH_TABLE) {
        if(native_touch.selecting)f80_grid_drag(&tracker.table_grid,x,y);
        else if(native_touch.moved)
            f80_grid_scroll(&tracker.table_grid,
                            native_touch.last_x-x,native_touch.last_y-y);
    } else if(native_touch.kind==NATIVE_TOUCH_WRAP) {
        if(native_touch.moved && y!=native_touch.last_y)
            f80_wrap_scroll(&tracker.table_actions,native_touch.last_y-y,
                            tracker.table_buttons,5);
    } else if(x!=native_touch.last_x) {
        if(native_touch.selecting)move_wave_marker_touch(x);
        else if(native_touch.moved)pan_wave_touch(x);
    }
    native_touch.last_x=x;
    native_touch.last_y=y;
}
static void native_mouse_event(void) {
    MEVENT event;
    if(getmouse(&event)!=OK)return;
    int x=event.x,y=event.y;
    if(event.bstate&BUTTON4_PRESSED) { handle_mouse(x,y,-1);return; }
#ifdef BUTTON5_PRESSED
    if(event.bstate&BUTTON5_PRESSED) { handle_mouse(x,y,1);return; }
#endif
#ifdef REPORT_MOUSE_POSITION
    if(event.bstate&REPORT_MOUSE_POSITION) {
        if(native_touch.down)native_move(x,y);
#ifndef BUTTON5_PRESSED
        /* Older ncurses reports wheel-down as position motion. */
        else if(!native_other_button_down)handle_mouse(x,y,1);
#endif
        return;
    }
#endif
    if(event.bstate&BUTTON1_PRESSED) {
        if(native_touch.down) { native_move(x,y);return; }
        F80Rect r=tracker.phrase_canvas->rect;
        if(!tracker.app.modal && x>=r.x+5 && x<r.x+r.w-1 &&
           y>=r.y+3 && y<r.y+r.h) {
            native_touch.kind=NATIVE_TOUCH_GRID;
        } else if(tracker.table_dialog && tracker.app.modal==tracker.table_dialog &&
                  f80_contains(tracker.table_actions.rect,x,y)) {
            native_touch.kind=NATIVE_TOUCH_WRAP;
        } else if(tracker.table_dialog && tracker.table_canvas &&
                  tracker.app.modal==tracker.table_dialog &&
                  f80_contains(tracker.table_canvas->rect,x,y) &&
                  x<tracker.table_canvas->rect.x+tracker.table_canvas->rect.w-
                    tracker.table_grid.show_v &&
                  y>tracker.table_canvas->rect.y &&
                  y<tracker.table_canvas->rect.y+tracker.table_canvas->rect.h-
                    tracker.table_grid.show_h) {
            native_touch.kind=NATIVE_TOUCH_TABLE;
        } else if(wave_touch_area(x,y)) {
            native_touch.kind=NATIVE_TOUCH_WAVE;
            begin_wave_gesture(x);
        } else native_touch.kind=0;
        if(native_touch.kind) {
            native_touch.down=1;
            native_touch.selecting=native_touch.moved=0;
            native_touch.x=native_touch.current_x=native_touch.last_x=x;
            native_touch.y=native_touch.current_y=native_touch.last_y=y;
            native_touch.started_at=SDL_GetTicks();
        } else {
            native_other_button_down=1;
            handle_mouse(x,y,0);
        }
        return;
    }
    if(event.bstate&BUTTON1_RELEASED) {
        native_other_button_down=0;
        if(native_touch.down) {
            if(x!=native_touch.x || y!=native_touch.y)native_touch.moved=1;
            if(native_touch.selecting || native_touch.moved)native_move(x,y);
            else handle_mouse(native_touch.x,native_touch.y,0);
            native_touch.down=native_touch.kind=0;
        }
        return;
    }
    if(event.bstate&BUTTON1_CLICKED) {
        native_other_button_down=0;
        if(native_touch.down) {
            if(x!=native_touch.x || y!=native_touch.y)native_touch.moved=1;
            if(!native_touch.moved && !native_touch.selecting)
                handle_mouse(native_touch.x,native_touch.y,0);
            else native_move(x,y);
            native_touch.down=native_touch.kind=0;
        } else handle_mouse(x,y,0);
    }
}
#endif
static int header_button_width(void) {
    F80Widget *buttons[]={tracker.play_button,tracker.stop_button,
                        tracker.follow_button,tracker.mode_button};
    int width=3;
    for(int i=0;i<4;++i) {
        int w,h;f80_fit_size(buttons[i],&w,&h);
        width+=w;
    }
    return width;
}
static int header_is_wide(int cols) {
    int title_w,title_h,bpm_w,bpm_h;
    f80_fit_size(tracker.header_title,&title_w,&title_h);
    f80_fit_size(tracker.bpm_label,&bpm_w,&bpm_h);
    return cols>=title_w+bpm_w+header_button_width()+20;
}
static void layout_header(int cols,int wide) {
    F80Rect r=tracker.header->rect;
    int title_w,title_h,bpm_w,bpm_h;
    f80_fit_size(tracker.header_title,&title_w,&title_h);
    f80_fit_size(tracker.bpm_label,&bpm_w,&bpm_h);
    int button_w=header_button_width();
    int button_x=wide?r.x+cols-button_w-1:r.x+1;
    int info_end=wide?button_x-2:r.x+cols;
    int progress_x=r.x+title_w+2;
    int bpm_x=info_end-bpm_w;
    int progress_w=bpm_x-progress_x-2;
    if(progress_w<0)progress_w=0;
    f80_layout(tracker.header_title,f80_rect(r.x,r.y,title_w,1));
    f80_layout(tracker.progress,f80_rect(progress_x,r.y,progress_w,1));
    f80_layout(tracker.bpm_label,f80_rect(bpm_x,r.y,bpm_w,1));
    F80Widget *buttons[]={tracker.play_button,tracker.stop_button,
                        tracker.follow_button,tracker.mode_button};
    int x=button_x;
    for(int i=0;i<4;++i) {
        int w,h;f80_fit_size(buttons[i],&w,&h);
        f80_layout(buttons[i],f80_rect(x,r.y+(wide?0:2),w,2));
        x+=w+1;
    }
}
static int frame(void *user) {
    (void)user;
    if(!tracker.app.running) {
        while(tracker.app.modal)f80_hide_modal(&tracker.app);
        sample_audio_close();sample_free(&tracker.wave);
        for(int index=0;index<tracker.sample_count;++index)
            sample_free(&tracker.samples[index].wave);
        f80_free(tracker.root);tracker.root=NULL;
        return 0;
    }
#ifndef __EMSCRIPTEN__
    native_poll_hold();
#endif
    advance_playback();
    advance_audition();
    update_labels();
    const F80StatusItem *items=current_status();
    tracker.status->status_items=items;
    tracker.status->status_count=10;
    int rows,cols;getmaxyx(stdscr,rows,cols);
    int editing=0;
#ifdef __EMSCRIPTEN__
    editing=sample_web_compact_editing();
#endif
    if(editing && !tracker.compact_editing)tracker.reveal_cursor=1;
    tracker.compact_editing=editing;
    int wide_header=header_is_wide(cols);
    tracker.header->height=f80_fixed(wide_header?2:4);
    tracker.wave_row->height=f80_fixed(editing?3:rows>=32?14:rows>=23?10:6);
    f80_layout(tracker.root,f80_rect(0,0,cols,rows));
    layout_header(cols,wide_header);
    if(tracker.app.modal) {
#ifndef __EMSCRIPTEN__
        if(f80_file_dialog_active(&file_dialog))
            f80_file_dialog_layout(&file_dialog,cols,rows);
        else {
#endif
        int w,h;f80_fit_size(tracker.app.modal,&w,&h);
        if(tracker.app.modal==tracker.manage_dialog || tracker.app.modal==tracker.slices_dialog ||
           tracker.app.modal==tracker.order_dialog || tracker.app.modal==tracker.table_dialog ||
           tracker.app.modal==tracker.settings_dialog) {
            w=tracker.app.modal==tracker.manage_dialog?66:72;
            h=tracker.app.modal==tracker.manage_dialog?19:24;
            if(tracker.app.modal==tracker.order_dialog) { w=90;h=28; }
            if(tracker.app.modal==tracker.table_dialog) { w=46;h=25; }
            if(tracker.app.modal==tracker.settings_dialog) { w=48;h=14; }
        }
        if(w>cols-2)w=cols-2;
        if(h>rows-2)h=rows-2;
        f80_layout(tracker.app.modal,f80_rect((cols-w)/2,(rows-h)/2,w,h));
        if(tracker.app.modal==tracker.settings_dialog) {
            F80Rect r=tracker.app.modal->rect;
            int left=r.x+2,label_w=r.w>=30?21:r.w/2;
            int input_w=r.w-label_w-5;
            if(input_w<1)input_w=1;
            for(int i=0;i<4;++i) {
                int row=r.y+2+i*2;
                f80_layout(tracker.settings_label[i],f80_rect(left,row,label_w,1));
                f80_layout(tracker.settings_input[i],
                           f80_rect(left+label_w+1,row,input_w,1));
            }
            int button_y=r.y+r.h-3;
            f80_layout(tracker.settings_apply,f80_rect(left,button_y,10,2));
            f80_layout(tracker.settings_close,f80_rect(left+11,button_y,10,2));
        } else if(tracker.app.modal==tracker.table_dialog) {
            F80Rect r=tracker.app.modal->rect;
            f80_layout(tracker.table_name_label,f80_rect(r.x+2,r.y+2,4,1));
            f80_layout(tracker.table_name,f80_rect(r.x+7,r.y+2,r.w-9,1));
            f80_layout(tracker.table_panel,
                       f80_rect(r.x+2,r.y+4,r.w-4,r.h-10));
            F80Rect panel=tracker.table_panel->rect;
            f80_layout(tracker.table_canvas,panel);
            f80_grid_layout(&tracker.table_grid,tracker.table_canvas->rect);
            f80_wrap_layout(&tracker.table_actions,
                            f80_rect(r.x+2,r.y+r.h-5,r.w-4,4),
                            tracker.table_buttons,5);
            F80Rect actions=tracker.table_actions.rect;
            f80_layout(tracker.table_scrollbar,
                       f80_rect(actions.x+actions.w-1,actions.y,1,actions.h));
        } else if(tracker.app.modal==tracker.order_dialog) {
            F80Rect r=tracker.app.modal->rect;
            int wide=r.w>=70;
            int left=r.x+2,inner=r.w-4;
            int bank_y=r.y+2,bank_list_y=r.y+3,bank_h;
            int seq_y,seq_list_y,seq_h,seq_x=left,bank_w=inner,seq_w=inner;
            if(wide) {
                bank_w=(inner-2)/2;seq_x=left+bank_w+2;seq_w=inner-bank_w-2;
                seq_y=bank_y;seq_list_y=bank_list_y;bank_h=seq_h=r.h-12;
            } else {
                bank_h=(r.h-14)/2;if(bank_h<2)bank_h=2;
                seq_y=bank_list_y+bank_h;seq_list_y=seq_y+1;seq_h=bank_h;
            }
            f80_layout(tracker.order_bank_label,f80_rect(left,bank_y,bank_w,1));
            f80_layout(tracker.order_bank_bg,f80_rect(left,bank_list_y,bank_w,bank_h));
            f80_layout(tracker.order_bank,f80_rect(left,bank_list_y,bank_w,bank_h));
            f80_layout(tracker.order_seq_label,f80_rect(seq_x,seq_y,seq_w,1));
            f80_layout(tracker.order_seq_bg,f80_rect(seq_x,seq_list_y,seq_w,seq_h));
            f80_layout(tracker.order_seq,f80_rect(seq_x,seq_list_y,seq_w,seq_h));
            int input_y=(wide?bank_list_y+bank_h:seq_list_y+seq_h)+1;
            f80_layout(tracker.order_name,f80_rect(left,input_y,inner,1));
            int at=left,row=input_y+2;
            for(size_t i=7;i<tracker.app.modal->child_count;++i) {
                F80Widget *child=tracker.app.modal->children[i];
                int cw,ch;f80_fit_size(child,&cw,&ch);
                if(at+cw>r.x+r.w-2) { at=left;row+=2; }
                f80_layout(child,f80_rect(at,row,cw,2));at+=cw+1;
            }
        } else if(tracker.app.modal==tracker.manage_dialog ||
                  tracker.app.modal==tracker.slices_dialog) {
            F80Rect r=tracker.app.modal->rect;
            int manage=tracker.app.modal==tracker.manage_dialog;
            F80Widget *list=manage?tracker.manage_list:tracker.slices_list;
            F80Widget *background=manage?tracker.manage_list_bg:tracker.slices_list_bg;
            F80Widget *input=manage?tracker.manage_name:tracker.slices_note;
            F80Widget *info=manage?tracker.manage_info:tracker.slices_info;
            int list_h=r.h-11;if(list_h<2)list_h=2;
            f80_layout(background,f80_rect(r.x+2,r.y+2,r.w-4,list_h));
            f80_layout(list,f80_rect(r.x+2,r.y+2,r.w-4,list_h));
            f80_layout(info,f80_rect(r.x+2,r.y+2+list_h,r.w-4,1));
            f80_layout(input,f80_rect(r.x+2,r.y+4+list_h,r.w-4,1));
            int at=r.x+2,row=r.y+6+list_h;
            for(size_t i=4;i<tracker.app.modal->child_count;++i) {
                F80Widget *child=tracker.app.modal->children[i];
                int cw,ch;f80_fit_size(child,&cw,&ch);
                if(at+cw>r.x+r.w-2) { at=r.x+2;row+=2; }
                f80_layout(child,f80_rect(at,row,cw,2));at+=cw+1;
            }
        }
#ifndef __EMSCRIPTEN__
        }
#endif
    } else tracker.app.focus=tracker.focus==FOCUS_PHRASE?
               tracker.phrase_canvas:tracker.wave_canvas;
    f80_draw_screen(&tracker.app);
#ifdef __EMSCRIPTEN__
    if(tracker.app.modal==tracker.table_dialog && tracker.table_canvas) {
        F80Rect table=tracker.table_canvas->rect;
        sample_web_table_rect(table.x,table.y,table.w,table.h,
                              tracker.table_grid.show_h,tracker.table_grid.show_v);
        F80Rect actions=tracker.table_actions.rect;
        sample_web_wrap_rect(actions.x,actions.y,actions.w,actions.h);
    } else {
        sample_web_table_rect(0,0,0,0,0,0);
        sample_web_wrap_rect(0,0,0,0);
    }
    if(!tracker.app.modal) {
        F80Rect grid=tracker.phrase_canvas->rect;
        if(grid.w>6 && grid.h>3) {
            sample_web_grid_rect(grid.x+5,grid.y+3,grid.w-6,grid.h-3);
            int body=grid.h-3,first=tracker.edit_order>=0?
                tracker.edit_order*tracker.song.pattern_rows-tracker.phrase_scroll:0;
            int top=first>0?first:0;
            int bottom=first+tracker.song.pattern_rows<body?first+tracker.song.pattern_rows:body;
            if(bottom>top)
                sample_web_editable_grid_rect(grid.x+5,grid.y+3+top,
                                              grid.w-6,bottom-top);
        }
        F80Rect wave=tracker.wave_canvas->rect;
        if(tracker.wave.pcm && wave.w>0 && wave.h>1)
            sample_web_wave_rect(wave.x,wave.y,wave.w,wave.h-1);
        F80Rect order=tracker.order_list->rect;
        if(order.w>0 && order.h>0)
            sample_web_order_rect(order.x,order.y,order.w,order.h);
    } else {
        sample_web_grid_rect(0,0,0,0);
        sample_web_editable_grid_rect(0,0,0,0);
        sample_web_wave_rect(0,0,0,0);
        sample_web_order_rect(0,0,0,0);
    }
#endif
    int key=getch();
    if(key==KEY_MOUSE) {
#ifndef __EMSCRIPTEN__
        native_mouse_event();
#else
        int x,y,wheel;
        if(f80_curses_mouse(&x,&y,&wheel)==OK)handle_mouse(x,y,wheel);
#endif
    } else if(key!=ERR)handle_key(f80_curses_key(key));
    return 1;
}
static int load_default_song(const char *executable) {
#ifdef __EMSCRIPTEN__
    (void)executable;
    if(!load_song_at("/default.song"))return 0;
#else
    char path[PATH_MAX],resolved[PATH_MAX],directory[PATH_MAX];
    if(executable && realpath(executable,directory)) {
        char *slash=strrchr(directory,'/');
        if(slash) {
            *slash=0;
            if(snprintf(path,sizeof(path),"%s/../../../default.song",directory)<
               (int)sizeof(path) && realpath(path,resolved)) {
                if(!load_song_at(resolved))return 0;
                goto loaded;
            }
            if(snprintf(path,sizeof(path),"%s/default.song",directory)<
               (int)sizeof(path) && realpath(path,resolved)) {
                if(!load_song_at(resolved))return 0;
                goto loaded;
            }
        }
    }
    if(!realpath("default.song",resolved)) {
        message("default.song is missing");return 0;
    }
    if(!load_song_at(resolved))return 0;
loaded:
#endif
    for(int i=0;i<tracker.sample_count;++i) {
        SampleWave *wave=i==tracker.sample_index?&tracker.wave:&tracker.samples[i].wave;
        if(!wave->pcm) { message("Default song has a missing WAV");return 0; }
    }
    return 1;
}
int main(int argc,char **argv) {
    setlocale(LC_ALL,"");
    memset(&tracker,0,sizeof(tracker));
    tracker.entry_octave=4;tracker.loop=tracker.follow=1;tracker.active_step=-1;
    tracker.edit_order=0;tracker.reveal_cursor=1;
    tracker.active_pattern_index=tracker.active_order_index=-1;
    if(!load_default_song(argv[0])) {
        fprintf(stderr,"Could not load default.song: %s\n",tracker.message);
        sample_free(&tracker.wave);
        for(int i=0;i<tracker.sample_count;++i)sample_free(&tracker.samples[i].wave);
        return 1;
    }
    message("Default song ready");
    if(argc>1)load_path(argv[1],NULL);
    initscr();cbreak();noecho();keypad(stdscr,TRUE);curs_set(0);f80_colors();
#ifndef __EMSCRIPTEN__
    define_key("\033[1;5D",KEY_F(60));
    define_key("\033[1;5C",KEY_F(61));
    define_key("\033[5D",KEY_F(60));
    define_key("\033[5C",KEY_F(61));
    define_key("\033[1;2D",KEY_SLEFT);
    define_key("\033[1;2C",KEY_SRIGHT);
    define_key("\033[1;2A",KEY_F(62));
    define_key("\033[1;2B",KEY_F(63));
#endif
#ifdef REPORT_MOUSE_POSITION
    mousemask(ALL_MOUSE_EVENTS|REPORT_MOUSE_POSITION,NULL);
#else
    mousemask(ALL_MOUSE_EVENTS,NULL);
#endif
#ifndef __EMSCRIPTEN__
    mouseinterval(0);
    /* ncurses enables clicks, but some terminals need drag reporting enabled explicitly. */
    fputs("\033[?1002h",stdout);fflush(stdout);
#endif
    tracker.root=f80_window(F80_WINDOW_BLUE,F80_COL);
    tracker.app.menu=f80_add(tracker.root,f80_menubar(menus,sizeof(menus)/sizeof(*menus)));
    F80Widget *header_gap=f80_add(tracker.root,f80_box(F80_ROW));
    header_gap->fill=1;
    f80_sized(header_gap,f80_grow(),f80_fixed(1));
    tracker.header=f80_add(tracker.root,f80_box(F80_ROW));
    f80_sized(tracker.header,f80_grow(),f80_fixed(2));
    tracker.header_title=f80_add(tracker.header,f80_label(tracker.header_text));
    tracker.progress=f80_add(tracker.header,f80_progress(0,0));
    tracker.bpm_label=f80_add(tracker.header,f80_label(tracker.bpm_text));
    tracker.play_button=f80_add(tracker.header,f80_button("~P~lay",toggle_play,NULL));
    tracker.stop_button=f80_add(tracker.header,f80_button("~S~top",stop_playback,NULL));
    tracker.follow_button=f80_add(tracker.header,f80_button("~F~ollow",toggle_follow,NULL));
    tracker.mode_button=f80_add(tracker.header,f80_button("~S~ong",toggle_mode,NULL));
    tracker.wave_row=f80_add(tracker.root,f80_box(F80_ROW));
    f80_sized(tracker.wave_row,f80_grow(),f80_fixed(14));
    tracker.order_frame=f80_add(tracker.wave_row,f80_box(F80_COL));
    tracker.order_frame->frame=1;
    tracker.order_frame->title="ORD";
    f80_sized(tracker.order_frame,f80_fixed(10),f80_grow());
    tracker.order_list=f80_add(tracker.order_frame,
        f80_listbox(tracker.order_panel_items,tracker.song.order_count,
                    order_panel_selected,NULL));
    f80_sized(tracker.order_list,f80_grow(),f80_grow());
    tracker.wave_frame=f80_add(tracker.wave_row,f80_box(F80_COL));
    tracker.wave_frame->frame=1;
    f80_sized(tracker.wave_frame,f80_grow(),f80_grow());
    tracker.wave_canvas=f80_add(tracker.wave_frame,f80_canvas(paint_wave,NULL));
    tracker.phrase_frame=f80_add(tracker.root,f80_box(F80_COL));
    tracker.phrase_frame->frame=1;
    tracker.phrase_canvas=f80_add(tracker.phrase_frame,f80_canvas(paint_phrase,NULL));
    tracker.details=f80_add(tracker.root,f80_label(tracker.details_text));
    f80_sized(tracker.details,f80_grow(),f80_fixed(1));
    tracker.status=f80_add(tracker.root,f80_statusbar(wave_status,10));
    tracker.app.running=1;f80_set_root(&tracker.app,tracker.root);
    f80_curses_run(frame,NULL,30);
#ifndef __EMSCRIPTEN__
    fputs("\033[?1002l",stdout);fflush(stdout);
#endif
    return 0;
}
