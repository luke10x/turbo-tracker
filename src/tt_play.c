#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "tt_audio.h"
#include "tt_sample.h"
#include "tt_tracker_internal.h"
#include "tt_play.h"

static void play_start_now(F80Widget *button,void *user);

static const TtPattern *playing_pattern(void) {
    if(tracker.play_mode==PLAY_PATTERN && !tracker.order_range_active)
        return tracker.loop_pattern_index==tracker.pattern_index?
               &tracker.pattern:&tracker.song.pattern[tracker.loop_pattern_index];
    int index=tracker.song.order[tracker.play_order];
    return index==tracker.pattern_index?&tracker.pattern:&tracker.song.pattern[index];
}

static int pattern_is_empty(const TtPattern *pattern) {
    for(int row=0;row<tracker.song.pattern_rows;++row)
        for(int channel=0;channel<TT_CHANNELS;++channel) {
            const TtCell *cell=&pattern->cell[row][channel];
            if(cell->note!=-1 || cell->effect!=TT_EFFECT_NONE ||
               cell->effect2!=TT_EFFECT_NONE || cell->effect3!=TT_EFFECT_NONE)return 0;
        }
    return 1;
}
static int next_active_order(int at,int direction) {
    for(int count=0;count<tracker.song.order_count;++count) {
        at+=direction;
        if(at<0 || at>=tracker.song.order_count)return -1;
        if(tracker.song.order_active[at])return at;
    }
    return -1;
}
static int playback_bpm(void) {
    return tracker.playback_bpm>0?tracker.playback_bpm:tracker.song.bpm;
}
static int row_duration_frames(void) {
    int divisor=playback_bpm()*tracker.song.rows_per_beat;
    return divisor>0?TT_AUDIO_OUTPUT_RATE*60/divisor:0;
}
static int udl_delay_frames(int value,int table_tick) {
    if(value<=0)return 0;
    int duration=row_duration_frames();
    if(table_tick) {
        int ticks=tracker.playback_ticks_per_row>0?
            tracker.playback_ticks_per_row:tracker.song.ticks_per_row;
        if(ticks>0)duration/=ticks;
    }
    if(duration<1)return 0;
    int delay=(int)((long long)duration*value/255);
    if(delay>=duration)delay=duration-1;
    return delay;
}

static void play_stop_now(F80Widget *button,void *user) {
    (void)button;(void)user;
    tracker.playing=tracker.started=0;tracker.play_step=tracker.play_order=0;
    tracker.active_step=-1;
    tracker.active_pattern_index=tracker.active_order_index=-1;
    tracker.song_finished=tracker.frames_to_tick=0;
    tracker.playback_bpm=tracker.song.bpm;
    tracker.playback_ticks_per_row=tracker.song.ticks_per_row;
    tracker.pending_flow=0;
    tracker.display_head=tracker.display_tail=0;
    tt_audio_stop();
    memset(tracker.table_voice,0,sizeof(tracker.table_voice));
    tt_tracker_message("Stopped");
}

static const TtWave *instrument_wave(const TtCell *cell, const TtPhrase **phrase) {
    int index=cell->instrument==0?tracker.sample_index:cell->instrument-1;
    if(index<0 || index>=tracker.sample_count)return NULL;
    if(index==tracker.sample_index) { *phrase=&tracker.phrase;return tracker.wave.pcm?&tracker.wave:NULL; }
    *phrase=&tracker.samples[index].phrase;
    return tracker.samples[index].wave.pcm?&tracker.samples[index].wave:NULL;
}

static TtCell cell_sample_selection(const TtCell *cell) {
    TtCell selected=*cell;
    for(int slot=0;slot<3;++slot) {
        const TtPatternEffect *fx=tt_cell_effect_const(cell,slot);
        if(fx->command==TT_EFFECT_S && fx->value>=0) {
            selected.effect=TT_EFFECT_S;selected.value=fx->value;
        }
    }
    return selected;
}

static int cell_reference_note(const TtPhrase *phrase,const TtCell *cell) {
    if(cell->effect!=TT_EFFECT_S || cell->value<0)return phrase->reference_note;
    int slice=cell->value>>4;
    return slice<15?tt_slice_base_note(phrase,slice):phrase->reference_note;
}

static void play_audition_cell_now(int step,int channel) {
    int first,last;
    TtCell *cell=&tracker.pattern.cell[step][channel];
    TtCell sample=cell_sample_selection(cell);
    const TtPhrase *phrase=NULL;
    const TtWave *wave=instrument_wave(cell,&phrase);
    if(!wave || !tt_cell_range(phrase,&sample,wave->frames,&first,&last)) {
        tt_audio_stop();
        tt_tracker_message("Set a loaded instrument, note, and playable effect value first");
        return;
    }
    int base=cell_reference_note(phrase,&sample),glide=0;
    for(int slot=0;slot<3;++slot) {
        const TtPatternEffect *fx=tt_cell_effect_const(cell,slot);
        if(fx->command==TT_EFFECT_G && fx->value>0)glide=fx->value;
    }
    int glided=glide>0 && tt_audio_glide_to(0,cell->note,base,glide);
    if(!glided && !tt_audio_play(wave,first,last,cell->note,base)) {
        tt_tracker_message("Audio unavailable: check the output device or browser permission");
        return;
    }
    tracker.audition_effect_active=0;
    for(int slot=0;slot<3;++slot) {
        const TtPatternEffect *fx=tt_cell_effect_const(cell,slot);
        if(fx->command==TT_EFFECT_T && fx->value>=0 && fx->value<TT_SONG_TABLES) {
            TableVoice *voice=&tracker.table_voice[channel];
            voice->active=1;voice->table=fx->value;voice->tick=0;
            voice->next_table=-1;voice->pending=0;
        } else if(fx->command==TT_EFFECT_M && fx->value>=0)
            tt_audio_set_volume(0,fx->value);
        else if(fx->command==TT_EFFECT_STRETCH && fx->value>=0)
            tt_audio_set_stretch(0,fx->value);
        else if(fx->command && fx->value>=0 && strchr("GIJKNOPRVWr",fx->command)) {
            tt_audio_command(0,(char)fx->command,fx->value);
            tracker.audition_effect_active=1;
        }
    }
    if(tracker.audition_effect_active)tracker.next_audition_tick_at=SDL_GetTicks();
    tracker.active_step=step;
    tracker.active_pattern_index=tracker.pattern_index;
}

void tt_play_play_row(int row) {
    if(!tracker.rendering_song && tracker.play_mode==PLAY_SONG &&
       tracker.follow && tracker.order_list)
        tracker.order_list->cursor=tracker.play_order;
    const TtPattern *pattern=playing_pattern();
    int pattern_index=tracker.play_mode==PLAY_PATTERN && !tracker.order_range_active?
                      tracker.loop_pattern_index:
                      tracker.song.order[tracker.play_order];
    if(row==0) {
        char info[192];
        if(tracker.play_mode==PLAY_PATTERN && !tracker.order_range_active)
            snprintf(info,sizeof(info),"Looping pattern %02X: %.90s%s",
                     pattern_index,tracker.song.name[pattern_index],
                     pattern_is_empty(pattern)?" [empty]":"");
        else
            snprintf(info,sizeof(info),"Order %02X/%02X: %.90s%s",
                     tracker.play_order,tracker.song.order_count-1,
                     tracker.song.name[pattern_index],
                     pattern_is_empty(pattern)?" [empty]":"");
        tt_tracker_message(info);
    }
    for(int channel=0;channel<TT_CHANNELS;++channel) {
        const TtCell *cell=&pattern->cell[row][channel];
        int glide=0,udl=-1,table_started=0;
        TtCell sample=cell_sample_selection(cell);
        for(int slot=0;slot<3;++slot) {
            const TtPatternEffect *fx=tt_cell_effect_const(cell,slot);
            if(fx->command==TT_EFFECT_T && fx->value>=0 && fx->value<TT_SONG_TABLES) {
                TableVoice *voice=&tracker.table_voice[channel];
                voice->active=1;voice->table=fx->value;voice->tick=0;
                voice->next_table=-1;voice->pending=0;
                voice->table_loop_active=voice->table_loop_broken=0;
                tt_audio_cancel_delayed(channel);
                table_started=1;
            } else if(fx->command==TT_EFFECT_M && fx->value>=0)
                tt_audio_set_volume(channel,fx->value);
            else if(fx->command==TT_EFFECT_G && fx->value>0)glide=fx->value;
            else if(fx->command==TT_EFFECT_U && fx->value>=0)udl=fx->value;
            else if(fx->command==TT_EFFECT_STRETCH && fx->value>=0)
                tt_audio_set_stretch(channel,fx->value);
            else if(fx->command=='X' && fx->value>=0 &&
                    fx->value<tracker.song.pattern_count)
                tt_play_emit((TtPlayEvent){TT_PLAY_PATTERN_JUMP,fx->value,0,0});
            else if(fx->command=='Y' && fx->value>=0 &&
                    fx->value<tracker.song.pattern_rows)
                tt_play_emit((TtPlayEvent){TT_PLAY_ROW_JUMP,fx->value,0,0});
            else if(fx->command=='Z' && fx->value>=0 && fx->value<=255) {
                tt_play_emit((TtPlayEvent){TT_PLAY_SET_BPM,fx->value,0,0});
                tt_play_flush();
            }
            else if(fx->command && fx->value>=0 && strchr("IJK NOPRVWr",fx->command) && fx->command!=' ')
                tt_audio_command(channel,(char)fx->command,fx->value);
        }
        TableVoice *table_voice=&tracker.table_voice[channel];
        if(!table_started && ((cell->note>=0 && cell->instrument>=0) ||
                              cell->note==TT_NOTE_OFF) &&
           table_voice->active) {
            table_voice->active=0;table_voice->pending=0;
            table_voice->next_table=-1;
            table_voice->table_loop_active=table_voice->table_loop_broken=0;
            tt_audio_cancel_delayed(channel);
        }
        if(cell->note==TT_NOTE_OFF) {
            table_voice->pending=0;
            tt_audio_note_off(channel);
            continue;
        }
        if(cell->note<0)continue;
        int first,last;
        const TtPhrase *phrase=NULL;
        const TtWave *wave=instrument_wave(cell,&phrase);
        if(!wave || !tt_cell_range(phrase,&sample,wave->frames,&first,&last))continue;
        int base=cell_reference_note(phrase,&sample);
        int delay=udl>=0?udl_delay_frames(udl,0):0;
        if(glide>0 && (delay?
           tt_audio_glide_to_delayed(channel,cell->note,base,glide,delay):
           tt_audio_glide_to(channel,cell->note,base,glide)))continue;
        if(!tt_audio_play_channel_delayed(channel,wave,first,last,cell->note,base,delay))
            tt_tracker_message("Audio unavailable: check the output device or browser permission");
    }
    if(tracker.rendering_song) {
        int next=(tracker.display_tail+1)%SONG_DISPLAY_ROWS;
        if(next==tracker.display_head)
            tracker.display_head=(tracker.display_head+1)%SONG_DISPLAY_ROWS;
        tracker.display_rows[tracker.display_tail]=(SongDisplayRow){
            tracker.song_written_frames,row,pattern_index,
            tracker.play_mode==PLAY_SONG || tracker.order_range_active?
                tracker.play_order:-1};
        tracker.display_tail=next;
    } else {
        tt_play_emit((TtPlayEvent){TT_PLAY_ROW_PLAYED,row,pattern_index,
                                  tracker.play_mode==PLAY_SONG || tracker.order_range_active?
                                      tracker.play_order:-1});
        tt_play_flush();
    }
}

static void table_play_note(int channel,const TtCell *cell,int glide_rate,
                            int delay_frames) {
    const TtPhrase *phrase=NULL;
    const TtWave *wave=instrument_wave(cell,&phrase);
    int first,last;
    if(!wave || !tt_cell_range(phrase,cell,wave->frames,&first,&last))return;
    int base=cell_reference_note(phrase,cell);
    if(glide_rate>0 && (delay_frames?
       tt_audio_glide_to_delayed(channel,cell->note,base,glide_rate,delay_frames):
       tt_audio_glide_to(channel,cell->note,base,glide_rate)))return;
    tt_audio_play_channel_delayed(channel,wave,first,last,cell->note,base,delay_frames);
}

void tt_play_table_ticks(void) {
    for(int channel=0;channel<TT_CHANNELS;++channel) {
        TableVoice *voice=&tracker.table_voice[channel];
        voice->pending=0;
        if(!voice->active)continue;
        const TtTable *table=&tracker.song.table[voice->table];
        if(voice->tick>=table->length) {
            if(voice->next_table>=0) {
                voice->table=voice->next_table;voice->next_table=-1;
                voice->tick=0;table=&tracker.song.table[voice->table];
                voice->table_loop_active=voice->table_loop_broken=0;
            } else { voice->active=0;continue; }
        }
        const TtTableTick *entry=&table->tick[voice->tick++];
        TtCell note={entry->note,entry->instrument,TT_EFFECT_NONE,-1,
                     TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
        if(entry->note==TT_NOTE_OFF && voice->table_loop_active)
            voice->table_loop_broken=1;
        int delay=-1,glide_rate=0,jump_table=-1,jump_tick=-1;
        for(int fx=0;fx<TT_TABLE_EFFECTS;++fx) {
            const TtTableEffect *effect=&entry->effect[fx];
            if(!effect->command || effect->value<0)continue;
            switch(effect->command) {
            case 'M':tt_audio_set_volume(channel,effect->value);break;
            case 'S':note.effect=TT_EFFECT_S;note.value=effect->value;break;
            case 'U':delay=effect->value;break;
            case 'G':glide_rate=effect->value;
                tt_audio_command(channel,'G',effect->value);break;
            case 'I':case 'J':case 'K':case 'N':case 'O':
            case 'P':case 'R':case 'V':case 'W':case 'r':
                tt_audio_command(channel,effect->command,effect->value);break;
            case 'T':
                if(effect->value<TT_SONG_TABLES)
                    voice->next_table=effect->value;
                break;
            case 'X':
                if(effect->value<TT_SONG_TABLES)jump_table=effect->value;
                break;
            case 'Y':jump_tick=effect->value;break;
            case 'Z':
                if(effect->value<=TT_MAX_TICKS_PER_ROW)
                    tt_play_emit((TtPlayEvent){TT_PLAY_SET_TICKS_PER_ROW,
                                               effect->value,0,0});
                tt_play_flush();
                break;
            default:break;
            }
        }
        if(note.note==TT_NOTE_OFF) {
            voice->pending=0;
            tt_audio_note_off(channel);
        } else if(note.note>=0 && note.instrument>=0) {
            voice->pending=0;
            int delay_frames=delay>=0?udl_delay_frames(delay,1):0;
            table_play_note(channel,&note,glide_rate,delay_frames);
            voice->pending=delay_frames>0;
        }
        if(jump_table>=0) {
            voice->table=jump_table;voice->tick=0;voice->next_table=-1;
            voice->table_loop_active=voice->table_loop_broken=0;
        } else if(jump_tick>=0 && jump_tick<table->length) {
            if(voice->table_loop_broken) {
                voice->table_loop_active=voice->table_loop_broken=0;
            } else {
                if(jump_tick<voice->tick)voice->table_loop_active=1;
                voice->tick=jump_tick;
            }
        }
    }
    tt_play_flush();
}

static void play_audition_now(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.focus==FOCUS_WAVE) {
        int marker=tracker.marker;
        int first=tracker.phrase.marker[marker];
        int last=marker<15?tracker.phrase.marker[marker+1]:tracker.wave.frames;
        if(last<=first) { tt_tracker_message("Move the next marker to make a slice");return; }
        tt_audio_set_volume(0,255);
        int base=tt_slice_base_note(&tracker.phrase,marker<15?marker:14);
        if(!tt_audio_play(&tracker.wave,first,last,base,base))
            tt_tracker_message("Audio unavailable");
        else tt_tracker_message("Audition marker slice");
    } else play_audition_cell_now(tracker.step,tracker.channel);
}

static void play_start_now(F80Widget *button,void *user) {
    (void)button;(void)user;
    tt_audio_stop();
    tracker.playing=tracker.started=0;
    if(tracker.play_mode==PLAY_SONG && !tracker.order_range_active) {
        int at=tracker.order_list?tracker.order_list->cursor:0;
        if(at<0 || at>=tracker.song.order_count)at=0;
        if(!tracker.song.order_active[at]) {
            at=0;
            while(at<tracker.song.order_count && !tracker.song.order_active[at])++at;
        }
        if(at>=tracker.song.order_count) {
            tt_tracker_message("Activate an order before playing the song");
            return;
        }
        tracker.play_order=at;
    }
    if(!tt_audio_begin_song()) {
        tt_tracker_message("Audio unavailable: check the output device or browser permission");
        return;
    }
    for(int channel=0;channel<TT_CHANNELS;++channel)
        tt_audio_set_volume(channel,255);
    tracker.playing=tracker.started=1;
    tracker.playback_bpm=tracker.song.bpm;
    tracker.playback_ticks_per_row=tracker.song.ticks_per_row;
    tracker.pending_flow=0;
    tracker.audition_effect_active=0;
    memset(tracker.table_voice,0,sizeof(tracker.table_voice));
    tracker.tick_phase=tracker.tick_remainder=tracker.frames_to_tick=0;
    tracker.song_written_frames=0;
    tracker.display_head=tracker.display_tail=0;
    tracker.song_finished=0;
    tracker.play_order=tracker.order_range_active?
        (tracker.order_range_anchor<tracker.order_range_end?
         tracker.order_range_anchor:tracker.order_range_end):
        tracker.order_list?tracker.order_list->cursor:0;
    tracker.play_step=0;
    if(tracker.play_mode==PLAY_PATTERN && !tracker.order_range_active)
        tracker.loop_pattern_index=tracker.pattern_index;
    tracker.active_step=tracker.active_pattern_index=tracker.active_order_index=-1;
    tt_tracker_message(tracker.order_range_active?"Looping selected orders":
        tracker.play_mode==PLAY_PATTERN?"Looping current pattern":"Playing song order");
}

static void play_toggle_now(F80Widget *button,void *user) {
    if(!tracker.started) { play_start_now(button,user);return; }
    if(tracker.playing) {
        tracker.playing=0;tt_audio_stop();tt_tracker_message("Paused");
    } else {
        if(tracker.song_finished) { play_start_now(button,user);return; }
        if(!tt_audio_begin_song()) {
            tt_tracker_message("Audio unavailable: check the output device or browser permission");
            return;
        }
        tracker.playing=1;
        tracker.frames_to_tick=0;
        tracker.song_written_frames=0;
        tracker.display_head=tracker.display_tail=0;
        tt_tracker_message("Resumed");
    }
}

int tt_play_next_tick_frames(int divisor) {
    tracker.tick_remainder+=TT_AUDIO_OUTPUT_RATE*60;
    int frames=tracker.tick_remainder/divisor;
    tracker.tick_remainder%=divisor;
    return frames;
}

static void advance_playback_now(void) {
    if(!tracker.playing)return;
    int buffered=tt_audio_song_buffered_frames();
    int needed=SONG_LOOKAHEAD_FRAMES-buffered;
    if(needed<0)needed=0;
    tracker.rendering_song=1;
    while(needed>0 && !tracker.song_finished) {
        if(!tracker.frames_to_tick) {
            if(tracker.tick_phase==0) {
                tt_play_play_row(tracker.play_step);
                if(tracker.rendering_song)tt_play_flush();
            }
            tt_play_table_ticks();
            tt_audio_tick();
            int ticks_per_row=tracker.playback_ticks_per_row>0?
                tracker.playback_ticks_per_row:tracker.song.ticks_per_row;
            if(++tracker.tick_phase>=ticks_per_row) {
                tracker.tick_phase=0;
                if(tracker.pending_flow) {
                    if(tt_play_emit((TtPlayEvent){.type=TT_PLAY_APPLY_PENDING_FLOW}))
                        tt_play_flush();
                } else if(++tracker.play_step>=tracker.song.pattern_rows) {
                    tracker.play_step=0;
                    if(tracker.order_range_active) {
                        int first=tracker.order_range_anchor<tracker.order_range_end?
                            tracker.order_range_anchor:tracker.order_range_end;
                        int last=tracker.order_range_anchor>tracker.order_range_end?
                            tracker.order_range_anchor:tracker.order_range_end;
                        if(++tracker.play_order>last)tracker.play_order=first;
                    } else if(tracker.play_mode==PLAY_SONG) {
                        int next=next_active_order(tracker.play_order,1);
                        if(next<0 && tracker.loop) {
                            next=0;
                            while(next<tracker.song.order_count &&
                                  !tracker.song.order_active[next])++next;
                            if(next>=tracker.song.order_count)next=-1;
                        }
                        if(next<0)tracker.song_finished=1;
                        else tracker.play_order=next;
                    }
                }
            }
            int divisor=playback_bpm()*tracker.song.rows_per_beat*
                (tracker.playback_ticks_per_row>0?
                 tracker.playback_ticks_per_row:tracker.song.ticks_per_row);
            tracker.frames_to_tick=tt_play_next_tick_frames(divisor);
        }
        int frames=needed<tracker.frames_to_tick?needed:tracker.frames_to_tick;
        tt_audio_render_song(frames);
        tracker.song_written_frames+=(Uint64)frames;
        tracker.frames_to_tick-=frames;
        needed-=frames;
    }
    tracker.rendering_song=0;
    buffered=tt_audio_song_buffered_frames();
    Uint64 played=tracker.song_written_frames>
        (Uint64)buffered?tracker.song_written_frames-(Uint64)buffered:0;
    while(tracker.display_head!=tracker.display_tail &&
          tracker.display_rows[tracker.display_head].frame<=played) {
        SongDisplayRow row=tracker.display_rows[tracker.display_head];
        tt_play_emit((TtPlayEvent){TT_PLAY_ROW_PLAYED,row.row,row.pattern,row.order});
        tracker.display_head=(tracker.display_head+1)%SONG_DISPLAY_ROWS;
    }
    tt_play_flush();
    if(tracker.song_finished && buffered<512) {
        tt_play_emit((TtPlayEvent){.type=TT_PLAY_FINISHED});
        tt_play_flush();
    }
}

static void advance_audition_now(void) {
    if(tracker.playing || !tracker.audition_effect_active)return;
    if(!tt_audio_active()) { tracker.audition_effect_active=0;return; }
    Uint64 now=SDL_GetTicks();
    int interval=60000/(playback_bpm()*tracker.song.rows_per_beat*
                        tracker.song.ticks_per_row);
    if(interval<1)interval=1;
    for(int tick=0;tick<64 && now>=tracker.next_audition_tick_at;++tick) {
        tt_audio_tick();
        tracker.next_audition_tick_at+=interval;
    }
}

enum { TT_PLAY_QUEUE_CAPACITY=64 };
static TtPlayEvent play_queue[TT_PLAY_QUEUE_CAPACITY];
static int play_queue_count;

int tt_play_emit(TtPlayEvent event) {
    if(play_queue_count>=TT_PLAY_QUEUE_CAPACITY)return 0;
    play_queue[play_queue_count++]=event;
    const char *name="playback";
    switch(event.type) {
    case TT_PLAY_START:name="start";break;
    case TT_PLAY_STOP:name="stop";break;
    case TT_PLAY_TOGGLE:name="pause/resume";break;
    case TT_PLAY_SET_MODE:name="set mode";break;
    case TT_PLAY_TOGGLE_MODE:name="toggle mode";break;
    case TT_PLAY_TOGGLE_LOOP:name="toggle loop";break;
    case TT_PLAY_TOGGLE_FOLLOW:name="toggle follow";break;
    case TT_PLAY_SELECT_ORDER:name="seek order";break;
    case TT_PLAY_SELECT_PATTERN:name="select pattern loop";break;
    case TT_PLAY_SET_ORDER_RANGE:name="order range";break;
    case TT_PLAY_CLEAR_ORDER_RANGE:name="clear order range";break;
    case TT_PLAY_AUDITION:name="audition";break;
    case TT_PLAY_AUDITION_CELL:name="audition cell";break;
    case TT_PLAY_ROW_PLAYED:name="row played";break;
    case TT_PLAY_FINISHED:name="finished";break;
    case TT_PLAY_DOCUMENT_RESET:name="document reset";break;
    case TT_PLAY_AUDIO_RECOVER:name="recover audio device";break;
    case TT_PLAY_PATTERN_JUMP:name="pattern jump";break;
    case TT_PLAY_ROW_JUMP:name="row jump";break;
    case TT_PLAY_SET_TICKS_PER_ROW:name="set playback ticks per row";break;
    case TT_PLAY_SET_BPM:name="set playback BPM";break;
    case TT_PLAY_APPLY_PENDING_FLOW:name="apply flow jump";break;
    }
    char detail[96];
    if(event.type==TT_PLAY_ROW_PLAYED && event.c>=0)
        snprintf(detail,sizeof(detail),"order %02X pattern %02X started",event.c,event.b);
    else if(event.type==TT_PLAY_ROW_PLAYED)
        snprintf(detail,sizeof(detail),"pattern %02X loop started",event.b);
    else if(event.type==TT_PLAY_AUDITION_CELL)
        snprintf(detail,sizeof(detail),"%s row %02X ch %d",name,event.a,event.b+1);
    else if(event.type==TT_PLAY_SELECT_ORDER)
        snprintf(detail,sizeof(detail),"%s %02X",name,event.a);
    else if(event.type==TT_PLAY_SELECT_PATTERN)
        snprintf(detail,sizeof(detail),"%s %02X",name,event.a);
    else if(event.type==TT_PLAY_PATTERN_JUMP)
        snprintf(detail,sizeof(detail),"%s %02X",name,event.a);
    else if(event.type==TT_PLAY_ROW_JUMP)
        snprintf(detail,sizeof(detail),"%s %02X",name,event.a);
    else if(event.type==TT_PLAY_SET_TICKS_PER_ROW)
        snprintf(detail,sizeof(detail),"%s %d",name,event.a?event.a:tracker.song.ticks_per_row);
    else if(event.type==TT_PLAY_SET_BPM)
        snprintf(detail,sizeof(detail),"%s %d",name,event.a?event.a:tracker.song.bpm);
    else if(event.type==TT_PLAY_SET_ORDER_RANGE)
        snprintf(detail,sizeof(detail),"%s %02X-%02X",name,event.a,event.b);
    else snprintf(detail,sizeof(detail),"%s",name);
    if(event.type!=TT_PLAY_ROW_PLAYED || event.a==0)
        tt_tracker_log_event("PLAY",detail);
    return 1;
}
static int pattern_jump_order(int pattern) {
    if(tracker.song.order_count<=0)return -1;
    int first=0,last=tracker.song.order_count-1;
    if(tracker.order_range_active) {
        first=tracker.order_range_anchor<tracker.order_range_end?
            tracker.order_range_anchor:tracker.order_range_end;
        last=tracker.order_range_anchor>tracker.order_range_end?
            tracker.order_range_anchor:tracker.order_range_end;
    }
    int count=last-first+1;
    int current=tracker.play_order;
    if(current<first || current>last)current=first-1;
    for(int offset=1;offset<=count;++offset) {
        int at=first+(current-first+offset)%count;
        if(tracker.song.order[at]==pattern &&
           (tracker.order_range_active || tracker.song.order_active[at]))
            return at;
    }
    return -1;
}
void tt_play_flush(void) {
    while(play_queue_count) {
        TtPlayEvent event=play_queue[0];
        --play_queue_count;
        memmove(play_queue,play_queue+1,(size_t)play_queue_count*sizeof(*play_queue));
        switch(event.type) {
        case TT_PLAY_START:play_start_now(NULL,NULL);break;
        case TT_PLAY_STOP:play_stop_now(NULL,NULL);break;
        case TT_PLAY_TOGGLE:play_toggle_now(NULL,NULL);break;
        case TT_PLAY_SET_MODE:
            tracker.play_mode=event.a;
            tracker.order_range_active=0;
            play_start_now(NULL,NULL);break;
        case TT_PLAY_TOGGLE_MODE:
            tracker.play_mode=tracker.play_mode==PLAY_SONG?PLAY_PATTERN:PLAY_SONG;
            tracker.order_range_active=0;
            play_start_now(NULL,NULL);break;
        case TT_PLAY_TOGGLE_LOOP:
            tracker.loop=!tracker.loop;
            tt_tracker_message(tracker.loop?"Song loop on":"Song loop off");break;
        case TT_PLAY_TOGGLE_FOLLOW:
            tracker.follow=!tracker.follow;
            tt_tracker_message(tracker.follow?"Follow playhead on":"Follow playhead off");break;
        case TT_PLAY_SELECT_ORDER: {
            int at=event.a;
            if(at<0 || at>=tracker.song.order_count)break;
            tracker.order_range_active=0;
            tracker.play_mode=PLAY_SONG;
            if(tracker.playing) {
                if(!tracker.song.order_active[at]) {
                    int next=next_active_order(at,1);
                    if(next<0) {
                        next=tracker.song.order_count-1;
                        while(next>=0 && !tracker.song.order_active[next])--next;
                    }
                    if(next<0) { tracker.song_finished=1;break; }
                    at=next;
                }
                play_start_now(NULL,NULL);
            }
            tracker.play_order=at;tracker.play_step=0;
            tracker.active_order_index=at;
            tracker.active_pattern_index=tracker.song.order[at];
            tracker.active_step=0;
            char status[64];
            snprintf(status,sizeof(status),"Order %02X plays pattern %02X",
                     at,tracker.song.order[at]);
            tt_tracker_message(status);break;
        }
        case TT_PLAY_SELECT_PATTERN: {
            int at=event.a;
            if(at<0 || at>=tracker.song.pattern_count ||
               tracker.play_mode!=PLAY_PATTERN)break;
            tracker.order_range_active=0;
            tracker.loop_pattern_index=at;
            tracker.play_step=0;
            tracker.active_order_index=-1;
            tracker.active_pattern_index=at;
            tracker.active_step=tracker.playing?0:-1;
            if(tracker.playing)play_start_now(NULL,NULL);
            char status[48];
            snprintf(status,sizeof(status),"Looping pattern %02X",at);
            tt_tracker_message(status);break;
        }
        case TT_PLAY_PATTERN_JUMP: {
            if(event.a<0 || event.a>=tracker.song.pattern_count)break;
            int order=-1;
            if(tracker.play_mode==PLAY_SONG || tracker.order_range_active) {
                order=pattern_jump_order(event.a);
                if(order<0)break;
            }
            tracker.pending_flow=TT_PLAY_PATTERN_JUMP;
            tracker.pending_pattern=event.a;
            tracker.pending_order=order;
            tracker.pending_row=0;
            break;
        }
        case TT_PLAY_ROW_JUMP:
            if(event.a<0 || event.a>=tracker.song.pattern_rows)break;
            tracker.pending_flow=TT_PLAY_ROW_JUMP;
            tracker.pending_order=-1;
            tracker.pending_row=event.a;
            break;
        case TT_PLAY_SET_TICKS_PER_ROW:
            if(event.a<0 || event.a>TT_MAX_TICKS_PER_ROW)break;
            {
                int value=event.a?event.a:tracker.song.ticks_per_row;
                if(value!=tracker.playback_ticks_per_row)tracker.tick_remainder=0;
                tracker.playback_ticks_per_row=value;
            }
            break;
        case TT_PLAY_SET_BPM:
            if(event.a!=0 && (event.a<30 || event.a>255))break;
            {
                int value=event.a?event.a:tracker.song.bpm;
                if(value!=tracker.playback_bpm)tracker.tick_remainder=0;
                tracker.playback_bpm=value;
            }
            break;
        case TT_PLAY_APPLY_PENDING_FLOW:
            if(tracker.pending_flow==TT_PLAY_PATTERN_JUMP) {
                if(tracker.pending_order>=0)tracker.play_order=tracker.pending_order;
                if(tracker.play_mode==PLAY_PATTERN && !tracker.order_range_active)
                    tracker.loop_pattern_index=tracker.pending_pattern;
                tracker.play_step=0;
            } else if(tracker.pending_flow==TT_PLAY_ROW_JUMP)
                tracker.play_step=tracker.pending_row;
            tracker.pending_flow=0;
            break;
        case TT_PLAY_SET_ORDER_RANGE: {
            if(event.a<0 || event.a>=tracker.song.order_count ||
               event.b<0 || event.b>=tracker.song.order_count)break;
            tracker.order_range_active=1;
            tracker.order_range_anchor=event.a;
            tracker.order_range_end=event.b;
            if(event.c && tracker.playing)play_start_now(NULL,NULL);
            int first=event.a<event.b?event.a:event.b;
            int last=event.a>event.b?event.a:event.b;
            if(event.c || tracker.play_order<first || tracker.play_order>last) {
                tracker.play_order=event.c?event.a:first;tracker.play_step=0;
                tracker.active_order_index=tracker.play_order;
                tracker.active_pattern_index=tracker.song.order[tracker.play_order];
                tracker.active_step=0;
            }
            break;
        }
        case TT_PLAY_CLEAR_ORDER_RANGE:
            tracker.order_range_active=0;
            break;
        case TT_PLAY_AUDITION:play_audition_now(NULL,NULL);break;
        case TT_PLAY_AUDITION_CELL:play_audition_cell_now(event.a,event.b);break;
        case TT_PLAY_ROW_PLAYED:
            tracker.active_step=event.a;
            tracker.active_pattern_index=event.b;
            tracker.active_order_index=event.c;
            if(tracker.follow && event.c>=0 && tracker.order_list)
                tracker.order_list->cursor=event.c;
            break;
        case TT_PLAY_FINISHED:
            tt_audio_end_song();tracker.playing=tracker.started=0;break;
        case TT_PLAY_DOCUMENT_RESET:
            tracker.order_range_active=0;
            tracker.play_mode=PLAY_SONG;
            tracker.play_order=tracker.play_step=0;
            tracker.playback_bpm=tracker.song.bpm;
            tracker.playback_ticks_per_row=tracker.song.ticks_per_row;
            break;
        case TT_PLAY_AUDIO_RECOVER:
            tracker.audio_recovery_result=tt_audio_recover();
            if(tracker.audio_recovery_result) {
                if(tracker.playing) {
                    tracker.frames_to_tick=0;
                    tracker.song_written_frames=0;
                    tracker.display_head=tracker.display_tail=0;
                }
                tt_tracker_message("Audio output restored");
            } else tt_tracker_message("Audio output could not be restored; tap to retry");
            break;
        }
    }
}
void tt_play_advance(void) {
    tt_play_flush();
    advance_playback_now();
    advance_audition_now();
}
