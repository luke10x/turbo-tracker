#ifndef TT_TRACKER_INTERNAL_H
#define TT_TRACKER_INTERNAL_H
#include <SDL3/SDL.h>
#include "f80.h"
#include "f80_components.h"
#include "tt_edit.h"
#include "tt_song.h"
enum { FOCUS_WAVE, FOCUS_PHRASE, FOCUS_ORDER, FOCUS_REFERENCE };
enum { PLAY_SONG, PLAY_PATTERN };
enum { PATH_WAV, PATH_SONG_LOAD, PATH_SONG_SAVE };
enum { TT_HISTORY_MAX=8 };
enum { SONG_LOOKAHEAD_FRAMES=16384, SONG_DISPLAY_ROWS=64 };
typedef struct {
    int active,table,tick,next_table,delay,pending;
    int delayed_glide;
    TtCell delayed_note;
} TableVoice;
typedef struct {
    Uint64 frame;
    int row,pattern,order;
} SongDisplayRow;
typedef struct {
    F80App app;
    TtWave wave;
    TtPhrase phrase;
    TtPattern pattern;
    TtSong song;
    F80Widget *root,*header_gap,*console,*wave_row,*order_frame,*order_list,*wave_frame,*wave_canvas;
    F80Widget *phrase_frame,*phrase_canvas;
    F80Widget *header,*header_title,*play_button,*stop_button,*follow_button,*mode_button;
    F80Widget *progress,*bpm_label,*details,*status;
    int focus,marker,zoom,view_start,step,channel,field,entry_octave;
    int note_input_length;
    char note_input[4];
    int selecting,visual_selecting,anchor_step,anchor_channel,clip_rows,clip_channels;
    TtCell clipboard[TT_MAX_STEPS][TT_CHANNELS];
    int phrase_scroll,field_scroll,path_action,edit_order,reveal_cursor;
    int compact_editing,effect_slots;
    int playing,started,follow,play_step,play_order,active_step,active_pattern_index;
    int active_order_index,loop,pattern_index,play_mode,loop_pattern_index;
    int order_range_active,order_range_anchor,order_range_end;
    int grid_selection_anchor,grid_selection_end,grid_selection_invalid;
    Uint64 next_audition_tick_at;
    int audition_effect_active;
    int tick_phase,tick_remainder;
    int frames_to_tick,song_finished,rendering_song;
    Uint64 song_written_frames;
    SongDisplayRow display_rows[SONG_DISPLAY_ROWS];
    int display_head,display_tail;
    TableVoice table_voice[TT_CHANNELS];
    F80Widget *table_dialog,*table_panel,*table_canvas,*table_name,*table_name_label;
    F80Widget *table_scrollbar,*table_buttons[5];
    F80Grid table_grid;
    F80Wrap table_actions;
    int table_index;
    TtTableTick table_clip[TT_TABLE_MAX_TICKS];
    int table_clip_rows,table_clip_left,table_clip_right;
    char table_title[32];
    char header_text[256],bpm_text[16],details_text[256],wave_title[160],phrase_title[96];
    char message[192],loaded_path[512],csv_path[256],song_path[512];
    int console_visible,console_scroll;
    TtStored samples[TT_HISTORY_MAX];
    int sample_count,sample_index;
    F80Widget *manage_dialog,*manage_list_bg,*manage_list,*manage_name,*manage_info;
    F80Widget *slices_dialog,*slices_list_bg,*slices_list,*slices_note,*slices_info;
    F80Widget *order_dialog,*order_bank_label,*order_bank_bg,*order_bank;
    F80Widget *order_seq_label,*order_seq_bg,*order_seq,*order_name;
    F80Widget *settings_dialog,*settings_label[4],*settings_input[4];
    F80Widget *settings_apply,*settings_close;
    char bank_rows[TT_SONG_PATTERNS][100],order_rows[TT_SONG_ORDER][100];
    char order_panel_rows[TT_SONG_ORDER][8];
    char order_heading[64];
    const char *bank_items[TT_SONG_PATTERNS],*order_items[TT_SONG_ORDER];
    const char *order_panel_items[TT_SONG_ORDER];
    char manage_rows[TT_HISTORY_MAX][290],slice_rows[TT_SLICES][96];
    const char *manage_items[TT_HISTORY_MAX],*slice_items[TT_SLICES];
    char manage_details[320],slice_details[320];
    int delete_armed;
} TtTracker;
extern TtTracker tracker;
void tt_tracker_message(const char *text);
void tt_tracker_log_event(const char *category,const char *description);
#endif
