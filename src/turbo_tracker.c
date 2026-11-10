#include <curses.h>
#include <locale.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
#include <termios.h>
#include <unistd.h>
#endif
#include "f80.h"
#include "f80_components.h"
#include "f80_curses.h"
#ifndef __EMSCRIPTEN__
#include "f80_file_dialog.h"
#endif
#include "tt_audio.h"
#include "tt_csv.h"
#include "tt_pattern.h"
#include "tt_sample.h"
#include "tt_song.h"
#include "tt_edit.h"
#include "tt_tracker_internal.h"
#include "tt_play.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
static int web_audio_lifecycle_installed;
EM_JS(void, choose_web_wav, (), {
    if(window.f80ChooseWav)window.f80ChooseWav();
});
EM_JS(void, choose_web_song, (), {
    if(window.f80ChooseSong)window.f80ChooseSong();
});
EM_JS(void, download_web_song, (const char *name), {
    if(window.f80DownloadSong)window.f80DownloadSong(UTF8ToString(name));
});
EM_JS(void, tt_web_grid_rect, (int x,int y,int width,int height), {
    Module.f80GridRect = {x,y,width,height};
});
EM_JS(void, tt_web_editable_grid_rect, (int x,int y,int width,int height), {
    Module.f80EditableGridRect = {x,y,width,height};
});
EM_JS(void, tt_web_wave_rect, (int x,int y,int width,int height), {
    Module.f80WaveRect = {x,y,width,height};
});
EM_JS(void, tt_web_order_rect, (int x,int y,int width,int height), {
    Module.f80OrderRect = {x,y,width,height};
});
EM_JS(void, tt_web_table_rect, (int x,int y,int width,int height,int horizontal,int vertical), {
    Module.f80TableRect = {x,y,width,height,horizontal,vertical};
});
EM_JS(void, tt_web_wrap_rect, (int x,int y,int width,int height), {
    Module.f80WrapRect = {x,y,width,height};
});
EM_JS(int, tt_web_compact_editing, (), {
    return !!window.f80CompactEditing;
});
EM_JS(void, tt_web_install_audio_lifecycle, (), {
    if (Module.ttAudioLifecycleInstalled) return;
    Module.ttAudioLifecycleInstalled = true;
    Module.ttAudioLifecyclePending = false;
    let lastAudioRecovery = 0;
    const recover = (fromGesture = false) => {
        if (!Module.ttAudioLifecyclePending || document.hidden || !Module.ccall) return;
        const now = Date.now();
        if (!fromGesture && now - lastAudioRecovery < 500) return;
        lastAudioRecovery = now;
        try {
            if (Module.ccall('tt_tracker_audio_recover', 'number', [], []) && fromGesture)
                Module.ttAudioLifecyclePending = false;
        } catch (error) {
            console.warn('Turbo Tracker audio recovery failed', error);
        }
    };
    const markBackgrounded = () => { Module.ttAudioLifecyclePending = true; };
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) markBackgrounded();
        else recover();
    });
    window.addEventListener('pagehide', markBackgrounded);
    document.addEventListener('freeze', markBackgrounded);
    window.addEventListener('pageshow', recover);
    window.addEventListener('focus', recover);
    window.addEventListener('pointerdown', () => recover(true), true);
    window.addEventListener('touchstart', () => recover(true), true);
});
EMSCRIPTEN_KEEPALIVE int tt_tracker_audio_recover(void) {
    if (!tt_play_emit((TtPlayEvent){.type=TT_PLAY_AUDIO_RECOVER}))return 0;
    tt_play_flush();
    return tracker.audio_recovery_result;
}
#endif

TtTracker tracker;
static TtEditHistory *edit_history;
enum { TT_EVENT_LOG_CAPACITY=512, TT_EVENT_LOG_WIDTH=192 };
static char event_log[TT_EVENT_LOG_CAPACITY][TT_EVENT_LOG_WIDTH];
static int event_log_head,event_log_count;
static char event_console_menu_label[32]="~S~how event console";
static F80Wrap order_pattern_actions,order_play_actions;
static F80Widget *order_pattern_buttons[4],*order_play_buttons[4];

void tt_tracker_log_event(const char *category,const char *description) {
    Uint64 ticks=SDL_GetTicks();
    snprintf(event_log[event_log_head],TT_EVENT_LOG_WIDTH,
             "[%02llu:%02llu.%03llu] %-6.6s %s",
             (unsigned long long)(ticks/60000),
             (unsigned long long)((ticks/1000)%60),
             (unsigned long long)(ticks%1000),category,description);
    event_log_head=(event_log_head+1)%TT_EVENT_LOG_CAPACITY;
    if(event_log_count<TT_EVENT_LOG_CAPACITY)++event_log_count;
    if(tracker.console_scroll>0)++tracker.console_scroll;
}
static const char *edit_event_name(TtEditType type) {
    static const char *const names[TT_EDIT_COUNT]={
        [TT_EDIT_CELL_SET]="cell set",[TT_EDIT_CELLS_BULK]="cells bulk",
        [TT_EDIT_PATTERN_NEW]="pattern new",[TT_EDIT_PATTERN_RENAME]="pattern rename",
        [TT_EDIT_PATTERN_APPEND_ORDER]="pattern and order new",
        [TT_EDIT_PATTERN_SELECT]="pattern select",[TT_EDIT_PATTERN_SYNC]="pattern sync",
        [TT_EDIT_PATTERN_EFFECTS_COMPACT]="pattern effects compact",
        [TT_EDIT_ORDER_INSERT]="order insert",[TT_EDIT_ORDER_REMOVE]="order remove",
        [TT_EDIT_ORDER_SWAP]="order swap",[TT_EDIT_ORDER_ACTIVE]="order activity",
        [TT_EDIT_ORDER_PATTERN]="order pattern",[TT_EDIT_ORDER_APPEND]="order append",
        [TT_EDIT_BPM]="BPM",
        [TT_EDIT_SETTINGS]="settings",[TT_EDIT_TABLE_TICK_SET]="table tick",
        [TT_EDIT_TABLE_TICKS_BULK]="table ticks bulk",
        [TT_EDIT_TABLE_LENGTH]="table length",[TT_EDIT_TABLE_NAME]="table name",
        [TT_EDIT_SAMPLE_RENAME]="sample rename",
        [TT_EDIT_SAMPLE_REFERENCE]="sample reference",
        [TT_EDIT_SAMPLE_SWAP]="sample swap",[TT_EDIT_SAMPLE_DELETE]="sample delete",
        [TT_EDIT_SAMPLE_SELECT]="sample select",[TT_EDIT_SAMPLE_APPEND]="sample append",
        [TT_EDIT_SAMPLE_RECOVER]="sample recover",
        [TT_EDIT_MARKER_MOVE]="marker move",[TT_EDIT_SLICE_NOTE]="slice note",
        [TT_EDIT_DOCUMENT_REPLACE]="document replace"
    };
    return type>TT_EDIT_NONE && type<TT_EDIT_COUNT && names[type]?names[type]:"unknown";
}

#ifndef __EMSCRIPTEN__
static F80FileDialog file_dialog;
#endif

static int clamp(int value,int low,int high) {
    return value<low?low:value>high?high:value;
}
/* Song-edit channel: every song mutation from the UI goes through here. */
static char edit_message[192];
static TtEditDoc edit_doc(void) {
    return (TtEditDoc){
        .song=&tracker.song,.active=&tracker.pattern,.active_index=&tracker.pattern_index,
        .samples=tracker.samples,.active_wave=&tracker.wave,.active_phrase=&tracker.phrase,
        .sample_count=&tracker.sample_count,.sample_index=&tracker.sample_index,
        .active_loaded_path=tracker.loaded_path,.active_csv_path=tracker.csv_path,
        .song_path=tracker.song_path,
        .msg=edit_message,.msg_size=(int)sizeof(edit_message)
    };
}
static int emit_edit(const TtEditEvent *event) {
    TtEditDoc doc=edit_doc();
    edit_message[0]=0;
    int applied=tt_edit_apply_recorded(edit_history,&doc,event);
    if(applied) {
        if(event->type!=TT_EDIT_PATTERN_SELECT && event->type!=TT_EDIT_PATTERN_SYNC &&
           event->type!=TT_EDIT_SAMPLE_SELECT)tracker.document_dirty=1;
        char detail[128];
        if(event->type==TT_EDIT_CELL_SET)
            snprintf(detail,sizeof(detail),"%s row %02X ch %d",edit_event_name(event->type),
                     event->cell_set.row,event->cell_set.channel+1);
        else if(event->type==TT_EDIT_TABLE_TICK_SET)
            snprintf(detail,sizeof(detail),"%s table %02X tick %02X",
                     edit_event_name(event->type),event->table_tick.table,event->table_tick.tick);
        else snprintf(detail,sizeof(detail),"%s",edit_event_name(event->type));
        tt_tracker_log_event("EDIT",detail);
    }
    return applied;
}
static int pattern_needs_effect_compaction(void) {
    for(int pattern=0;pattern<tracker.song.pattern_count;++pattern) {
        const TtPattern *source=pattern==tracker.pattern_index?
            &tracker.pattern:&tracker.song.pattern[pattern];
        for(int row=0;row<TT_MAX_STEPS;++row)
            for(int channel=0;channel<TT_CHANNELS;++channel) {
                const TtCell *cell=&source->cell[row][channel];
                int empty=cell->effect==TT_EFFECT_NONE;
                for(int slot=1;slot<3;++slot) {
                    const TtPatternEffect *fx=tt_cell_effect_const(cell,slot);
                    if(fx->command!=TT_EFFECT_NONE && empty)return 1;
                    if(fx->command==TT_EFFECT_NONE)empty=1;
                }
            }
    }
    return 0;
}
static void compact_pattern_effects_if_needed(void) {
    if(!pattern_needs_effect_compaction())return;
    TtEditEvent event={.type=TT_EDIT_PATTERN_EFFECTS_COMPACT};
    if(!emit_edit(&event))tt_tracker_message(edit_message);
}
static void edit_cell(int row,int channel,TtCell cell) {
    TtEditEvent event={.type=TT_EDIT_CELL_SET};
    event.cell_set.row=row;event.cell_set.channel=channel;event.cell_set.cell=cell;
    emit_edit(&event);
}
static void edit_cells(TtBulkOp op,int top,int left,int bottom,int right,int delta,
                       const TtCell *data,int data_cols) {
    TtEditEvent event={.type=TT_EDIT_CELLS_BULK};
    event.cells.op=op;event.cells.top=top;event.cells.left=left;
    event.cells.bottom=bottom;event.cells.right=right;event.cells.delta=delta;
    event.cells.data=data;event.cells.data_cols=data_cols;
    emit_edit(&event);
}
static void edit_marker(int marker,int frame) {
    TtEditEvent event={.type=TT_EDIT_MARKER_MOVE};
    event.marker=(TtEditMarker){marker,frame};emit_edit(&event);
}
static void edit_slice(int slice,int note) {
    TtEditEvent event={.type=TT_EDIT_SLICE_NOTE};
    event.slice=(TtEditSlice){slice,note};emit_edit(&event);
}
static void reveal_order_cursor(int cursor) {
    if(!tracker.order_list)return;
    tracker.order_list->cursor=cursor;
    int body=tracker.order_list->rect.h;
    if(body<1)return;
    if(cursor<tracker.order_list->scroll)
        tracker.order_list->scroll=cursor;
    else if(cursor>=tracker.order_list->scroll+body)
        tracker.order_list->scroll=cursor-body+1;
}
void tt_tracker_message(const char *text) {
    snprintf(tracker.message,sizeof(tracker.message),"%s",text);
    tt_tracker_log_event("STATUS",text);
}
static void send_play(TtPlayType type,int a,int b) {
    if(tt_play_emit((TtPlayEvent){type,a,b,0}))tt_play_flush();
}
static void send_play_range(int first,int last,int begin) {
    if(tt_play_emit((TtPlayEvent){TT_PLAY_SET_ORDER_RANGE,first,last,begin}))
        tt_play_flush();
}
static void leave_follow(void) {
    if(tracker.follow)send_play(TT_PLAY_TOGGLE_FOLLOW,0,0);
}
static void stop_playback(F80Widget *button,void *user) {
    (void)button;(void)user;send_play(TT_PLAY_STOP,0,0);
}
static void toggle_play(F80Widget *button,void *user) {
    (void)button;(void)user;send_play(TT_PLAY_TOGGLE,0,0);
}
static void audition(F80Widget *button,void *user) {
    (void)button;(void)user;send_play(TT_PLAY_AUDITION,0,0);
}

static int order_for_pattern(int pattern,int near) {
    int found=-1,best=TT_SONG_ORDER+1;
    for(int i=0;i<tracker.song.order_count;++i)if(tracker.song.order[i]==pattern) {
        int distance=abs(i-near);
        if(distance<best) { found=i;best=distance; }
    }
    return found;
}
static int grid_base_total_rows(void) {
    return tracker.edit_order>=0?tracker.song.order_count*tracker.song.pattern_rows:tracker.song.pattern_rows;
}
static int inline_table_length(void) {
    return tracker.inline_table_open && tracker.inline_table_index>=0 &&
           tracker.inline_table_index<TT_SONG_TABLES?
           tracker.song.table[tracker.inline_table_index].length:0;
}
static int grid_total_rows(void) {
    return grid_base_total_rows()+inline_table_length();
}
static int grid_is_inline_table_row(int visual,int *tick) {
    int length=inline_table_length(),parent=tracker.inline_table_parent;
    if(length && visual>parent && visual<=parent+length) {
        if(tick)*tick=visual-parent-1;
        return 1;
    }
    return 0;
}
static int grid_base_row(int visual) {
    int length=inline_table_length(),parent=tracker.inline_table_parent;
    if(length && visual>parent && visual<=parent+length)return parent;
    return length && visual>parent+length?visual-length:visual;
}
static int grid_visual_row(int base) {
    int length=inline_table_length(),parent=tracker.inline_table_parent;
    return length && base>parent?base+length:base;
}
static int grid_edit_row(void) {
    return grid_visual_row((tracker.edit_order>=0?
        tracker.edit_order*tracker.song.pattern_rows:0)+tracker.step);
}
static int grid_order_for_row(int global) {
    global=grid_base_row(global);
    return tracker.edit_order>=0?global/tracker.song.pattern_rows:-1;
}
static int grid_pattern_for_row(int global) {
    global=grid_base_row(global);
    int order=grid_order_for_row(global);
    return order>=0?tracker.song.order[order]:tracker.pattern_index;
}
static int pattern_fields_per_channel(int channel);
static int pattern_edit_fields_per_channel(int channel);
typedef enum {
    TT_UI_SELECT_PATTERN, TT_UI_SELECT_PATTERN_ONLY, TT_UI_SELECT_ORDER,
    TT_UI_MODE_CONTEXT, TT_UI_SYNC_PATTERN_ORDER,
    TT_UI_FOCUS, TT_UI_SCROLL_ORDER, TT_UI_ORDER_CURSOR,
    TT_UI_PATTERN_LOCK,
    TT_UI_BEGIN_VISUAL_SELECTION, TT_UI_EXTEND_VISUAL_SELECTION,
    TT_UI_END_VISUAL_SELECTION, TT_UI_PATTERN_CURSOR,
    TT_UI_PATTERN_ROW_CURSOR, TT_UI_INLINE_TABLE_TOGGLE,
    TT_UI_INLINE_TABLE_CURSOR, TT_UI_EXIT_REQUEST,
    TT_UI_EXIT_CONFIRM, TT_UI_EXIT_CANCEL
} TtUiType;
typedef struct { TtUiType type; int index,a,b; } TtUiEvent;
static int handle_ui_event(TtUiEvent event);
static void leave_follow(void);
static void confirm_exit(F80Widget *button,void *user) {
    (void)button;(void)user;
    handle_ui_event((TtUiEvent){.type=TT_UI_EXIT_CONFIRM});
}
static void cancel_exit(F80Widget *button,void *user) {
    (void)button;(void)user;
    handle_ui_event((TtUiEvent){.type=TT_UI_EXIT_CANCEL});
}
static int handle_ui_event(TtUiEvent event) {
    if(event.type==TT_UI_SYNC_PATTERN_ORDER) {
        if(tracker.play_mode!=PLAY_SONG)return 1;
        if(tracker.edit_order>=0 &&
           tracker.edit_order<tracker.song.order_count &&
           tracker.song.order[tracker.edit_order]==tracker.pattern_index)return 1;
        int found=order_for_pattern(tracker.pattern_index,tracker.edit_order);
        if(found!=tracker.edit_order) {
            tracker.edit_order=found;
            tracker.reveal_cursor=1;
        }
        return 1;
    }
    if(event.type==TT_UI_MODE_CONTEXT) {
        if(event.a==PLAY_PATTERN) {
            tracker.edit_order=-1;
            tracker.phrase_scroll=0;
        } else {
            int order=order_for_pattern(tracker.pattern_index,tracker.edit_order);
            if(order<0)order=tracker.order_list?
                clamp(tracker.order_list->cursor,0,tracker.song.order_count-1):0;
            if(tracker.song.order_count>0) {
                if(tracker.song.order[order]!=tracker.pattern_index) {
                    TtEditEvent edit={.type=TT_EDIT_PATTERN_SELECT};
                    edit.pattern.index=tracker.song.order[order];
                    if(!emit_edit(&edit))return 0;
                }
                tracker.edit_order=order;
                tracker.phrase_scroll=order*tracker.song.pattern_rows;
            }
        }
        tracker.reveal_cursor=1;
        return 1;
    }
    if(event.type==TT_UI_EXIT_REQUEST) {
        if(tracker.exit_dialog && tracker.app.modal==tracker.exit_dialog)return 1;
        if(!tracker.document_dirty) {
            tt_tracker_log_event("UI","Exit requested");
            tracker.app.running=0;
            return 1;
        }
        if(!tracker.exit_dialog) {
            static const char *const lines[]={"Unsaved changes will be lost."};
            tracker.exit_dialog=f80_dialog("Exit without saving?",lines,NULL,1,1);
            tracker.exit_dialog->dialog_default=1;
            f80_add(tracker.exit_dialog,
                    f80_button("~E~xit without saving",confirm_exit,NULL));
            f80_add(tracker.exit_dialog,f80_button("~C~ancel",cancel_exit,NULL));
        }
        tt_tracker_log_event("UI","Exit confirmation opened");
        f80_show_modal(&tracker.app,tracker.exit_dialog);
        return 1;
    }
    if(event.type==TT_UI_EXIT_CONFIRM) {
        if(tracker.app.modal==tracker.exit_dialog) {
            f80_hide_modal(&tracker.app);
            tracker.exit_dialog=NULL;
        }
        tt_tracker_log_event("UI","Exit confirmed with unsaved changes");
        tracker.app.running=0;
        return 1;
    }
    if(event.type==TT_UI_EXIT_CANCEL) {
        if(tracker.app.modal==tracker.exit_dialog) {
            f80_hide_modal(&tracker.app);
            tracker.exit_dialog=NULL;
        }
        tt_tracker_log_event("UI","Exit cancelled");
        return 1;
    }
    if(event.type==TT_UI_PATTERN_LOCK) {
        tracker.pattern_lock=event.a!=0;
        tt_tracker_log_event("UI",tracker.pattern_lock?
            "Pattern navigation locked":"Pattern navigation unlocked");
        return 1;
    }
    if(event.type==TT_UI_INLINE_TABLE_TOGGLE) {
        int table=event.index;
        if(table<0 || table>=TT_SONG_TABLES || event.b<0 || event.b>=TT_CHANNELS ||
           event.a<0 || event.a>=grid_base_total_rows())return 0;
        if(tracker.inline_table_open && tracker.inline_table_index==table &&
           tracker.inline_table_parent==event.a && tracker.inline_table_channel==event.b) {
            tracker.inline_table_open=0;tracker.inline_table_selected=0;
            tt_tracker_log_event("UI","Inline table collapsed");
        } else {
            tracker.inline_table_open=1;tracker.inline_table_index=table;
            tracker.inline_table_parent=event.a;tracker.inline_table_channel=event.b;
            tracker.inline_table_selected=0;tracker.inline_table_tick=0;
            tracker.inline_table_field=0;
            tt_tracker_log_event("UI","Inline table expanded");
        }
        tracker.reveal_cursor=1;
        return 1;
    }
    if(event.type==TT_UI_INLINE_TABLE_CURSOR) {
        if(!tracker.inline_table_open)return 0;
        if(event.a==-1) { tracker.inline_table_selected=0;return 1; }
        int length=inline_table_length();
        if(event.a<0 || event.a>=length || event.b<0 || event.b>=14)return 0;
        tracker.note_input_length=0;
        tracker.inline_table_tick=event.a;tracker.inline_table_field=event.b;
        tracker.inline_table_selected=1;
        tracker.reveal_cursor=1;
        return 1;
    }
    if(event.type==TT_UI_PATTERN_ROW_CURSOR) {
        if(event.a<0 || event.a>=tracker.song.pattern_rows)return 0;
        tracker.step=event.a;
        tracker.inline_table_selected=0;
        tracker.reveal_cursor=1;
        return 1;
    }
    if(event.type==TT_UI_PATTERN_CURSOR) {
        tracker.inline_table_selected=0;
        if(tracker.field>=2 && event.b<2)compact_pattern_effects_if_needed();
        int channel=clamp(event.a,0,TT_CHANNELS-1);
        if(event.b>=2) {
            int requested=(event.b-2)/2+1;
            if(requested>tracker.effect_slots[channel])
                tracker.effect_slots[channel]=requested;
        }
        int fields=pattern_edit_fields_per_channel(channel);
        tracker.channel=channel;
        tracker.field=clamp(event.b,0,fields-1);
        tracker.reveal_cursor=1;
        return 1;
    }
    if(event.type==TT_UI_FOCUS) {
        if(tracker.focus==FOCUS_PHRASE && event.index!=FOCUS_PHRASE)
            compact_pattern_effects_if_needed();
        tracker.focus=event.index;
        if(event.index!=FOCUS_PHRASE)tracker.inline_table_selected=0;
        if(event.index!=FOCUS_PHRASE)
            memset(tracker.effect_slots,0,sizeof(tracker.effect_slots));
        tracker.app.focus=event.index==FOCUS_ORDER?tracker.order_list:
            event.index==FOCUS_PHRASE?tracker.phrase_canvas:tracker.wave_canvas;
        return 1;
    }
    if(event.type==TT_UI_SCROLL_ORDER) {
        if(!tracker.order_list || !event.index)return 0;
        leave_follow();
        F80Widget *list=tracker.order_list;
        int body=list->rect.h>0?list->rect.h:1;
        int maximum=list->item_count>body?list->item_count-body:0;
        list->scroll=clamp(list->scroll+event.index,0,maximum);
        list->scroll_detached=1;
        return 1;
    }
    if(event.type==TT_UI_ORDER_CURSOR) {
        if(!tracker.order_list || event.index<0 ||
           event.index>=tracker.order_list->item_count)return 0;
        reveal_order_cursor(event.index);
        return 1;
    }
    if(event.type==TT_UI_BEGIN_VISUAL_SELECTION) {
        if(tracker.focus!=FOCUS_PHRASE)return 0;
        tracker.anchor_step=tracker.step;
        tracker.anchor_channel=tracker.channel;
        tracker.grid_selection_invalid=0;
        tracker.selecting=tracker.visual_selecting=1;
        tt_tracker_log_event("UI","Visual selection started");
        return 1;
    }
    if(event.type==TT_UI_EXTEND_VISUAL_SELECTION) {
        if(tracker.focus!=FOCUS_PHRASE || !tracker.selecting ||
           !tracker.visual_selecting)return 0;
        leave_follow();
        if(event.index==F80_KEY_UP)
            tracker.step=clamp(tracker.step-1,0,tracker.song.pattern_rows-1);
        else if(event.index==F80_KEY_DOWN)
            tracker.step=clamp(tracker.step+1,0,tracker.song.pattern_rows-1);
        else if(event.index==F80_KEY_LEFT)
            tracker.channel=clamp(tracker.channel-1,0,TT_CHANNELS-1);
        else if(event.index==F80_KEY_RIGHT)
            tracker.channel=clamp(tracker.channel+1,0,TT_CHANNELS-1);
        else return 0;
        if(event.index==F80_KEY_UP || event.index==F80_KEY_DOWN)
            tracker.reveal_cursor=1;
        return 1;
    }
    if(event.type==TT_UI_END_VISUAL_SELECTION) {
        if(!tracker.visual_selecting)return 0;
        tracker.selecting=tracker.visual_selecting=0;
        tt_tracker_log_event("UI","Visual selection ended");
        return 1;
    }
    int order=event.type==TT_UI_SELECT_ORDER?event.index:-1;
    int index=order>=0 && order<tracker.song.order_count?
        tracker.song.order[order]:event.index;
    if(index<0 || index>=tracker.song.pattern_count ||
       (event.type==TT_UI_SELECT_ORDER &&
        (order<0 || order>=tracker.song.order_count)))return 0;
    int pattern_was_current=index==tracker.pattern_index;
    if(!pattern_was_current) {
        TtEditEvent edit={.type=TT_EDIT_PATTERN_SELECT};
        edit.pattern.index=index;
        if(!emit_edit(&edit)) { tt_tracker_message(edit_message);return 0; }
    }
    if(event.type==TT_UI_SELECT_PATTERN && pattern_was_current)return 1;
    if(event.type==TT_UI_SELECT_PATTERN_ONLY) {
        tracker.edit_order=-1;
        tracker.phrase_scroll=0;
        tracker.reveal_cursor=1;
        tracker.selecting=0;
        return 1;
    }
    if(order>=0 && tracker.order_list)reveal_order_cursor(order);
    tracker.edit_order=order>=0?order:order_for_pattern(index,tracker.edit_order);
    if(tracker.order_list)tracker.order_list->scroll_detached=0;
    tracker.phrase_scroll=tracker.edit_order>=0?
        tracker.edit_order*tracker.song.pattern_rows:0;
    tracker.reveal_cursor=1;
    tracker.selecting=0;
    return 1;
}
static void select_edit_pattern(int index) {
    if(tracker.play_mode==PLAY_PATTERN) {
        if(handle_ui_event((TtUiEvent){TT_UI_SELECT_PATTERN_ONLY,index}))
            send_play(TT_PLAY_SELECT_PATTERN,index,0);
    } else handle_ui_event((TtUiEvent){TT_UI_SELECT_PATTERN,index});
}
static void move_pattern_row(int delta) {
    int row=tracker.step+delta;
    if(tracker.pattern_lock || tracker.play_mode!=PLAY_SONG ||
       tracker.edit_order<0) {
        handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_ROW_CURSOR,
            .a=clamp(row,0,tracker.song.pattern_rows-1)});
        return;
    }
    int order=tracker.edit_order;
    if(row<0) { --order;row=tracker.song.pattern_rows-1; }
    else if(row>=tracker.song.pattern_rows) { ++order;row=0; }
    order=clamp(order,0,tracker.song.order_count-1);
    if(order!=tracker.edit_order) {
        leave_follow();
        if(!handle_ui_event((TtUiEvent){.type=TT_UI_SELECT_ORDER,
                                        .index=order}))return;
    }
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_ROW_CURSOR,.a=row});
}
static void selection_bounds(int *top,int *left,int *bottom,int *right) {
    *top=tracker.anchor_step<tracker.step?tracker.anchor_step:tracker.step;
    *bottom=tracker.anchor_step>tracker.step?tracker.anchor_step:tracker.step;
    *left=tracker.anchor_channel<tracker.channel?tracker.anchor_channel:tracker.channel;
    *right=tracker.anchor_channel>tracker.channel?tracker.anchor_channel:tracker.channel;
}
static int selected_cell(int global,int channel) {
    if(!tracker.selecting || tracker.focus!=FOCUS_PHRASE)return 0;
    if(tracker.grid_selection_invalid) {
        int top=tracker.grid_selection_anchor<tracker.grid_selection_end?
            tracker.grid_selection_anchor:tracker.grid_selection_end;
        int bottom=tracker.grid_selection_anchor>tracker.grid_selection_end?
            tracker.grid_selection_anchor:tracker.grid_selection_end;
        int left=tracker.anchor_channel<tracker.channel?tracker.anchor_channel:tracker.channel;
        int right=tracker.anchor_channel>tracker.channel?tracker.anchor_channel:tracker.channel;
        return global>=top && global<=bottom && channel>=left && channel<=right;
    }
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    return global%tracker.song.pattern_rows>=top &&
           global%tracker.song.pattern_rows<=bottom &&
           global/tracker.song.pattern_rows==tracker.edit_order &&
           channel>=left && channel<=right;
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
    tracker.marker=clamp(marker,0,TT_MARKERS-1);
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

static void order_panel_selected(F80Widget *list,void *user) {
    (void)user;
    leave_follow();
    int count=tracker.play_mode==PLAY_SONG?tracker.song.order_count:
              tracker.song.pattern_count;
    int at=clamp(list->cursor,0,count-1);
    if(tracker.play_mode==PLAY_SONG) {
        if(!handle_ui_event((TtUiEvent){TT_UI_SELECT_ORDER,at}))return;
        send_play(TT_PLAY_SELECT_ORDER,at,0);
    } else if(handle_ui_event((TtUiEvent){TT_UI_SELECT_PATTERN_ONLY,at}))
        send_play(TT_PLAY_SELECT_PATTERN,at,0);
}
static int order_at(int x,int y) {
    if(!tracker.order_list)return -1;
    F80Rect r=tracker.order_list->rect;
    if(x<r.x || x>=r.x+r.w || y<r.y || y>=r.y+r.h)return -1;
    int at=tracker.order_list->scroll+y-r.y;
    return at<tracker.song.order_count?at:-1;
}
static void begin_order_range(int x,int y) {
    if(tracker.app.modal)return;
    int at=order_at(x,y);
    if(at<0)return;
    F80Rect r=tracker.order_list->rect;
    if(tracker.play_mode==PLAY_SONG && x<r.x+4) {
        handle_ui_event((TtUiEvent){.type=TT_UI_ORDER_CURSOR,.index=at});
        TtEditEvent edit={.type=TT_EDIT_ORDER_ACTIVE};
        edit.order.position=at;
        edit.order.pattern=!tracker.song.order_active[at];
        if(!emit_edit(&edit))tt_tracker_message(edit_message);
        return;
    }
    if(tracker.play_mode!=PLAY_SONG) {
        handle_ui_event((TtUiEvent){.type=TT_UI_ORDER_CURSOR,.index=at});
        order_panel_selected(tracker.order_list,NULL);
        return;
    }
    handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_ORDER});
    leave_follow();
    if(!handle_ui_event((TtUiEvent){TT_UI_SELECT_ORDER,at}))return;
    send_play_range(at,at,1);
}
static void drag_order_range(int x,int y) {
    if(!tracker.order_range_active || tracker.app.modal || !tracker.order_list)return;
    F80Rect r=tracker.order_list->rect;
    if(y<r.y)handle_ui_event((TtUiEvent){TT_UI_SCROLL_ORDER,-1});
    else if(y>=r.y+r.h)handle_ui_event((TtUiEvent){TT_UI_SCROLL_ORDER,1});
    int at=order_at(clamp(x,r.x,r.x+r.w-1),clamp(y,r.y,r.y+r.h-1));
    if(at>=0 && at!=tracker.order_range_end)
        send_play_range(tracker.order_range_anchor,at,0);
}
#ifdef __EMSCRIPTEN__
static void order_panel_scroll(int rows) {
    if(!tracker.order_list || tracker.app.modal || !rows)return;
    handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_ORDER});
    handle_ui_event((TtUiEvent){TT_UI_SCROLL_ORDER,rows});
}
#endif








static void start_song(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(handle_ui_event((TtUiEvent){.type=TT_UI_MODE_CONTEXT,.a=PLAY_SONG}))
        send_play(TT_PLAY_SET_MODE,PLAY_SONG,0);
}
static void start_pattern_loop(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(handle_ui_event((TtUiEvent){.type=TT_UI_MODE_CONTEXT,.a=PLAY_PATTERN}))
        send_play(TT_PLAY_SET_MODE,PLAY_PATTERN,0);
}




static int load_path(const char *path,const char *original_name) {
    if(tracker.sample_count>=TT_HISTORY_MAX) {
        tt_tracker_message("Sample slots full; restart to load a different set of WAVs");
        return 0;
    }
    TtWave next={0};
    char error[160];
    if(!tt_load_wav(path,&next,error,sizeof(error))) {
        tt_tracker_message(error);return 0;
    }
    stop_playback(NULL,NULL);
    if(original_name && *original_name) {
        snprintf(next.name,sizeof(next.name),"%s",original_name);
        snprintf(next.source_name,sizeof(next.source_name),"%s",original_name);
    }
    TtEditEvent event={.type=TT_EDIT_SAMPLE_APPEND};
    event.loaded_wave=(TtEditLoadedWave){0,&next,path};
    if(!emit_edit(&event)) { tt_free(&next);tt_tracker_message(edit_message);return 0; }
    tracker.zoom=tracker.view_start=tracker.marker=0;
    tt_tracker_message("WAV loaded; markers reset across the sample");
    return 1;
}
static void select_sample(int direction) {
    if(tracker.sample_count<2) {
        center_on_marker();
        tt_tracker_message("Only one sample; load another WAV from File");
        return;
    }
    int target=(tracker.sample_index+direction+tracker.sample_count)%tracker.sample_count;
    stop_playback(NULL,NULL);
    TtEditEvent event={.type=TT_EDIT_SAMPLE_SELECT};
    event.sample.index=target;
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    tracker.zoom=tracker.view_start=tracker.marker=0;
    center_on_marker();
    tt_tracker_message("Selected sample");
}
static void previous_sample(F80Widget *button,void *user) {
    (void)button;(void)user;select_sample(-1);
}
static void next_sample(F80Widget *button,void *user) {
    (void)button;(void)user;select_sample(1);
}
static int save_song_at(const char *path) {
    TtSongSample meta[TT_HISTORY_MAX]={0};
    TtEditEvent sync={.type=TT_EDIT_PATTERN_SYNC};
    if(!emit_edit(&sync)) { tt_tracker_message(edit_message);return 0; }
    for(int i=0;i<tracker.sample_count;++i) {
        const TtWave *wave=i==tracker.sample_index?&tracker.wave:&tracker.samples[i].wave;
        const TtPhrase *phrase=i==tracker.sample_index?&tracker.phrase:&tracker.samples[i].phrase;
        const char *loaded=i==tracker.sample_index?tracker.loaded_path:tracker.samples[i].loaded_path;
        TtSongSample *entry=&meta[i];
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
    if(!tt_song_save(path,&tracker.song,meta,tracker.sample_count,error,sizeof(error))) {
        tt_tracker_message(error);return 0;
    }
    snprintf(tracker.song_path,sizeof(tracker.song_path),"%s",path);
    tracker.document_dirty=0;
    tt_tracker_message("Song saved with patterns, order, sample paths and slices");
    return 1;
}
static int load_song_at(const char *path) {
    TtSong *next=malloc(sizeof(*next));
    if(!next) { tt_tracker_message("Out of memory loading song");return 0; }
    TtSongSample meta[TT_HISTORY_MAX];
    TtStored loaded[TT_HISTORY_MAX]={0};
    int count=0,missing=0;char error[192];
    if(!tt_song_load(path,next,meta,&count,error,sizeof(error))) {
        free(next);tt_tracker_message(error);return 0;
    }
    for(int i=0;i<count;++i) {
        char resolved[512];
        if(!tt_song_resolve_path(path,meta[i].path,resolved,sizeof(resolved))) {
            snprintf(error,sizeof(error),"Sample path too long");goto failed;
        }
        if(!strcmp(resolved,"builtin:demo")) {
            if(!tt_make_demo(&loaded[i].wave)) {
                snprintf(error,sizeof(error),"Could not recreate demo sample");goto failed;
            }
        } else {
            if(!tt_load_wav(resolved,&loaded[i].wave,error,sizeof(error))) {
#ifdef __EMSCRIPTEN__
                const char *basename=strrchr(resolved,'/');
                if(basename && basename[1]) {
                    char fallback[512];snprintf(fallback,sizeof(fallback),"/%s",basename+1);
                    if(tt_load_wav(fallback,&loaded[i].wave,error,sizeof(error)))
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
    TtEditEvent event={.type=TT_EDIT_DOCUMENT_REPLACE};
    event.replacement=(TtEditDocumentReplace){next,loaded,count};
    if(!emit_edit(&event)) { snprintf(error,sizeof(error),"%s",edit_message);goto failed; }
    tracker.edit_order=order_for_pattern(0,0);
    tracker.phrase_scroll=tracker.edit_order>=0?
        tracker.edit_order*tracker.song.pattern_rows:0;
    tracker.reveal_cursor=1;
    send_play(TT_PLAY_DOCUMENT_RESET,0,0);
    tracker.zoom=tracker.view_start=tracker.marker=tracker.step=0;
    snprintf(tracker.song_path,sizeof(tracker.song_path),"%s",path);
    tracker.document_dirty=0;
    free(next);
    if(missing) {
        char info[192];
        snprintf(info,sizeof(info),"Song loaded; %d WAV file%s missing",missing,missing==1?"":"s");
        tt_tracker_message(info);
    } else tt_tracker_message("Song loaded");
    return 1;
failed:
    for(int i=0;i<count;++i)tt_free(&loaded[i].wave);
    free(next);tt_tracker_message(error);return 0;
}
static int recover_missing(const char *name) {
    int recovered=0;
    for(int i=0;i<tracker.sample_count;++i) {
        TtWave *wave=i==tracker.sample_index?&tracker.wave:&tracker.samples[i].wave;
        const char *path=i==tracker.sample_index?tracker.loaded_path:tracker.samples[i].loaded_path;
        if(wave->pcm || (name && strcmp(wave->source_name,name)))continue;
        char candidate[512];
        snprintf(candidate,sizeof(candidate),"%s",path);
        TtWave next={0};char error[160];
        if(!tt_load_wav(candidate,&next,error,sizeof(error))) {
            const char *base=strrchr(candidate,'/');
            if(!base || !base[1])continue;
            snprintf(candidate,sizeof(candidate),"/%s",base+1);
            if(!tt_load_wav(candidate,&next,error,sizeof(error)))continue;
        }
        if(next.frames!=wave->frames || next.rate!=wave->rate) {
            tt_free(&next);continue;
        }
        TtEditEvent event={.type=TT_EDIT_SAMPLE_RECOVER};
        event.loaded_wave=(TtEditLoadedWave){i,&next,candidate};
        if(!emit_edit(&event)) { tt_free(&next);continue; }
        ++recovered;
    }
    if(recovered)tt_tracker_message("Missing WAV restored");
    return recovered;
}
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE int tt_tracker_recover_wav(const char *name) {
    return recover_missing(name);
}
EMSCRIPTEN_KEEPALIVE int tt_tracker_load_wav(const char *original_name) {
    return load_path("/sample-tracker-upload.wav",original_name);
}
EMSCRIPTEN_KEEPALIVE int tt_tracker_load_song(void) {
    return load_song_at("/sample-tracker-import.song");
}
EMSCRIPTEN_KEEPALIVE int tt_tracker_save_song(void) {
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
    else tt_tracker_save_song();
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
        tt_tracker_message("Could not open file dialog");
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
    (void)button;(void)user;
    handle_ui_event((TtUiEvent){.type=TT_UI_EXIT_REQUEST});
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
    tt_tracker_message("Previous marker selected");
}
static void next_marker(F80Widget *button,void *user) {
    (void)button;(void)user;
    focus_marker(tracker.marker+1);
    tt_tracker_message("Next marker selected");
}
static void set_effect_slot(TtCell *cell,int slot,int effect) {
    TtPatternEffect *fx=tt_cell_effect(cell,slot);
    if(!fx)return;
    if(effect==TT_EFFECT_NONE) {
        fx->command=effect;fx->value=-1;
    } else if(fx->command!=effect) {
        fx->command=effect;fx->value=-1;
    }
}
static int pattern_effect_slot(void) {
    return tracker.field>=2?(tracker.field-2)/2:0;
}
static int pattern_effect_field(void) {
    return tracker.field>=2 && tracker.field%2==0;
}
static void adjust_value(int delta) {
    if(tracker.focus==FOCUS_WAVE) {
        int cells=tracker.wave_canvas->rect.w*2;
        if(cells<1)cells=1;
        int distance=visible_frames()/cells;
        if(distance<1)distance=1;
        edit_marker(tracker.marker,tracker.phrase.marker[tracker.marker]+delta*distance);
        center_on_marker();
        tt_tracker_message("Marker moved; hold on the waveform to select and drag one");
    } else if(tracker.focus==FOCUS_REFERENCE) {
        TtEditEvent event={.type=TT_EDIT_SAMPLE_REFERENCE};
        event.sample.index=tracker.sample_index;
        event.sample.value=clamp(tracker.phrase.reference_note+delta,12,127);
        emit_edit(&event);
        tt_tracker_message("Reference note sets the sample's original pitch");
    } else {
        TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel],*cell=&edited;
        if(tracker.field==0)
            cell->note=clamp((cell->note<0?tracker.phrase.reference_note:cell->note)+delta,
                             12,127);
        else if(tracker.field==1)cell->instrument=tracker.sample_index+1;
        else if(pattern_effect_field()) {
            int slot=pattern_effect_slot();
            TtPatternEffect *fx=tt_cell_effect(cell,slot);
            const char *effects="GIJKMNOPRSTVWXYZ";
            const char *current=fx->command?strchr(effects,fx->command):NULL;
            int index=current?(int)(current-effects):-1;
            int next=clamp(index+(delta>0?1:-1),-1,(int)strlen(effects)-1);
            set_effect_slot(cell,slot,next<0?TT_EFFECT_NONE:effects[next]);
        } else {
            TtPatternEffect *fx=tt_cell_effect(cell,pattern_effect_slot());
            if(fx->command==TT_EFFECT_NONE)set_effect_slot(cell,pattern_effect_slot(),TT_EFFECT_S);
            fx=tt_cell_effect(cell,pattern_effect_slot());
            fx->value=clamp(fx->value+delta,0,255);
        }
        edit_cell(tracker.step,tracker.channel,edited);
        tt_tracker_message("Pattern cell changed");
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
    (void)button;(void)user;
    handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_PHRASE});
}
static void focus_wave(F80Widget *button,void *user) {
    (void)button;(void)user;
    handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_WAVE});
}
static void focus_order(F80Widget *button,void *user) {
    (void)button;(void)user;
    handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_ORDER});
}
static void toggle_loop(F80Widget *button,void *user) {
    (void)button;(void)user;send_play(TT_PLAY_TOGGLE_LOOP,0,0);
}
static void toggle_follow(F80Widget *button,void *user) {
    (void)button;(void)user;send_play(TT_PLAY_TOGGLE_FOLLOW,0,0);
}
static void toggle_mode(F80Widget *button,void *user) {
    (void)button;(void)user;
    int mode=tracker.play_mode==PLAY_SONG?PLAY_PATTERN:PLAY_SONG;
    if(handle_ui_event((TtUiEvent){.type=TT_UI_MODE_CONTEXT,.a=mode}))
        send_play(TT_PLAY_SET_MODE,mode,0);
}
static void toggle_pattern_lock(F80Widget *button,void *user) {
    (void)button;(void)user;
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_LOCK,
        .a=!tracker.pattern_lock});
}
static void tt_extrema(int first,int last,float *low,float *high) {
    *low=1;*high=-1;
    if(!tracker.wave.pcm)return;
    if(first<0)first=0;
    if(last>tracker.wave.frames)last=tracker.wave.frames;
    if(last<=first)last=first+1;
    if(last-first>=128 && tracker.wave.peaks) {
        int begin=first/64,finish=(last-1)/64;
        for(int block=begin;block<=finish && block<tracker.wave.peak_count;++block) {
            TtPeak peak=tracker.wave.peaks[block];
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
    for(int marker=0;marker<TT_MARKERS;++marker) {
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
        tt_extrema(first,last,&low,&high);
        if(low<-1)low=-1;if(high>1)high=1;
        int top=(int)((1-high)*(vertical-1)/2);
        int bottom=(int)((1-low)*(vertical-1)/2);
        f80_raster_line(&raster,x,top,x,bottom,10,2);
    }
    for(int marker=0;marker<TT_MARKERS;++marker) {
        int x=wave_x_from_frame(tracker.phrase.marker[marker],pixels);
        if(x<0)continue;
        f80_raster_line(&raster,x,0,x,vertical-1,
                        marker==tracker.marker?4:8,marker==tracker.marker?4:3);
    }
    for(int ch=0;ch<TT_CHANNELS;++ch) {
        if(tt_audio_voice_pcm(ch)!=tracker.wave.pcm)continue;
        int x=wave_x_from_frame((int)tt_audio_position(ch),pixels);
        if(x<0)continue;
        int dir=tt_audio_increment(ch)>0;
        f80_raster_line(&raster,x,0,x,vertical-1,8,3);
        if(vertical>=8)
            f80_raster_line(&raster,x,dir?0:vertical-3,x,dir?2:vertical-1,15,2);
    }
    f80_raster_draw_quadrants(&raster);
    f80_raster_free(&raster);
    if(!compact)for(int marker=0;marker<TT_MARKERS;++marker) {
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
static int pattern_effect_slots_visible(int channel) {
    int slots=tracker.effect_slots[channel]>1?tracker.effect_slots[channel]:1;
    int displayed=tracker.edit_order>=0?tracker.song.order_count:1;
    for(int item=0;item<displayed;++item) {
        int pattern=tracker.edit_order>=0?tracker.song.order[item]:tracker.pattern_index;
        if(pattern<0 || pattern>=tracker.song.pattern_count)continue;
        const TtPattern *source=pattern==tracker.pattern_index?
            &tracker.pattern:&tracker.song.pattern[pattern];
        for(int row=0;row<tracker.song.pattern_rows;++row) {
            const TtCell *cell=&source->cell[row][channel];
            if(cell->effect3!=TT_EFFECT_NONE)slots=3;
            else if(cell->effect2!=TT_EFFECT_NONE && slots<2)slots=2;
        }
    }
    if(channel==tracker.channel && tracker.field>=2) {
        int cursor_slots=(tracker.field-2)/2+1;
        if(cursor_slots>slots)slots=cursor_slots;
    }
    return slots;
}
static int pattern_fields_per_channel(int channel) {
    return 2+2*pattern_effect_slots_visible(channel);
}
static int pattern_edit_fields_per_channel(int channel) {
    int count=pattern_fields_per_channel(channel);
    return count>8?8:count;
}
static int pattern_field_width(int field) {
    if(field==0)return 4;
    if(field==1 || field%2==0)return 3;
    return 2;
}
static int inline_table_column_width(int column) {
    return column==0?3:column==1?2:5;
}
static int inline_table_row_width(void) {
    int width=7;
    for(int column=0;column<8;++column)
        width+=inline_table_column_width(column);
    return width;
}
static int pattern_field_offset(int field) {
    int offset=0;
    for(int at=0;at<field;++at)offset+=pattern_field_width(at);
    return offset;
}
static int pattern_field_count(void) {
    int total=0;
    for(int channel=0;channel<TT_CHANNELS;++channel)
        total+=pattern_fields_per_channel(channel);
    return total;
}
static int pattern_channel_start(int channel) {
    int start=0;
    for(int at=0;at<channel;++at)start+=pattern_fields_per_channel(at);
    return start;
}
static void pattern_field_at(int index,int *channel,int *field) {
    int at=0;
    for(int ch=0;ch<TT_CHANNELS;++ch) {
        int count=pattern_fields_per_channel(ch);
        if(index<at+count) { *channel=ch;*field=index-at;return; }
        at+=count;
    }
    *channel=TT_CHANNELS-1;*field=pattern_fields_per_channel(*channel)-1;
}
static int pattern_field_span(int first,int last) {
    int width=0;
    for(int index=first;index<=last;++index) {
        int channel,field;pattern_field_at(index,&channel,&field);
        if(index>first && field==0)++width;
        width+=pattern_field_width(field);
    }
    return width;
}
static int pattern_max_field_scroll(int available) {
    int count=pattern_field_count();
    int first=count-1;
    while(first>0 && pattern_field_span(first-1,pattern_field_count()-1)<=available)
        --first;
    return first;
}
static void ensure_pattern_visible(F80Rect r) {
    int body=r.h-3;
    if(body<1)body=1;
    if(tracker.play_mode==PLAY_SONG)
        handle_ui_event((TtUiEvent){.type=TT_UI_SYNC_PATTERN_ORDER});
    int maximum=grid_total_rows()-body;
    if(maximum<0)maximum=0;
    if(tracker.playing && tracker.follow &&
       tracker.play_mode==PLAY_SONG &&
       tracker.active_order_index>=0 && tracker.edit_order>=0) {
        int playhead=tracker.active_order_index*tracker.song.pattern_rows+tracker.active_step;
        tracker.phrase_scroll=playhead-body/2;
    } else if(tracker.playing && tracker.follow &&
       tracker.play_mode==PLAY_PATTERN &&
       tracker.active_pattern_index==tracker.pattern_index &&
       tracker.active_step>=0) {
        tracker.phrase_scroll=tracker.active_step-body/2;
    } else if(tracker.reveal_cursor) {
        int edit=tracker.inline_table_selected?
            tracker.inline_table_parent+1+tracker.inline_table_tick:grid_edit_row();
        if(edit<tracker.phrase_scroll)tracker.phrase_scroll=edit;
        if(edit>=tracker.phrase_scroll+body)
            tracker.phrase_scroll=edit-body+1;
        tracker.reveal_cursor=0;
    }
    tracker.phrase_scroll=clamp(tracker.phrase_scroll,0,maximum);
    int available=r.w-6;
    if(available<1)available=1;
    int selected=pattern_channel_start(tracker.channel)+tracker.field;
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
    int start[TT_CHANNELS],end[TT_CHANNELS];
    for(int channel=0;channel<TT_CHANNELS;++channel)
        start[channel]=end[channel]=-1;
    int field_count=pattern_field_count();
    for(int index=tracker.field_scroll;index<field_count && x<right;++index) {
        int channel,field;pattern_field_at(index,&channel,&field);
        if(index>tracker.field_scroll && field==0) {
            for(int y=r.y;y<r.y+r.h;++y) {
                int line=y-r.y-3;
                if(line>=0 && line<r.h-3 &&
                   grid_is_inline_table_row(tracker.phrase_scroll+line,NULL))continue;
                f80_put(y,x,"│",1,10,0);
            }
            if(++x>=right)break;
        }
        int width=pattern_field_width(field),visible=width;
        if(visible>right-x)visible=right-x;
        if(start[channel]<0)start[channel]=x;
        end[channel]=x+visible;
        float level=tt_audio_level(channel);
        if(level>1)level=1;
        int pixels=(int)(level*24+.5f);
        for(int column=0;column<visible;++column) {
            int pixel=(pattern_field_offset(field)+column)*2;
            f80_put(r.y+1,x+column,pixels>pixel+1?"█":pixels>pixel?"▌":"▒",
                    1,pixels>pixel?8:10,1);
        }
        for(int line=0;line<r.h-3;++line) {
            int global=tracker.phrase_scroll+line,table_tick=-1;
            if(global>=grid_total_rows())break;
            int inline_line=grid_is_inline_table_row(global,&table_tick);
            int base_global=grid_base_row(global);
            int order=grid_order_for_row(global),row=base_global%tracker.song.pattern_rows;
            int pattern_index=grid_pattern_for_row(global);
            int editable=order<0 || order==tracker.edit_order;
            if(inline_line) {
                if(index==tracker.field_scroll) {
                    const TtTableTick *tick=
                        &tracker.song.table[tracker.inline_table_index].tick[table_tick];
                    TableVoice *voice=&tracker.table_voice[tracker.inline_table_channel];
                    int playhead=voice->active && voice->table==tracker.inline_table_index &&
                        table_tick==(voice->tick>0?voice->tick-1:0);
                    int table_x=r.x+5;
                    for(int column=0;column<8;++column) {
                        int width=inline_table_column_width(column);
                        int visible=width;
                        if(table_x>=right)break;
                        if(visible>right-table_x)visible=right-table_x;
                        char shown[6]=".....";
                        int empty=0;
                        if(column==0) {
                            if(tracker.inline_table_selected &&
                               table_tick==tracker.inline_table_tick &&
                               tracker.inline_table_field==0 && tracker.note_input_length) {
                                memcpy(shown,tracker.note_input,
                                       (size_t)tracker.note_input_length);
                                shown[tracker.note_input_length]=0;
                            } else if(tick->note==TT_NOTE_OFF)memcpy(shown,"OFF",4);
                            else if(tick->note>=0)tt_note_name(tick->note,shown);
                            else { memcpy(shown,"...",4);empty=1; }
                        } else if(column==1) {
                            if(tick->instrument>=0)
                                snprintf(shown,sizeof(shown),"%02X",tick->instrument);
                            else { memcpy(shown,"..",3);empty=1; }
                        } else {
                            int fx=column-2;
                            char pair[6];
                            TtTableEffect effect=tick->effect[fx];
                            tt_pattern_effect_text(effect.command,effect.value,pair);
                            memcpy(shown,pair,5);
                            empty=effect.command==0;
                        }
                        int selected=tracker.inline_table_selected &&
                            table_tick==tracker.inline_table_tick &&
                            (column==0?tracker.inline_table_field==0:
                             column==1?tracker.inline_table_field==1:
                             tracker.inline_table_field==2+(column-2)*2 ||
                             tracker.inline_table_field==3+(column-2)*2);
                        int pair=!editable?66:playhead?4:empty?1:
                            column==0?4:column==1?8:9;
                        if(selected)pair=3;
                        f80_put(r.y+3+line,table_x,shown,visible,pair,
                                editable && playhead);
                        table_x+=width+(column<7?1:0);
                    }
                }
                continue;
            }
            const TtPattern *pattern=pattern_index==tracker.pattern_index?
                &tracker.pattern:&tracker.song.pattern[pattern_index];
            const TtCell *cell=&pattern->cell[row][channel];
            char shown[6]=".....";
            int empty=0;
            if(inline_line && channel!=tracker.inline_table_channel) {
                memset(shown,' ',sizeof(shown));shown[visible]=0;empty=1;
            } else if(inline_line && channel==tracker.inline_table_channel) {
                const TtTableTick *tick=&tracker.song.table[tracker.inline_table_index].tick[table_tick];
                if(field==0) {
                    if(tick->note==TT_NOTE_OFF)memcpy(shown,"OFF",4);
                    else if(tick->note>=0)tt_note_name(tick->note,shown);
                    else memcpy(shown,"...",4);
                    empty=tick->note==-1;
                } else if(field==1) {
                    if(tick->instrument>=0)snprintf(shown,sizeof(shown),"%02X",tick->instrument);
                    else memcpy(shown,"..",3);
                    empty=tick->instrument<0;
                } else if(field%2==0) {
                    int fx=(field-2)/2;
                    if(fx<TT_TABLE_EFFECTS && tick->effect[fx].command) {
                        shown[0]=tick->effect[fx].command;shown[1]=shown[2]=' ';
                        shown[3]=0;
                    } else { memcpy(shown,".  ",4);empty=1; }
                } else {
                    int fx=(field-3)/2;
                    if(fx<TT_TABLE_EFFECTS && tick->effect[fx].value>=0)
                        snprintf(shown,sizeof(shown),"%02X",tick->effect[fx].value);
                    else { memcpy(shown,"..",3);empty=1; }
                }
            } else if(field>7) {
                memset(shown,' ',sizeof(shown));shown[visible]=0;empty=1;
            } else if(field==0) {
                if(cell->note==TT_NOTE_OFF)memcpy(shown,"OFF",4);
                else if(cell->note>=0)tt_note_name(cell->note,shown);
                else memcpy(shown,"...",4);
                empty=cell->note==-1;
            } else if(field==1) {
                if(cell->instrument>=0)snprintf(shown,sizeof(shown),"%02X",cell->instrument);
                else memcpy(shown,"..",3);
                empty=cell->instrument<0;
            } else {
                int slot=(field-2)/2;
                const TtPatternEffect *fx=tt_cell_effect_const(cell,slot);
                char pair[6];tt_pattern_effect_text(fx->command,fx->value,pair);
                if(field%2==0)memcpy(shown,pair,4);
                else memcpy(shown,pair+3,3);
                empty=fx->command==TT_EFFECT_NONE;
            }
            int editrow=!inline_line && editable && tracker.focus==FOCUS_PHRASE && row==tracker.step;
            int editcol=tracker.focus==FOCUS_PHRASE && channel==tracker.channel;
            if(editrow && editcol && field==0 && tracker.note_input_length)
                memcpy(shown,tracker.note_input,(size_t)tracker.note_input_length);
            int selected=editrow && editcol && field==tracker.field;
            int table_selected=inline_line && tracker.inline_table_selected &&
                channel==tracker.inline_table_channel && table_tick==tracker.inline_table_tick &&
                field==tracker.inline_table_field;
            int playhead=row==tracker.active_step &&
                (order>=0?order==tracker.active_order_index:
                 pattern_index==tracker.active_pattern_index);
            if(inline_line) {
                TableVoice *voice=&tracker.table_voice[tracker.inline_table_channel];
                playhead=voice->active && voice->table==tracker.inline_table_index &&
                    table_tick==(voice->tick>0?voice->tick-1:0);
            }
            int pair=!editable?66:editrow && editcol?4:editrow?2:
                     playhead?4:empty?1:
                     field==0?4:field==1?8:9;
            if(table_selected)pair=3;
            else if(!inline_line && selected_cell(base_global,channel))
                pair=tracker.grid_selection_invalid?15:65;
            if(!inline_line && tracker.inline_table_open &&
               base_global==tracker.inline_table_parent && channel==tracker.inline_table_channel &&
               tracker.field>=2 && channel==tracker.channel &&
               (field==(tracker.field&~1) || field==(tracker.field|1)))pair=59;
            f80_put(r.y+3+line,x,shown,visible,pair,
                    editable && (editrow || playhead));
            if(selected && !tracker.selecting) {
                int highlight=field==0?3:field==1?2:width;
                if(highlight>visible)highlight=visible;
                f80_put(r.y+3+line,x,shown,highlight,3,1);
            }
        }
        x+=width;
    }
    for(int channel=0;channel<TT_CHANNELS;++channel)if(start[channel]>=0) {
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
        int table_tick=-1;
        if(grid_is_inline_table_row(global,&table_tick)) {
            char gutter[8];
            if(table_tick==0)snprintf(gutter,sizeof(gutter),"T%02X",tracker.inline_table_index);
            else snprintf(gutter,sizeof(gutter)," %02X",table_tick);
            TableVoice *voice=&tracker.table_voice[tracker.inline_table_channel];
            int play=voice->active && voice->table==tracker.inline_table_index &&
                table_tick==(voice->tick>0?voice->tick-1:0);
            int y=r.y+3+line;
            f80_put(y,r.x,gutter,4,play?4:8,1);
            continue;
        }
        int order=grid_order_for_row(global),row=global%tracker.song.pattern_rows;
        int pattern_index=grid_pattern_for_row(global);
        int editable=order<0 || order==tracker.edit_order;
        int playhead=row==tracker.active_step &&
            (order>=0?order==tracker.active_order_index:
             pattern_index==tracker.active_pattern_index);
        char gutter[8];
        if(row==0 && tracker.play_mode==PLAY_PATTERN)
            snprintf(gutter,sizeof(gutter),"P%02X ",pattern_index);
        else if(row==0 && order>=0)snprintf(gutter,sizeof(gutter),"O%02X ",order);
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
    if(pattern_field_span(tracker.field_scroll,pattern_field_count()-1)>r.w-6)
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
static void manage_refresh(void) {
    if(!tracker.manage_dialog)return;
    for(int i=0;i<tracker.sample_count;++i) {
        const TtWave *wave=i==tracker.sample_index?&tracker.wave:&tracker.samples[i].wave;
        char note[4];const TtPhrase *phrase=i==tracker.sample_index?
            &tracker.phrase:&tracker.samples[i].phrase;
        tt_note_name(phrase->reference_note,note);
        snprintf(tracker.manage_rows[i],sizeof(tracker.manage_rows[i]),
                 "%02X  %-36.36s  Ref %s",i+1,wave->name,note);
        tracker.manage_items[i]=tracker.manage_rows[i];
    }
    tracker.manage_list->item_count=tracker.sample_count;
    int selected=clamp(tracker.manage_list->cursor,0,tracker.sample_count-1);
    const TtWave *wave=selected==tracker.sample_index?&tracker.wave:
        &tracker.samples[selected].wave;
    const TtPhrase *phrase=selected==tracker.sample_index?&tracker.phrase:
        &tracker.samples[selected].phrase;
    char note[4];tt_note_name(phrase->reference_note,note);
    snprintf(tracker.manage_details,sizeof(tracker.manage_details),
             "Source: %.50s | %d frames | %d Hz | Base %s",
             wave->source_name,wave->frames,wave->rate,note);
}
static void manage_selected(F80Widget *list,void *user) {
    (void)user;
    int index=clamp(list->cursor,0,tracker.sample_count-1);
    const TtWave *wave=index==tracker.sample_index?&tracker.wave:
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
    TtEditEvent event={.type=TT_EDIT_SAMPLE_SWAP};
    event.sample.index=a;event.sample.other=b;
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    tracker.zoom=tracker.view_start=tracker.marker=0;
    tracker.manage_list->cursor=b;manage_selected(tracker.manage_list,NULL);
    tt_tracker_message("Samples reordered; pattern instrument numbers updated");
}
static void manage_delete(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.sample_count<=1) { tt_tracker_message("Keep at least one sample");return; }
    if(!tracker.delete_armed) { tracker.delete_armed=1;tt_tracker_message("Press Delete again to remove this sample");return; }
    int removed=tracker.manage_list->cursor;
    stop_playback(NULL,NULL);
    TtEditEvent event={.type=TT_EDIT_SAMPLE_DELETE};
    event.sample.index=removed;
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    tracker.zoom=tracker.view_start=tracker.marker=0;
    tracker.manage_list->cursor=clamp(removed,0,tracker.sample_count-1);
    manage_selected(tracker.manage_list,NULL);
    tt_tracker_message("Sample deleted; pattern references updated");
}
static void manage_rename(F80Widget *button,void *user) {
    (void)button;(void)user;
    tracker.delete_armed=0;
    if(!tracker.manage_name->input[0]) { tt_tracker_message("Enter a sample name");return; }
    int index=tracker.manage_list->cursor;
    TtEditEvent event={.type=TT_EDIT_SAMPLE_RENAME};
    event.sample.index=index;
    snprintf(event.sample.name,sizeof(event.sample.name),"%s",tracker.manage_name->input);
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    manage_refresh();tt_tracker_message("Sample renamed; original WAV name retained for CSV");
}
static void manage_ref(F80Widget *button,void *user) {
    (void)button;int delta=(int)(long)user,index=tracker.manage_list->cursor;
    tracker.delete_armed=0;
    TtPhrase *phrase=index==tracker.sample_index?&tracker.phrase:
        &tracker.samples[index].phrase;
    TtEditEvent event={.type=TT_EDIT_SAMPLE_REFERENCE};
    event.sample.index=index;
    event.sample.value=clamp(phrase->reference_note+delta,0,127);
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    manage_refresh();tt_tracker_message("Sample base changed; slice bases transposed");
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
        tt_note_name(note,candidate);
        if(!strcmp(candidate,text))return note;
    }
    return -1;
}
static void slices_refresh(void) {
    if(!tracker.slices_dialog)return;
    for(int i=0;i<TT_SLICES;++i) {
        char note[4];tt_note_name(tt_slice_base_note(&tracker.phrase,i),note);
        snprintf(tracker.slice_rows[i],sizeof(tracker.slice_rows[i]),
                 "%X-%X  %8.3f - %8.3fs  %s%s",i,i+1,
                 (double)tracker.phrase.marker[i]/tracker.wave.rate,
                 (double)tracker.phrase.marker[i+1]/tracker.wave.rate,note,
                 tracker.phrase.slice_note_offset[i]==TT_SLICE_INHERIT?" (sample)":" (override)");
        tracker.slice_items[i]=tracker.slice_rows[i];
    }
    int i=clamp(tracker.slices_list->cursor,0,TT_SLICES-1);
    snprintf(tracker.slice_details,sizeof(tracker.slice_details),
             "Slice %X: frames %d-%d, duration %.3fs; source %.55s",i,
             tracker.phrase.marker[i],tracker.phrase.marker[i+1],
             (double)(tracker.phrase.marker[i+1]-tracker.phrase.marker[i])/tracker.wave.rate,
             tracker.wave.source_name);
}
static void slices_selected(F80Widget *list,void *user) {
    (void)user;char note[4];
    tt_note_name(tt_slice_base_note(&tracker.phrase,list->cursor),note);
    snprintf(tracker.slices_note->input,sizeof(tracker.slices_note->input),"%s",note);
    tracker.slices_note->input_pos=(int)strlen(tracker.slices_note->input);
    slices_refresh();
}
static void slices_apply(F80Widget *button,void *user) {
    (void)button;(void)user;
    int note=parse_note(tracker.slices_note->input);
    if(note<0) { tt_tracker_message("Enter a note like C-4 or F#3");return; }
    edit_slice(tracker.slices_list->cursor,note);
    slices_refresh();tt_tracker_message("Slice base note set");
}
static void slices_inherit(F80Widget *button,void *user) {
    (void)button;(void)user;
    edit_slice(tracker.slices_list->cursor,-1);
    slices_selected(tracker.slices_list,NULL);tt_tracker_message("Slice inherits sample base note");
}
static void slices_step(F80Widget *button,void *user) {
    (void)button;
    int index=tracker.slices_list->cursor;
    edit_slice(index,clamp(tt_slice_base_note(&tracker.phrase,index)+(int)(long)user,0,127));
    slices_selected(tracker.slices_list,NULL);tt_tracker_message("Slice base note changed");
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
    tracker.slices_list=f80_add(dialog,f80_listbox(tracker.slice_items,TT_SLICES,
                                                   slices_selected,NULL));
    tracker.slices_list->window_role=F80_WINDOW_CYAN;
    tracker.slices_list->cursor=clamp(tracker.marker,0,TT_SLICES-1);
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
    snprintf(tracker.order_heading,sizeof(tracker.order_heading),"PLAY ORDER");
    for(int i=0;i<tracker.song.pattern_count;++i) {
        snprintf(tracker.bank_rows[i],sizeof(tracker.bank_rows[i]),
                 "%02Xp: %.12s",i,tracker.song.name[i]);
        tracker.bank_items[i]=tracker.bank_rows[i];
    }
    for(int i=0;i<tracker.song.order_count;++i) {
        int pattern=tracker.song.order[i];
        int name_width=tracker.order_dialog && tracker.order_dialog->rect.w>=70?12:9;
        snprintf(tracker.order_rows[i],sizeof(tracker.order_rows[i]),
                 "[%c] %02Xo>%02Xp:%.12s",
                 tracker.song.order_active[i]?'x':' ',i,pattern,
                 tracker.song.name[pattern]);
        if(name_width<12)tracker.order_rows[i][16+name_width]='\0';
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
static void order_new_pattern(F80Widget *button,void *user) {
    (void)button;(void)user;
    TtEditEvent event={.type=TT_EDIT_PATTERN_NEW};
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    int index=tracker.song.pattern_count-1;
    select_edit_pattern(index);
    tracker.order_bank->cursor=index;
    order_refresh();order_bank_selected(tracker.order_bank,NULL);
    tt_tracker_message("New pattern selected for editing; add it to the play order");
}
static void order_pattern_move(F80Widget *button,void *user) {
    (void)button;(void)user;
    tt_tracker_message("Pattern reordering is not implemented yet");
}
static void order_name_changed(void) {
    if(!tracker.order_name || !tracker.order_dialog)return;
    TtEditEvent event={.type=TT_EDIT_PATTERN_RENAME};
    event.pattern.index=tracker.order_bank->cursor;
    snprintf(event.pattern.name,sizeof(event.pattern.name),"%s",tracker.order_name->input);
    if(emit_edit(&event))order_refresh();
}
static void order_add(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.song.order_count>=TT_SONG_ORDER) { tt_tracker_message("Order is full");return; }
    stop_playback(NULL,NULL);
    int at=clamp(tracker.order_seq->cursor,0,tracker.song.order_count-1);
    TtEditEvent event={.type=TT_EDIT_ORDER_PATTERN};
    event.order.position=at;event.order.pattern=tracker.order_bank->cursor;
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    tracker.order_seq->cursor=at;
    order_refresh();tt_tracker_message("Selected order now references this pattern");
}
static void order_append(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.song.order_count>=TT_SONG_ORDER) { tt_tracker_message("Order is full");return; }
    int at=tracker.song.order_count;
    TtEditEvent event={.type=TT_EDIT_ORDER_APPEND};
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    tracker.order_seq->cursor=at;order_refresh();
    tt_tracker_message("Order appended");
}
static void order_remove(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.song.order_count<=1) { tt_tracker_message("Keep at least one order entry");return; }
    stop_playback(NULL,NULL);
    TtEditEvent event={.type=TT_EDIT_ORDER_REMOVE};
    event.order.position=tracker.order_seq->cursor;
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    order_refresh();tt_tracker_message("Order entry removed");
}
static void order_move(F80Widget *button,void *user) {
    (void)button;int a=tracker.order_seq->cursor,b=a+(int)(long)user;
    if(b<0 || b>=tracker.song.order_count)return;
    stop_playback(NULL,NULL);
    TtEditEvent event={.type=TT_EDIT_ORDER_SWAP};
    event.order.position=a;event.order.other=b;
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    tracker.order_seq->cursor=b;
    order_refresh();tt_tracker_message("Order entry moved");
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
    int bpm,rows,beat_rows,ticks,table_length;
    if(!setting_number(tracker.settings_input[0],30,300,&bpm) ||
       !setting_number(tracker.settings_input[1],16,TT_MAX_STEPS,&rows) ||
       rows%16 || !setting_number(tracker.settings_input[2],1,16,&beat_rows) ||
       !setting_number(tracker.settings_input[3],1,TT_MAX_TICKS_PER_ROW,&ticks) ||
       !setting_number(tracker.settings_input[4],TT_TABLE_MIN_TICKS,
                       TT_TABLE_MAX_TICKS,&table_length)) {
        tt_tracker_message("Use BPM 30-300, pattern 16-256 by 16, beat 1-16, ticks 1-48, table 1-256");
        return;
    }
    stop_playback(NULL,NULL);
    TtEditEvent event={.type=TT_EDIT_SETTINGS};
    event.settings=(TtEditSettings){bpm,rows,beat_rows,ticks,table_length};
    if(!emit_edit(&event)) { tt_tracker_message(edit_message);return; }
    tracker.step=clamp(tracker.step,0,rows-1);
    tracker.anchor_step=clamp(tracker.anchor_step,0,rows-1);
    tracker.selecting=0;
    tracker.reveal_cursor=1;
    close_settings(NULL,NULL);
    tt_tracker_message("Song settings updated");
}
static void show_settings(F80Widget *button,void *user) {
    (void)button;(void)user;
    static const char *labels[]={"BPM (30-300)","Pattern rows (16-256)",
                                 "Rows per beat (1-16)","Ticks per row (1-48)",
                                 "Table ticks (1-256)"};
    int values[]={tracker.song.bpm,tracker.song.pattern_rows,
                  tracker.song.rows_per_beat,tracker.song.ticks_per_row,
                  tracker.song.table[0].length};
    F80Widget *dialog=f80_dialog("Song settings",NULL,NULL,0,0);
    tracker.settings_dialog=dialog;
    for(int i=0;i<5;++i) {
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
    order_pattern_buttons[0]=f80_add(dialog,f80_button("Up",order_pattern_move,(void *)(long)-1));
    order_pattern_buttons[1]=f80_add(dialog,f80_button("Down",order_pattern_move,(void *)(long)1));
    order_pattern_buttons[2]=f80_add(dialog,f80_button("New",order_new_pattern,NULL));
    order_pattern_buttons[3]=f80_add(dialog,f80_button("To order",order_add,NULL));
    order_play_buttons[0]=f80_add(dialog,f80_button("Up",order_move,(void *)(long)-1));
    order_play_buttons[1]=f80_add(dialog,f80_button("Down",order_move,(void *)(long)1));
    order_play_buttons[2]=f80_add(dialog,f80_button("New",order_append,NULL));
    order_play_buttons[3]=f80_add(dialog,f80_button("Delete",order_remove,NULL));
    f80_wrap_init(&order_pattern_actions,1,0);
    f80_wrap_init(&order_play_actions,1,0);
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
    const TtTableTick *entry=&tracker.song.table[tracker.table_index].tick[tick];
    if(field==0) {
        if(entry->note==TT_NOTE_OFF)snprintf(out,size,"OFF");
        else if(entry->note<0)snprintf(out,size,"...");
        else { char note[4];tt_note_name(entry->note,note);snprintf(out,size,"%s",note); }
    } else if(field==1) {
        if(entry->instrument<0)snprintf(out,size,"..");
        else snprintf(out,size,"%02X",entry->instrument);
    } else {
        const TtTableEffect *fx=&entry->effect[field-2];
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
                              0,TT_SONG_TABLES-1);
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
    TtTable *table=&tracker.song.table[tracker.table_index];
    TtEditEvent event={.type=TT_EDIT_TABLE_LENGTH};
    event.table.table=tracker.table_index;
    event.table.length=table->length+(int)(long)user*TT_TABLE_MIN_TICKS;
    emit_edit(&event);
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
        "FX: G I J K M N O P R S T V W X Y Z; values 00-FF.",
        "O in the note field enters OFF; X/Y jump pattern or row, Z sets ticks per row.",
        "Pattern/table notes: type C-4 or C#4; Space advances fields.",
        "Delete clears a field. Enter starts/stops playback. Ctrl+U/Y undo/redo.",
        "Ctrl+O toggles the event console; F9 opens menu; F10 exits.",
        "SLC04 plays 0 to 4; SFA plays F to A backward.",
        "Sampler > Manage reorders, renames, deletes and retunes samples.",
        "Sampler > Slices sets a base note for each marker-to-marker slice.",
        "Song > Settings changes BPM, pattern rows, rows per beat, and ticks.",
        "Song > Patterns and order edits the bank and playback sequence.",
        "Select TBLxx or its value, then use F7 to expand its inline table.",
        "Table ticks show note, instrument, and six FX slots."
    };
    F80Widget *dialog=f80_dialog("Sample tracker keys",lines,NULL,17,0);
    dialog->dialog_align=1;
    f80_add(dialog,f80_button("~C~lose",close_help,NULL));
    f80_show_modal(&tracker.app,dialog);
}
static int console_max_scroll(int body_rows) {
    int maximum=event_log_count-body_rows;
    return maximum>0?maximum:0;
}
static void paint_event_console(F80Widget *canvas,void *user) {
    (void)user;
    F80Rect r=canvas->rect;
    if(r.w<=0 || r.h<=0)return;
    f80_fill(r,F80_COLOR_PANEL);
    f80_put(r.y,r.x," EVENT CONSOLE  |  Ctrl+O panels  |  PgUp/PgDn scroll",
            r.w,F80_COLOR_HIGHLIGHT,1);
    if(r.h<2)return;
    int body=r.h-2;
    tracker.console_scroll=clamp(tracker.console_scroll,0,console_max_scroll(body));
    int shown=event_log_count-tracker.console_scroll;
    if(shown>body)shown=body;
    int first=event_log_count-tracker.console_scroll-shown;
    int start_y=r.y+1+body-shown;
    for(int i=0;i<shown;++i) {
        int slot=(event_log_head-event_log_count+first+i+
                  TT_EVENT_LOG_CAPACITY*2)%TT_EVENT_LOG_CAPACITY;
        f80_put(start_y+i,r.x,event_log[slot],r.w,F80_COLOR_TEXT,0);
    }
    char footer[96];
    snprintf(footer,sizeof(footer)," %d events  |  %s  |  F9 menu  F10 exit",
             event_log_count,tracker.console_scroll?"scrolling":"live");
    f80_put(r.y+r.h-1,r.x,footer,r.w,F80_COLOR_ACCENT,0);
}
static void toggle_console(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.app.menu && tracker.app.menu->menu_active)
        f80_menu_close(tracker.app.menu);
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
    tracker.console_visible=!tracker.console_visible;
    int panels_visible=!tracker.console_visible;
    tracker.root->window_role=tracker.console_visible?
        F80_WINDOW_CONSOLE:F80_WINDOW_BLUE;
    tracker.header_gap->visible=panels_visible;
    tracker.header->visible=panels_visible;
    tracker.wave_row->visible=panels_visible;
    tracker.phrase_frame->visible=panels_visible;
    tracker.details->visible=panels_visible;
    tracker.status->visible=panels_visible;
    tracker.console->visible=tracker.console_visible;
    snprintf(event_console_menu_label,sizeof(event_console_menu_label),"%s",
             tracker.console_visible?"~H~ide event console":"~S~how event console");
    if(tracker.console_visible)tracker.console_scroll=0;
    else tracker.reveal_cursor=1;
    tt_tracker_message(tracker.console_visible?"Event console shown":"Panels shown");
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
static void restore_history(int redo) {
    if(redo?!tt_edit_can_redo(edit_history):!tt_edit_can_undo(edit_history)) {
        tt_tracker_message(redo?"Nothing to redo":"Nothing to undo");return;
    }
    if(tracker.app.menu && tracker.app.menu->menu_active)
        f80_menu_close(tracker.app.menu);
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
    stop_playback(NULL,NULL);
    TtEditDoc doc=edit_doc();
    int applied=redo?tt_edit_redo(edit_history,&doc):tt_edit_undo(edit_history,&doc);
    if(!applied) { tt_tracker_message(edit_message);return; }
    tracker.document_dirty=1;
    tracker.step=clamp(tracker.step,0,tracker.song.pattern_rows-1);
    tracker.edit_order=order_for_pattern(tracker.pattern_index,tracker.edit_order);
    tracker.phrase_scroll=tracker.edit_order>=0?
        tracker.edit_order*tracker.song.pattern_rows:0;
    tracker.zoom=tracker.view_start=tracker.marker=0;
    tracker.selecting=0;tracker.reveal_cursor=1;
    if(tracker.order_list)tracker.order_list->cursor=
        clamp(tracker.order_list->cursor,0,tracker.song.order_count-1);
    tt_tracker_message(redo?"Redone":"Undone");
}
static void undo_edit(F80Widget *button,void *user) {
    (void)button;(void)user;restore_history(0);
}
static void redo_edit(F80Widget *button,void *user) {
    (void)button;(void)user;restore_history(1);
}
static const F80MenuItem file_items[]={
    {"~L~oad WAV...",NULL,open_load,1},
    {"~O~pen song...",NULL,open_song,1},
    {"~S~ave song...",NULL,save_song,1},
    {"~Q~uit",NULL,quit,1}
};
static const F80MenuItem edit_items[]={
    {"~U~ndo","Ctrl+U",undo_edit,1},
    {"~R~edo","Ctrl+Y",redo_edit,1}
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
    {"~P~atterns and order...",NULL,show_order,1}
};
static const F80MenuItem view_items[]={
    {"Zoom ~o~ut",NULL,zoom_out,1},{"Zoom ~i~n",NULL,zoom_in,1},
    {"~W~aveform",NULL,focus_wave,1},{"~O~RD list",NULL,focus_order,1},
    {"P~a~ttern",NULL,focus_phrase,1},
    {event_console_menu_label,"Ctrl+O",toggle_console,1}
};
static const F80MenuItem help_items[]={ {"~K~eys and format",NULL,show_help,1} };
static const F80Menu menus[]={
    {"~F~ile",file_items,sizeof(file_items)/sizeof(*file_items)},
    {"~E~dit",edit_items,sizeof(edit_items)/sizeof(*edit_items)},
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
static void move_order_selection(int delta) {
    if(!tracker.order_list)return;
    int next=clamp(tracker.order_list->cursor+delta,0,tracker.song.order_count-1);
    if(next==tracker.order_list->cursor)return;
    handle_ui_event((TtUiEvent){.type=TT_UI_ORDER_CURSOR,.index=next});
    order_panel_selected(tracker.order_list,NULL);
}
static void previous_order(F80Widget *button,void *user) {
    (void)button;(void)user;move_order_selection(-1);
}
static void next_order(F80Widget *button,void *user) {
    (void)button;(void)user;move_order_selection(1);
}
static void toggle_order_active(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.play_mode!=PLAY_SONG || !tracker.order_list)return;
    int at=clamp(tracker.order_list->cursor,0,tracker.song.order_count-1);
    TtEditEvent edit={.type=TT_EDIT_ORDER_ACTIVE};
    edit.order.position=at;edit.order.pattern=!tracker.song.order_active[at];
    if(!emit_edit(&edit))tt_tracker_message(edit_message);
}
static const F80StatusItem order_status[]={
    {"1","Ord-",previous_order},{"2","Ord+",next_order},
    {"3","Ply",toggle_play},{"4","Stp",stop_playback},
    {"5","Fol",toggle_follow},{"6","Pat",show_order},
    {"7","Wav",focus_wave},{"8","Act",toggle_order_active},
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
    TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel],*cell=&edited;
    if(tracker.field==0)cell->note=-1;
    else if(tracker.field==1)cell->instrument=-1;
    else {
        TtPatternEffect *fx=tt_cell_effect(cell,pattern_effect_slot());
        if(pattern_effect_field())set_effect_slot(cell,pattern_effect_slot(),TT_EFFECT_NONE);
        else fx->value=-1;
    }
    edit_cell(tracker.step,tracker.channel,edited);
}
static void tt_down(F80Widget *button,void *user) {
    (void)button;(void)user;
    TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel],*cell=&edited;
    cell->instrument=clamp((cell->instrument<1?tracker.sample_index+1:cell->instrument)-1,
                           1,tracker.sample_count);
    edit_cell(tracker.step,tracker.channel,edited);
}
static void tt_up(F80Widget *button,void *user) {
    (void)button;(void)user;
    TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel],*cell=&edited;
    cell->instrument=clamp((cell->instrument<1?tracker.sample_index+1:cell->instrument)+1,
                           1,tracker.sample_count);
    edit_cell(tracker.step,tracker.channel,edited);
}
static void use_m(F80Widget *button,void *user) {
    (void)button;(void)user;
    TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel];
    set_effect_slot(&edited,pattern_effect_slot(),TT_EFFECT_M);
    edit_cell(tracker.step,tracker.channel,edited);
}
static void use_s(F80Widget *button,void *user) {
    (void)button;(void)user;
    TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel];
    set_effect_slot(&edited,pattern_effect_slot(),TT_EFFECT_S);
    edit_cell(tracker.step,tracker.channel,edited);
}
static void repeat_above(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(tracker.step==0)return;
    TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel],*dst=&edited;
    const TtCell *src=&tracker.pattern.cell[tracker.step-1][tracker.channel];
    if(tracker.field==0)dst->note=src->note;
    else if(tracker.field==1)dst->instrument=src->instrument;
    else {
        int slot=pattern_effect_slot();
        *tt_cell_effect(dst,slot)=*tt_cell_effect((TtCell *)src,slot);
    }
    edit_cell(tracker.step,tracker.channel,edited);
}
static int selected_pattern_table(int *table) {
    if(tracker.field<2)return 0;
    const TtPatternEffect *fx=tt_cell_effect_const(
        &tracker.pattern.cell[tracker.step][tracker.channel],pattern_effect_slot());
    if(fx->command!=TT_EFFECT_T || fx->value<0 || fx->value>=TT_SONG_TABLES)return 0;
    if(table)*table=fx->value;
    return 1;
}
static void toggle_inline_table(F80Widget *button,void *user) {
    (void)button;(void)user;
    int table;
    if(!selected_pattern_table(&table))return;
    int row=(tracker.edit_order>=0?tracker.edit_order*tracker.song.pattern_rows:0)+tracker.step;
    handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_TOGGLE,.index=table,
        .a=row,.b=tracker.channel});
}
static void inline_table_move_tick(int delta) {
    if(!tracker.inline_table_open)return;
    int length=tracker.song.table[tracker.inline_table_index].length;
    int tick=clamp(tracker.inline_table_tick+delta,0,length-1);
    handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,
        .a=tick,.b=tracker.inline_table_field});
}
static void inline_table_move_field(int delta) {
    if(!tracker.inline_table_open)return;
    int field=clamp(tracker.inline_table_field+delta,0,13);
    handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,
        .a=tracker.inline_table_tick,.b=field});
}
static void inline_table_tick_down(F80Widget *button,void *user) {
    (void)button;(void)user;inline_table_move_tick(-1);
}
static void inline_table_tick_up(F80Widget *button,void *user) {
    (void)button;(void)user;inline_table_move_tick(1);
}
static void inline_table_field_down(F80Widget *button,void *user) {
    (void)button;(void)user;inline_table_move_field(-1);
}
static void inline_table_field_up(F80Widget *button,void *user) {
    (void)button;(void)user;inline_table_move_field(1);
}
static void close_inline_table(F80Widget *button,void *user) {
    (void)button;(void)user;
    handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,.a=-1});
}
static void select_transpose(int delta) {
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    edit_cells(TT_BULK_TRANSPOSE,top,left,bottom,right,delta,NULL,0);
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
    tt_tracker_message("Selection copied");
}
static void clear_selection(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(!tracker.selecting)return;
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    edit_cells(TT_BULK_CLEAR,top,left,bottom,right,0,NULL,0);
    tt_tracker_message("Selection cleared");
}
static void cut_selection(F80Widget *button,void *user) {
    copy_selection(button,user);clear_selection(button,user);tt_tracker_message("Selection cut");
}
static int paste_available(void) {
    if(!tracker.selecting || !tracker.clip_rows)return 0;
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    return bottom-top+1==tracker.clip_rows && right-left+1==tracker.clip_channels;
}
static void paste_selection(F80Widget *button,void *user) {
    (void)button;(void)user;
    if(!paste_available()) { tt_tracker_message("Select an area matching the clipboard size");return; }
    int top,left,bottom,right;selection_bounds(&top,&left,&bottom,&right);
    edit_cells(TT_BULK_PASTE,top,left,top+tracker.clip_rows-1,left+tracker.clip_channels-1,0,
               &tracker.clipboard[0][0],TT_CHANNELS);
    tt_tracker_message("Selection pasted");
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
    {"3","Sm-",tt_down},{"4","Sm+",tt_up},
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
static const F80StatusItem inline_table_status[]={
    {"1","Tk-",inline_table_tick_down},{"2","Tk+",inline_table_tick_up},
    {"3","Fld-",inline_table_field_down},{"4","Fld+",inline_table_field_up},
    {"5","Hide",close_inline_table},{"6","Clr",clear_field},
    {"7","Men",pull_menu},{"8","Ply",toggle_play},
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
static F80StatusItem pattern_status_visible[10];
static F80StatusItem mode_status_visible[10];
static const F80StatusItem *mode_status(const F80StatusItem *base) {
    memcpy(mode_status_visible,base,sizeof(mode_status_visible));
    if(tracker.play_mode==PLAY_SONG) {
        mode_status_visible[0]=(F80StatusItem){"1","Or-",previous_order};
        mode_status_visible[1]=(F80StatusItem){"2","Or+",next_order};
    } else {
        mode_status_visible[0]=(F80StatusItem){"1","Pt-",prev_pattern};
        mode_status_visible[1]=(F80StatusItem){"2","Pt+",next_pattern};
    }
    return mode_status_visible;
}
static const F80StatusItem *current_status(void) {
    if(tracker.focus==FOCUS_WAVE)return wave_status;
    if(tracker.focus==FOCUS_ORDER)return mode_status(order_status);
    if(tracker.focus==FOCUS_REFERENCE)return reference_status;
    if(tracker.inline_table_selected)return mode_status(inline_table_status);
    if(tracker.selecting) {
        memcpy(selection_visible,selection_status,sizeof(selection_visible));
        if(!paste_available()) {
            selection_visible[6].label="---";
            selection_visible[6].action=NULL;
        }
        return mode_status(selection_visible);
    }
    if(tracker.field==0)return mode_status(note_status);
    if(tracker.field==1)return mode_status(instrument_status);
    const F80StatusItem *base=tracker.field%2==0?effect_status:value_status;
    memcpy(pattern_status_visible,base,sizeof(pattern_status_visible));
    int table;
    if(selected_pattern_table(&table)) {
        pattern_status_visible[6].label=tracker.inline_table_open &&
            tracker.inline_table_index==table?"Hide":"Tbl";
        pattern_status_visible[6].action=toggle_inline_table;
    }
    return mode_status(pattern_status_visible);
}
static void change_octave(int delta) {
    tracker.entry_octave=clamp(tracker.entry_octave+delta,0,8);
    tt_tracker_message("Keyboard entry octave changed");
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
static void clear_note_input(void) { tracker.note_input_length=0; }
static int typed_note_value(const char input[4]) {
    static const char letters[]="CDEFGAB";
    static const int semitones[]={0,2,4,5,7,9,11};
    const char *letter=strchr(letters,input[0]);
    if(!letter || (input[1]!='-' && input[1]!='#') ||
       input[2]<'0' || input[2]>'9')return -1;
    int note=(input[2]-'0'+1)*12+semitones[letter-letters]+(input[1]=='#');
    return note<=127?note:-1;
}
static void type_note_key(int key) {
    if((key=='o' || key=='O') && tracker.note_input_length==0) {
        TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel];
        edited.note=TT_NOTE_OFF;
        edit_cell(tracker.step,tracker.channel,edited);
        tt_tracker_message("Note off entered");
        return;
    }
    char character=(char)toupper((unsigned char)key);
    int length=tracker.note_input_length;
    int valid=length==0?strchr("CDEFGAB",character)!=NULL:
              length==1?(character=='-' || character=='#'):
              length==2?character>='0' && character<='9':0;
    if(!valid) { clear_note_input();tt_tracker_message("Type notes as C-4 or C#4");return; }
    tracker.note_input[length++]=character;
    tracker.note_input_length=length;
    if(length!=3)return;
    int note=typed_note_value(tracker.note_input);
    clear_note_input();
    if(note<0) { tt_tracker_message("Note must be between C-0 and G-9");return; }
    TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel];
    edited.note=note;
    edit_cell(tracker.step,tracker.channel,edited);
}
static void advance_pattern_field(void) {
    clear_note_input();
    int field=tracker.field,channel=tracker.channel;
    if(field<7)++field;
    else if(channel<TT_CHANNELS-1) { ++channel;field=0; }
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,.a=channel,.b=field});
}
static void edit_table_ticks(TtBulkOp op,int top,int left,int bottom,int right,
                             const TtTableTick *data) {
    TtEditEvent event={.type=TT_EDIT_TABLE_TICKS_BULK};
    event.table_ticks=(TtEditTableTicksBulk){op,tracker.table_index,top,left,bottom,right,data};
    emit_edit(&event);
}
static void table_clear_area(void) {
    int top,left,bottom,right;
    f80_grid_bounds(&tracker.table_grid,&top,&left,&bottom,&right);
    edit_table_ticks(TT_BULK_CLEAR,top,left,bottom,right,NULL);
}
static void table_copy_area(void) {
    int top,left,bottom,right;
    f80_grid_bounds(&tracker.table_grid,&top,&left,&bottom,&right);
    TtTable *table=&tracker.song.table[tracker.table_index];
    tracker.table_clip_rows=bottom-top+1;
    tracker.table_clip_left=left;tracker.table_clip_right=right;
    for(int row=0;row<tracker.table_clip_rows;++row)
        tracker.table_clip[row]=table->tick[top+row];
    tt_tracker_message("Table selection copied");
}
static void table_paste_area(void) {
    int top,left,bottom,right;
    f80_grid_bounds(&tracker.table_grid,&top,&left,&bottom,&right);
    if(!tracker.table_clip_rows || left!=tracker.table_clip_left ||
       right!=tracker.table_clip_right || bottom-top+1!=tracker.table_clip_rows) {
        tt_tracker_message("Select matching table fields and ticks to paste");return;
    }
    edit_table_ticks(TT_BULK_PASTE,top,left,top+tracker.table_clip_rows-1,right,
                     tracker.table_clip);
    tt_tracker_message("Table selection pasted");
}
static void inline_table_commit(TtTableTick tick) {
    TtEditEvent event={.type=TT_EDIT_TABLE_TICK_SET};
    event.table_tick=(TtEditTableTickSet){tracker.inline_table_index,
        tracker.inline_table_tick,tick};
    emit_edit(&event);
}
static int inline_table_parent_step(void) {
    return tracker.edit_order>=0?
        tracker.inline_table_parent-tracker.edit_order*tracker.song.pattern_rows:
        tracker.inline_table_parent;
}
static void inline_table_return_to_pattern(int step) {
    step=clamp(step,0,tracker.song.pattern_rows-1);
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_ROW_CURSOR,.a=step});
    int channel=tracker.inline_table_channel,field=tracker.field;
    int pattern_index=tracker.edit_order>=0?
        tracker.song.order[tracker.edit_order]:tracker.pattern_index;
    int row=tracker.inline_table_parent%tracker.song.pattern_rows;
    if(pattern_index>=0 && pattern_index<tracker.song.pattern_count) {
        const TtPattern *pattern=pattern_index==tracker.pattern_index?
            &tracker.pattern:&tracker.song.pattern[pattern_index];
        const TtCell *cell=&pattern->cell[row][channel];
        for(int slot=0;slot<3;++slot) {
            const TtPatternEffect *effect=tt_cell_effect_const(cell,slot);
            if(effect->command==TT_EFFECT_T && effect->value==tracker.inline_table_index) {
                field=2+slot*2;
                break;
            }
        }
    }
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,
        .a=channel,.b=field});
}
static int inline_table_vertical_key(int key) {
    if(!tracker.inline_table_open ||
       (key!=F80_KEY_UP && key!=F80_KEY_DOWN))return 0;
    leave_follow();
    int direction=key==F80_KEY_UP?-1:1;
    int length=inline_table_length();
    if(tracker.inline_table_selected) {
        int tick=tracker.inline_table_tick+direction;
        if(tick>=0 && tick<length) {
            clear_note_input();
            handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,
                .a=tick,.b=tracker.inline_table_field});
        } else {
            clear_note_input();
            handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,.a=-1});
            inline_table_return_to_pattern(inline_table_parent_step()+
                (direction>0?1:0));
        }
        return 1;
    }
    int current=tracker.edit_order>=0?
        tracker.edit_order*tracker.song.pattern_rows+tracker.step:tracker.step;
    int parent=tracker.inline_table_parent;
    if((direction>0 && current==parent) ||
       (direction<0 && current==parent+1)) {
        clear_note_input();
        handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,
            .a=direction>0?0:length-1,.b=0});
        return 1;
    }
    return 0;
}
static void inline_table_clear_field(void) {
    TtTableTick tick=tracker.song.table[tracker.inline_table_index].tick[
        tracker.inline_table_tick];
    int field=tracker.inline_table_field;
    if(field==0)tick.note=-1;
    else if(field==1)tick.instrument=-1;
    else if(field%2==0)tick.effect[(field-2)/2]=(TtTableEffect){0,-1};
    else tick.effect[(field-3)/2].value=-1;
    inline_table_commit(tick);
}
static void type_inline_table_note_key(int key) {
    if((key=='o' || key=='O') && tracker.note_input_length==0) {
        TtTableTick edited=tracker.song.table[tracker.inline_table_index].tick[
            tracker.inline_table_tick];
        edited.note=TT_NOTE_OFF;
        inline_table_commit(edited);
        tt_tracker_message("Table note off entered");
        return;
    }
    char character=(char)toupper((unsigned char)key);
    int length=tracker.note_input_length;
    int valid=length==0?strchr("CDEFGAB",character)!=NULL:
              length==1?(character=='-' || character=='#'):
              length==2?character>='0' && character<='9':0;
    if(!valid) {
        clear_note_input();
        tt_tracker_message("Type notes as C-4 or C#4");
        return;
    }
    tracker.note_input[length++]=character;
    tracker.note_input_length=length;
    if(length!=3)return;
    int note=typed_note_value(tracker.note_input);
    clear_note_input();
    if(note<0) {
        tt_tracker_message("Note must be between C-0 and G-9");
        return;
    }
    TtTableTick edited=tracker.song.table[tracker.inline_table_index].tick[
        tracker.inline_table_tick];
    edited.note=note;
    inline_table_commit(edited);
}
static void inline_table_key(int key) {
    int tick=tracker.inline_table_tick,field=tracker.inline_table_field;
    if(key==27) {
        handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,.a=-1});
        return;
    }
    if(key==F80_KEY_UP || key==F80_KEY_DOWN) {
        inline_table_vertical_key(key);
        return;
    }
    if(key==F80_KEY_LEFT || key==F80_KEY_RIGHT) {
        clear_note_input();
        field=clamp(field+(key==F80_KEY_LEFT?-1:1),0,13);
        handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,.a=tick,.b=field});
        return;
    }
    if(key==' ') {
        clear_note_input();
        field++;
        if(field>13) { field=0;tick=(tick+1)%tracker.song.table[tracker.inline_table_index].length; }
        handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,.a=tick,.b=field});
        return;
    }
    if(key==F80_KEY_DELETE || key==F80_KEY_BACKSPACE || key=='.') {
        clear_note_input();
        inline_table_clear_field();return;
    }
    TtTableTick edited=tracker.song.table[tracker.inline_table_index].tick[tick];
    int changed=0,digit=hex_digit(key);
    if(field==0) {
        type_inline_table_note_key(key);
        return;
    } else if(field==1 && digit>=0) {
        edited.instrument=((edited.instrument<0?0:edited.instrument)<<4 & 0xf0)|digit;
        changed=1;
    } else if(field%2==0) {
        int fx=(field-2)/2;
        if(key>='a' && key<='z')key-=32;
        if(key>='G' && key<='Z' && strchr("GIJKMNOPRSTUVWXY",key)) {
            if(edited.effect[fx].command!=key) {
                edited.effect[fx]=(TtTableEffect){(char)key,-1};changed=1;
            }
        }
    } else if(digit>=0) {
        int fx=(field-3)/2;
        if(edited.effect[fx].command) {
            edited.effect[fx].value=((edited.effect[fx].value<0?0:
                edited.effect[fx].value)<<4 & 0xf0)|digit;
            changed=1;
        }
    }
    if(changed)inline_table_commit(edited);
}
static void handle_table_key(int key) {
    if(key==9 || key==F80_KEY_BTAB) {
        tracker.app.focus=tracker.app.focus==tracker.table_name?
                          tracker.table_canvas:tracker.table_name;
        return;
    }
    if(tracker.app.focus==tracker.table_name) {
        f80_key(&tracker.app,key);
        if(strlen(tracker.table_name->input)>=TT_SONG_NAME) {
            tracker.table_name->input[TT_SONG_NAME-1]=0;
            tracker.table_name->input_pos=clamp(tracker.table_name->input_pos,0,
                                                TT_SONG_NAME-1);
        }
        if(tracker.table_name->input[0]) {
            TtEditEvent event={.type=TT_EDIT_TABLE_NAME};
            event.table.table=tracker.table_index;
            snprintf(event.table.name,sizeof(event.table.name),"%s",tracker.table_name->input);
            emit_edit(&event);
        }
        return;
    }
    if(key==2005) { table_copy_area();return; }
    if(key==2006) { table_copy_area();table_clear_area();tt_tracker_message("Table selection cut");return; }
    if(key==2007) { table_paste_area();return; }
    if(key==2008) { table_clear_area();tt_tracker_message("Table selection cleared");return; }
    if(f80_grid_key(&tracker.table_grid,key))return;
    const int edit_table=tracker.table_index,edit_tick=tracker.table_grid.row;
    TtTableTick edited=tracker.song.table[edit_table].tick[edit_tick],*entry=&edited;
    int changed=0;
    if(key=='[')table_select(NULL,(void *)(long)-1);
    else if(key==']')table_select(NULL,(void *)(long)1);
    else if(key=='-')table_resize(NULL,(void *)(long)-1);
    else if(key=='+' || key=='=')table_resize(NULL,(void *)(long)1);
    else if(key==F80_KEY_DELETE || key==F80_KEY_BACKSPACE || key=='.')
        table_clear_area();
    else if(tracker.table_grid.field==0) {
        int note=piano_offset(key);
        if(key=='o' || key=='O') {
            entry->note=TT_NOTE_OFF;
            changed=1;
        } else if(note>=0) {
            entry->note=clamp((tracker.entry_octave+1)*12+note,0,127);
            changed=1;
        }
    } else if(tracker.table_grid.field==1) {
        int digit=hex_digit(key);
        if(digit>=0) {
            entry->instrument=((entry->instrument<0?0:entry->instrument)<<4 & 0xf0)|digit;
            changed=1;
        }
    } else {
        TtTableEffect *fx=&entry->effect[tracker.table_grid.field-2];
        int digit=hex_digit(key);
        if(key>='g' && key<='z')key-=32;
        if(key>='G' && key<='Z' && strchr("GIJKMNOPRSTUVWXY",key)) {
            if(fx->command!=key) { fx->command=(char)key;fx->value=-1;changed=1; }
        }
        else if(digit>=0 && fx->command) {
            fx->value=((fx->value<0?0:fx->value)<<4 & 0xf0)|digit;
            changed=1;
        }
    }
    if(changed) {
        TtEditEvent event={.type=TT_EDIT_TABLE_TICK_SET};
        event.table_tick=(TtEditTableTickSet){edit_table,edit_tick,edited};
        emit_edit(&event);
    }
}
static void update_labels(void) {
    tracker.wave_frame->style=tracker.wave.pcm?0:15;
    if(tracker.order_list) {
        int song_mode=tracker.play_mode==PLAY_SONG;
        int list_count=song_mode?tracker.song.order_count:tracker.song.pattern_count;
        tracker.order_list->item_count=list_count;
        tracker.order_list->range_active=song_mode && tracker.order_range_active;
        tracker.order_list->range_first=tracker.order_range_anchor<tracker.order_range_end?
            tracker.order_range_anchor:tracker.order_range_end;
        tracker.order_list->range_last=tracker.order_range_anchor>tracker.order_range_end?
            tracker.order_range_anchor:tracker.order_range_end;
        tracker.order_list->cursor=clamp(tracker.order_list->cursor,0,
                                        list_count-1);
        tracker.order_frame->title=song_mode?"Orders":"Patterns";
        for(int i=0;i<list_count;++i) {
            if(song_mode)
                snprintf(tracker.order_panel_rows[i],sizeof(tracker.order_panel_rows[i]),
                         "[%c] %02Xo %c %02Xp",
                         tracker.song.order_active[i]?'x':' ',i,
                         tracker.active_order_index==i?'>':' ',tracker.song.order[i]);
            else
                snprintf(tracker.order_panel_rows[i],sizeof(tracker.order_panel_rows[i]),
                         "%02Xp: %.12s",i,tracker.song.name[i]);
            tracker.order_panel_items[i]=tracker.order_panel_rows[i];
        }
    }
    char reference[4];tt_note_name(tracker.phrase.reference_note,reference);
    snprintf(tracker.header_text,sizeof(tracker.header_text),"Turbo Tracker");
    tracker.play_button->text=tracker.playing?"~P~ause":"~P~lay";
    tracker.follow_button->text=tracker.follow?"~F~ollow":"~H~old";
    tracker.mode_button->text=tracker.play_mode==PLAY_SONG?"~S~ong":"~P~attern";
    tracker.pattern_lock_button->text=tracker.pattern_lock?"~U~nlock":"~L~ock";
    snprintf(tracker.bpm_text,sizeof(tracker.bpm_text),"BPM %d",tracker.song.bpm);
    int total=tracker.play_mode==PLAY_SONG?
              tracker.song.order_count*tracker.song.pattern_rows:tracker.song.pattern_rows;
    int current=tracker.active_step>=0?
                (tracker.play_mode==PLAY_SONG && tracker.active_order_index>=0?
                 tracker.active_order_index*tracker.song.pattern_rows:0)+
                tracker.active_step:0;
    tracker.progress->value=total>0?(float)current/total:0;
    if(total>0 && tracker.song.bpm>0 && tracker.song.rows_per_beat>0) {
        float row_secs=60.0f/(tracker.song.bpm*tracker.song.rows_per_beat);
        f80_progress_set_time(tracker.progress,
            (int)(current*row_secs+0.5f),(int)(total*row_secs+0.5f));
    } else {
        f80_progress_set_time(tracker.progress,0,0);
    }
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
                 tracker.song.name[tracker.pattern_index],TT_CHANNELS,
                 tracker.loop?"loop":"once");
    tracker.phrase_frame->title=tracker.phrase_title;
    if(tracker.focus==FOCUS_WAVE)
        snprintf(tracker.details_text,sizeof(tracker.details_text),
                 "WAVE  marker %X %.3fs  |  %s",tracker.marker,
                 (double)tracker.phrase.marker[tracker.marker]/tracker.wave.rate,
                 tracker.message);
    else if(tracker.focus==FOCUS_ORDER)
        snprintf(tracker.details_text,sizeof(tracker.details_text),
                 "ORD %02X -> PAT %02X  |  PLAYHEAD %02X  |  %s",
                 tracker.order_list?tracker.order_list->cursor:0,
                 tracker.song.order[tracker.order_list?tracker.order_list->cursor:0],
                 tracker.active_order_index>=0?tracker.active_order_index:tracker.play_order,
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
        char shown[8]="";
        const TtCell *cell=&tracker.pattern.cell[tracker.step][tracker.channel];
        const char *field_name=tracker.field==0?"NOTE":tracker.field==1?"INST":
            tracker.field%2==0?"FX":"VALUE";
        if(tracker.field==0) {
            if(cell->note==TT_NOTE_OFF)memcpy(shown,"OFF",4);
            else if(cell->note>=0)tt_note_name(cell->note,shown);
            else memcpy(shown,"...",4);
        } else if(tracker.field==1) {
            if(cell->instrument>=0)snprintf(shown,sizeof(shown),"%02X",cell->instrument);
            else memcpy(shown,"..",3);
        }
        else {
            const TtPatternEffect *fx=tt_cell_effect_const(cell,(tracker.field-2)/2);
            tt_pattern_effect_text(fx->command,fx->value,shown);
            if(tracker.field%2)memmove(shown,shown+3,3);
        }
        snprintf(tracker.details_text,sizeof(tracker.details_text),
                 "ROW %02X CH%d  %s  %s  Oct %d  |  %s",tracker.step,tracker.channel+1,
                 shown,field_name,
                 tracker.entry_octave,
                 tracker.message);
    }
}
static void handle_key(int key) {
    if(key==3) { quit(NULL,NULL);return; } /* Ctrl-C */
    if(key==15) { toggle_console(NULL,NULL);return; } /* Ctrl-O */
    if(key==21) { undo_edit(NULL,NULL);return; }
    if(key==25) { redo_edit(NULL,NULL);return; }
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
            if(tracker.app.modal==tracker.exit_dialog)
                handle_ui_event((TtUiEvent){.type=TT_UI_EXIT_CANCEL});
            else if(tracker.app.modal==tracker.manage_dialog)close_manage(NULL,NULL);
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
                TT_SLICES-1:clamp(tracker.slices_list->cursor+
                (key==F80_KEY_UP?-1:1),0,TT_SLICES-1);
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
        } else {
            f80_key(&tracker.app,key);
            if(tracker.app.modal==tracker.order_dialog &&
               tracker.app.focus==tracker.order_name)order_name_changed();
        }
        return;
    }
    if(tracker.app.menu && tracker.app.menu->menu_active) {
        f80_key(&tracker.app,key);return;
    }
    if(tracker.console_visible) {
        int body=tracker.console->rect.h-2;
        if(body<1)body=1;
        int maximum=console_max_scroll(body);
        if(key==F80_KEY_UP)tracker.console_scroll=clamp(tracker.console_scroll+1,0,maximum);
        else if(key==F80_KEY_DOWN)tracker.console_scroll=clamp(tracker.console_scroll-1,0,maximum);
        else if(key==F80_KEY_PPAGE)tracker.console_scroll=
            clamp(tracker.console_scroll+body,0,maximum);
        else if(key==F80_KEY_NPAGE)tracker.console_scroll=
            clamp(tracker.console_scroll-body,0,maximum);
        else if(key==F80_KEY_HOME)tracker.console_scroll=maximum;
        else if(key==F80_KEY_END)tracker.console_scroll=0;
        else if(key==27)toggle_console(NULL,NULL);
        else if(key==' ')toggle_play(NULL,NULL);
        return;
    }
    if(key==' ' && tracker.focus==FOCUS_PHRASE && tracker.inline_table_selected) {
        inline_table_key(key);return;
    }
    if(key==' ' && tracker.focus==FOCUS_PHRASE && !tracker.selecting) {
        if(tracker.field==0 && tracker.note_input_length)
            tt_tracker_message("Complete the note as C-4 or C#4 before advancing");
        else advance_pattern_field();
        return;
    }
    if(key==' ' && tracker.focus==FOCUS_PHRASE && tracker.selecting)return;
    if(key==' ')return;
    if(key==22) { /* Ctrl-V starts a visual selection in the focused editor. */
        if(tracker.focus==FOCUS_PHRASE)
            handle_ui_event((TtUiEvent){.type=TT_UI_BEGIN_VISUAL_SELECTION});
        else if(tracker.focus==FOCUS_ORDER && tracker.play_mode==PLAY_SONG &&
                tracker.order_list) {
            if(tracker.order_range_active)
                send_play(TT_PLAY_CLEAR_ORDER_RANGE,0,0);
            else send_play_range(tracker.order_list->cursor,
                                 tracker.order_list->cursor,1);
        }
        return;
    }
    if(tracker.focus==FOCUS_PHRASE && tracker.selecting &&
       tracker.visual_selecting && key==27) {
        handle_ui_event((TtUiEvent){.type=TT_UI_END_VISUAL_SELECTION});
        return;
    }
    switch(key) {
    case 10:case 13:toggle_play(NULL,NULL);return;
    case 9:case F80_KEY_BTAB:
        handle_ui_event((TtUiEvent){TT_UI_FOCUS,
            key==9?
                tracker.focus==FOCUS_WAVE?FOCUS_PHRASE:
                tracker.focus==FOCUS_PHRASE?FOCUS_ORDER:FOCUS_WAVE:
                tracker.focus==FOCUS_WAVE?FOCUS_ORDER:
                tracker.focus==FOCUS_ORDER?FOCUS_PHRASE:FOCUS_WAVE});
        return;
    case 27:
        if(tracker.focus==FOCUS_ORDER && tracker.order_range_active)
            send_play(TT_PLAY_CLEAR_ORDER_RANGE,0,0);
        if(tracker.selecting || tracker.visual_selecting)
            handle_ui_event((TtUiEvent){.type=TT_UI_END_VISUAL_SELECTION});
        tracker.selecting=0;
        return;
    case '[':change_octave(-1);return;
    case ']':change_octave(1);return;
    case 'L':toggle_loop(NULL,NULL);return;
    default:break;
    }
    if(key>=2001 && key<=2008) {
        F80Action action=current_status()[key-2001].action;
        if(action)action(NULL,NULL);
        if(key>=2005 && key<=2007 && tracker.visual_selecting)
            handle_ui_event((TtUiEvent){.type=TT_UI_END_VISUAL_SELECTION});
        return;
    }
    if(tracker.focus==FOCUS_ORDER) {
        int extending_order_range=
            key==F80_KEY_SHIFT_UP || key==F80_KEY_SHIFT_DOWN ||
            (tracker.order_range_active &&
             (key==F80_KEY_UP || key==F80_KEY_DOWN));
        if(extending_order_range && tracker.play_mode==PLAY_SONG &&
           tracker.order_list) {
            int direction=(key==F80_KEY_SHIFT_UP || key==F80_KEY_UP)?-1:1;
            int anchor=tracker.order_range_active?tracker.order_range_anchor:
                       tracker.order_list->cursor;
            int current_end=tracker.order_range_active?tracker.order_range_end:
                            tracker.order_list->cursor;
            int end=clamp(current_end+direction,0,tracker.song.order_count-1);
            send_play_range(anchor,end,!tracker.order_range_active);
            handle_ui_event((TtUiEvent){.type=TT_UI_ORDER_CURSOR,.index=end});
            return;
        }
        if(key==F80_KEY_UP || key==F80_KEY_DOWN || key==F80_KEY_PPAGE ||
           key==F80_KEY_NPAGE || key==F80_KEY_HOME || key==F80_KEY_END) {
            leave_follow();
            tracker.order_list->scroll_detached=0;
            f80_key(&tracker.app,key);
        }
    } else if(tracker.focus==FOCUS_WAVE) {
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
        if(tracker.inline_table_selected) {
            inline_table_key(key);return;
        }
        if(tracker.selecting && tracker.visual_selecting &&
           (key==F80_KEY_UP || key==F80_KEY_DOWN ||
            key==F80_KEY_LEFT || key==F80_KEY_RIGHT)) {
            handle_ui_event((TtUiEvent){.type=TT_UI_EXTEND_VISUAL_SELECTION,
                .index=key});
            return;
        }
        if((key==F80_KEY_UP || key==F80_KEY_DOWN) &&
           inline_table_vertical_key(key))return;
        if(key==F80_KEY_SHIFT_UP || key==F80_KEY_SHIFT_DOWN ||
           key==F80_KEY_SHIFT_LEFT || key==F80_KEY_SHIFT_RIGHT) {
            leave_follow();
            if(!tracker.selecting) {
                tracker.anchor_step=tracker.step;
                tracker.anchor_channel=tracker.channel;
                tracker.selecting=1;
            }
            if(key==F80_KEY_SHIFT_UP)tracker.step=clamp(tracker.step-1,0,tracker.song.pattern_rows-1);
            else if(key==F80_KEY_SHIFT_DOWN)tracker.step=clamp(tracker.step+1,0,tracker.song.pattern_rows-1);
            else if(key==F80_KEY_SHIFT_LEFT)tracker.channel=clamp(tracker.channel-1,0,TT_CHANNELS-1);
            else tracker.channel=clamp(tracker.channel+1,0,TT_CHANNELS-1);
            if(key==F80_KEY_SHIFT_UP || key==F80_KEY_SHIFT_DOWN)
                tracker.reveal_cursor=1;
            return;
        }
        if(key==F80_KEY_UP || key==F80_KEY_DOWN || key==F80_KEY_LEFT ||
           key==F80_KEY_RIGHT || key==27)tracker.selecting=0;
        if(key==F80_KEY_UP || key==F80_KEY_DOWN || key==F80_KEY_LEFT ||
           key==F80_KEY_RIGHT)leave_follow();
        if(tracker.selecting) {
            if(key=='y') {
                copy_selection(NULL,NULL);
                if(tracker.visual_selecting)
                    handle_ui_event((TtUiEvent){.type=TT_UI_END_VISUAL_SELECTION});
            }
            else if(key=='x') {
                cut_selection(NULL,NULL);
                if(tracker.visual_selecting)
                    handle_ui_event((TtUiEvent){.type=TT_UI_END_VISUAL_SELECTION});
            }
            else if(key=='p') {
                paste_selection(NULL,NULL);
                if(tracker.visual_selecting)
                    handle_ui_event((TtUiEvent){.type=TT_UI_END_VISUAL_SELECTION});
            }
            return;
        }
        TtCell edited=tracker.pattern.cell[tracker.step][tracker.channel],*entry=&edited;
        int digit=hex_digit(key);
        if(key==F80_KEY_UP) {
            clear_note_input();
            move_pattern_row(-1);
        } else if(key==F80_KEY_DOWN) {
            clear_note_input();
            move_pattern_row(1);
        }
        else if(key==F80_KEY_LEFT) {
            clear_note_input();
            int channel=tracker.channel,field=tracker.field;
            if(field)field--;
            else if(channel) { channel--;field=pattern_edit_fields_per_channel(channel)-1; }
            handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,
                .a=channel,.b=field});
        } else if(key==F80_KEY_RIGHT) {
            clear_note_input();
            int channel=tracker.channel,field=tracker.field;
            if(field<pattern_edit_fields_per_channel(channel)-1)field++;
            else if(channel<TT_CHANNELS-1) { channel++;field=0; }
            handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,
                .a=channel,.b=field});
        } else if(key==F80_KEY_DELETE || key==F80_KEY_BACKSPACE || key=='.') {
            clear_note_input();
            if(tracker.field==0)entry->note=-1;
            else if(tracker.field==1)entry->instrument=-1;
            else {
                TtPatternEffect *fx=tt_cell_effect(entry,pattern_effect_slot());
                if(pattern_effect_field())set_effect_slot(entry,pattern_effect_slot(),TT_EFFECT_NONE);
                else fx->value=-1;
            }
            edit_cell(tracker.step,tracker.channel,edited);
        } else if(tracker.field==1 && digit>=0) {
            entry->instrument=digit;
            edit_cell(tracker.step,tracker.channel,edited);
        } else if(pattern_effect_field() &&
                  strchr("GIJKMNOPRSTVWXYZ",key>='a' && key<='z'?key-32:key)) {
            set_effect_slot(entry,pattern_effect_slot(),key>='a' && key<='z'?key-32:key);
            edit_cell(tracker.step,tracker.channel,edited);
        } else if(tracker.field>=3 && tracker.field%2==1 && digit>=0) {
            TtPatternEffect *fx=tt_cell_effect(entry,pattern_effect_slot());
            if(fx->command==TT_EFFECT_NONE)set_effect_slot(entry,pattern_effect_slot(),TT_EFFECT_S);
            fx=tt_cell_effect(entry,pattern_effect_slot());
            fx->value=(((fx->value<0?0:fx->value)<<4)&0xf0)|digit;
            edit_cell(tracker.step,tracker.channel,edited);
        }
        else if(tracker.field==0) {
            type_note_key(key);
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
    if(tracker.console_visible) {
        if(wheel==1 || wheel==-1) {
            int body=tracker.console->rect.h-2;
            tracker.console_scroll=clamp(tracker.console_scroll-wheel*3,0,
                                         console_max_scroll(body>0?body:1));
        }
        return;
    }
    F80Rect r=tracker.wave_canvas->rect;
    if(f80_contains(tracker.header->rect,x,y)) {
        f80_mouse(&tracker.app,x,y,wheel);
        return;
    }
    if(tracker.order_list && f80_contains(tracker.order_list->rect,x,y)) {
        handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_ORDER});
        if(wheel && wheel!=2)
            handle_ui_event((TtUiEvent){TT_UI_SCROLL_ORDER,wheel*3});
        else {
            leave_follow();
            f80_mouse(&tracker.app,x,y,wheel);
        }
        return;
    }
    if(f80_contains(r,x,y)) {
        handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_WAVE});
        if(!tracker.wave.pcm) {
            if(!wheel && y==r.y+r.h-2) {
                if(!recover_missing(NULL))tt_tracker_message("WAV still missing; check its saved path");
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
                tt_tracker_message(status);
            }
        }
        return;
    }
    r=tracker.phrase_canvas->rect;
    if(f80_contains(r,x,y)) {
        handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_PHRASE});
        int body=r.h-3,maximum=grid_total_rows()-body;
        if(maximum<0)maximum=0;
        if(wheel && wheel!=2) {
            leave_follow();
            tracker.phrase_scroll=clamp(tracker.phrase_scroll+wheel*3,0,maximum);
            return;
        }
        if(x==r.x+r.w-1) {
            leave_follow();
            if(y==r.y)tracker.phrase_scroll=clamp(tracker.phrase_scroll-1,0,maximum);
            else if(y==r.y+r.h-1)
                tracker.phrase_scroll=clamp(tracker.phrase_scroll+1,0,maximum);
            else tracker.phrase_scroll=clamp((y-r.y-1)*maximum/(r.h-2),0,maximum);
            return;
        }
        if(y==r.y+2) {
            int direction=x==r.x+4?-1:x==r.x+r.w-2?1:0;
            if(direction) {
                leave_follow();
                int maximum_field=pattern_max_field_scroll(r.w-6);
                tracker.field_scroll=clamp(tracker.field_scroll+direction,0,maximum_field);
                int channel,field;pattern_field_at(tracker.field_scroll,&channel,&field);
                handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,
                    .a=channel,.b=field});
            }
            return;
        }
        if(y>=r.y+3) {
            int global=tracker.phrase_scroll+y-r.y-3;
            int base_global=grid_base_row(global);
            int target_order=grid_order_for_row(global);
            if(global>=grid_total_rows() ||
               (tracker.pattern_lock && tracker.edit_order>=0 &&
                target_order!=tracker.edit_order))
                return;
            leave_follow();
            int old_step=tracker.step,old_channel=tracker.channel;
            int table_tick=-1,inline_line=grid_is_inline_table_row(global,&table_tick);
            if(inline_line) {
                int position=r.x+5;
                for(int column=0;column<8 && position<r.x+r.w-1;++column) {
                    int width=inline_table_column_width(column);
                    if(x>=position && x<position+width) {
                        int field=column==0?0:column==1?1:
                            2+(column-2)*2+(x-position>=3);
                        handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,
                            .a=table_tick,.b=field});
                        return;
                    }
                    position+=width+(column<7?1:0);
                }
                return;
            }
            if(target_order>=0 && target_order!=tracker.edit_order &&
               !handle_ui_event((TtUiEvent){.type=TT_UI_SELECT_ORDER,
                                             .index=target_order}))return;
            handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_ROW_CURSOR,
                .a=base_global%tracker.song.pattern_rows});
            int position=r.x+5;
            for(int index=tracker.field_scroll;index<pattern_field_count() &&
                position<r.x+r.w-1;++index) {
                int channel,field;pattern_field_at(index,&channel,&field);
                if(!inline_line && channel==tracker.inline_table_channel && field>7) {
                    position+=pattern_field_width(field);
                    continue;
                }
                if(index>tracker.field_scroll && field==0)++position;
                int width=pattern_field_width(field);
                if(x>=position && x<position+width) {
                    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,
                                .a=channel,.b=field});
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
       (tracker.pattern_lock && tracker.edit_order>=0 &&
        grid_order_for_row(global)!=tracker.edit_order))
        return 0;
    if(grid_is_inline_table_row(global,NULL)) {
        handle_mouse(x,y,0);
        return 1;
    }
    handle_mouse(x,y,0);
    tracker.anchor_step=tracker.step;
    tracker.anchor_channel=tracker.channel;
    tracker.selecting=1;
    return 1;
}
static void touch_scroll_rows(int rows) {
    F80Rect r=tracker.phrase_canvas->rect;
    int body=r.h-3;
    if(tracker.app.modal || body<1 || !rows)return;
    leave_follow();
    int maximum=grid_total_rows()-body;
    if(maximum<0)maximum=0;
    tracker.phrase_scroll=clamp(tracker.phrase_scroll+rows,0,maximum);
}
static void touch_scroll_fields(int fields) {
    F80Rect r=tracker.phrase_canvas->rect;
    if(tracker.app.modal || r.w<7 || !fields)return;
    leave_follow();
    int maximum=pattern_max_field_scroll(r.w-6);
    tracker.field_scroll=clamp(tracker.field_scroll+fields,0,maximum);
    /* Keep the cursor in the visible field so painting does not snap back. */
    int channel,field;pattern_field_at(tracker.field_scroll,&channel,&field);
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,.a=channel,.b=field});
    handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_PHRASE});
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
    handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_WAVE});
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
    for(int marker=0;marker<TT_MARKERS;++marker) {
        long long distance=llabs((long long)tracker.phrase.marker[marker]-frame);
        if(distance<best) { best=distance;nearest=marker; }
    }
    handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_WAVE});
    tracker.marker=nearest;
    wave_touch.marker_frame=tracker.phrase.marker[nearest];
    char status[64];
    snprintf(status,sizeof(status),"Marker %X selected; drag to move",nearest);
    tt_tracker_message(status);
}
static void move_wave_marker_touch(int x) {
    F80Rect r=tracker.wave_canvas->rect;
    if(tracker.app.modal || !tracker.wave.pcm || r.w<1)return;
    x=clamp(x,r.x,r.x+r.w-1);
    int frame=wave_touch.marker_frame+
        (int)((long long)(x-wave_touch.x)*wave_touch.visible/r.w);
    edit_marker(tracker.marker,frame);
}
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE void tt_tracker_begin_touch_selection(int x,int y) {
    begin_touch_selection(x,y);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_touch_scroll(int rows) {
    touch_scroll_rows(rows);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_touch_scroll_fields(int fields) {
    touch_scroll_fields(fields);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_scroll_order(int rows) {
    order_panel_scroll(rows);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_hold_order_selection(int x,int y) {
    begin_order_range(x,y);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_drag_order_selection(int x,int y) {
    drag_order_range(x,y);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_scroll_table(int ticks) {
    if(tracker.app.modal!=tracker.table_dialog || !tracker.table_canvas)return;
    f80_grid_scroll(&tracker.table_grid,0,ticks);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_scroll_table_horizontal(int cells) {
    if(tracker.app.modal!=tracker.table_dialog || !tracker.table_canvas)return;
    f80_grid_scroll(&tracker.table_grid,cells,0);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_scroll_table_actions(int rows) {
    if(tracker.app.modal!=tracker.table_dialog)return;
    f80_wrap_scroll(&tracker.table_actions,rows,tracker.table_buttons,5);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_hold_table_selection(int x,int y) {
    if(tracker.app.modal!=tracker.table_dialog || !tracker.table_canvas)return;
    f80_grid_mouse(&tracker.table_grid,x,y,0);
    f80_grid_hold(&tracker.table_grid);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_drag_table_selection(int x,int y) {
    if(tracker.app.modal!=tracker.table_dialog || !tracker.table_canvas)return;
    f80_grid_drag(&tracker.table_grid,x,y);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_begin_wave_gesture(int x) {
    begin_wave_gesture(x);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_pan_wave_touch(int x) {
    pan_wave_touch(x);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_hold_wave_marker(int x) {
    hold_wave_marker(x);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_move_wave_marker(int x) {
    move_wave_marker_touch(x);
}
EMSCRIPTEN_KEEPALIVE void tt_tracker_drag_selection(int x,int y) {
    handle_mouse(x,y,2);
}
/* Process browser key and pointer events inside their user-activation event.
   The File menu can then open the native browser picker synchronously. */
EMSCRIPTEN_KEEPALIVE void tt_tracker_flush_input(void) {
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
       NATIVE_TOUCH_TABLE=3, NATIVE_TOUCH_WRAP=4, NATIVE_TOUCH_ORDER=5 };
static struct {
    int down,kind,selecting,moved,x,y,current_x,current_y,last_x,last_y;
    Uint64 started_at;
} native_touch;
static int native_other_button_down;
static int native_ignore_followup_click;
static void native_poll_hold(void) {
    if(tracker.console_visible) { native_touch.down=0;return; }
    if(!native_touch.down || native_touch.moved || native_touch.selecting ||
       SDL_GetTicks()-native_touch.started_at<1000)return;
    if(native_touch.kind==NATIVE_TOUCH_WRAP)return;
    if(native_touch.kind==NATIVE_TOUCH_GRID)
        native_touch.selecting=begin_touch_selection(native_touch.current_x,native_touch.current_y);
    else if(native_touch.kind==NATIVE_TOUCH_ORDER) {
        begin_order_range(native_touch.current_x,native_touch.current_y);
        native_touch.selecting=tracker.order_range_active;
    }
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
    if(native_touch.kind==NATIVE_TOUCH_ORDER) {
        if(native_touch.selecting)drag_order_range(x,y);
        else if(native_touch.moved && y!=native_touch.last_y)
            handle_ui_event((TtUiEvent){TT_UI_SCROLL_ORDER,native_touch.last_y-y});
    } else if(native_touch.kind==NATIVE_TOUCH_GRID) {
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
static int native_decode_sgr_mouse(const char *payload,char ending,
                                   int *x,int *y,mmask_t *state) {
    int button,column,row;
    if(!payload || sscanf(payload,"%d;%d;%d",&button,&column,&row)!=3 ||
       column<1 || row<1)return 0;
    *x=column-1;*y=row-1;*state=0;
    int code=button&3;
    if(button&64) {
#ifdef BUTTON5_PRESSED
        *state=(button&1)?BUTTON5_PRESSED:BUTTON4_PRESSED;
#else
        if(button&1)return 0;
        *state=BUTTON4_PRESSED;
#endif
        return 1;
    }
    if(ending=='m') {
        if(code==0)*state=BUTTON1_RELEASED;
#ifdef BUTTON2_RELEASED
        else if(code==1)*state=BUTTON2_RELEASED;
#endif
#ifdef BUTTON3_RELEASED
        else if(code==2)*state=BUTTON3_RELEASED;
#endif
        else return 0;
    } else if(button&32) {
        *state=REPORT_MOUSE_POSITION;
        if(code==0)*state|=BUTTON1_PRESSED;
#ifdef BUTTON2_PRESSED
        else if(code==1)*state|=BUTTON2_PRESSED;
#endif
#ifdef BUTTON3_PRESSED
        else if(code==2)*state|=BUTTON3_PRESSED;
#endif
    } else {
        if(code==0)*state=BUTTON1_PRESSED;
#ifdef BUTTON2_PRESSED
        else if(code==1)*state=BUTTON2_PRESSED;
#endif
#ifdef BUTTON3_PRESSED
        else if(code==2)*state=BUTTON3_PRESSED;
#endif
        else return 0;
    }
    return *state!=0;
}
static void native_mouse_event_at(int x,int y,mmask_t bstate) {
    if(tracker.console_visible) {
        if(bstate&BUTTON4_PRESSED)handle_mouse(x,y,-1);
#ifdef BUTTON5_PRESSED
        else if(bstate&BUTTON5_PRESSED)handle_mouse(x,y,1);
#endif
        else if(bstate&(BUTTON1_CLICKED|BUTTON1_PRESSED))handle_mouse(x,y,0);
        return;
    }
    if(bstate&BUTTON4_PRESSED) { handle_mouse(x,y,-1);return; }
#ifdef BUTTON5_PRESSED
    if(bstate&BUTTON5_PRESSED) { handle_mouse(x,y,1);return; }
#endif
#ifdef REPORT_MOUSE_POSITION
    if(bstate&REPORT_MOUSE_POSITION) {
        if(native_touch.down)native_move(x,y);
#ifndef BUTTON5_PRESSED
        /* Older ncurses reports wheel-down as position motion. */
        else if(!native_other_button_down)handle_mouse(x,y,1);
#endif
        return;
    }
#endif
    if(bstate&BUTTON1_PRESSED) {
        native_ignore_followup_click=0;
        if(native_touch.down) { native_move(x,y);return; }
        F80Rect r=tracker.phrase_canvas->rect;
        if(!tracker.app.modal && tracker.order_list &&
           order_at(x,y)>=0) {
            native_touch.kind=NATIVE_TOUCH_ORDER;
            handle_ui_event((TtUiEvent){TT_UI_FOCUS,FOCUS_ORDER});
        }
        else if(!tracker.app.modal && x>=r.x+5 && x<r.x+r.w-1 &&
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
            native_ignore_followup_click=1;
        }
        return;
    }
    if(bstate&BUTTON1_RELEASED) {
        native_other_button_down=0;
        if(native_touch.down) {
            if(x!=native_touch.x || y!=native_touch.y)native_touch.moved=1;
            if(native_touch.selecting || native_touch.moved)native_move(x,y);
            else handle_mouse(native_touch.x,native_touch.y,0);
            native_ignore_followup_click=1;
            native_touch.down=native_touch.kind=0;
        }
        return;
    }
    if(bstate&BUTTON1_CLICKED) {
        if(native_ignore_followup_click) {
            native_ignore_followup_click=0;
            return;
        }
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
static void native_mouse_event(void) {
    MEVENT event;
    if(getmouse(&event)==OK)native_mouse_event_at(event.x,event.y,event.bstate);
}
static void native_sgr_mouse_event(void) {
    /* Keep this separate from KEY_MOUSE so ncurses retains its legacy mouse
       sequence while older macOS ncurses decodes SGR reports here. */
    char payload[64];size_t length=0;int ending=0;
    while(length+1<sizeof(payload)) {
        int key=getch();
        if(key==ERR)return;
        if(key=='M' || key=='m') { ending=key;break; }
        payload[length++]=(char)key;
    }
    payload[length]='\0';
    int x,y;mmask_t state;
    if(ending && native_decode_sgr_mouse(payload,(char)ending,&x,&y,&state))
        native_mouse_event_at(x,y,state);
}
#endif
static int header_button_width(void) {
    F80Widget *buttons[]={tracker.play_button,tracker.stop_button,
                        tracker.follow_button,tracker.mode_button,
                        tracker.pattern_lock_button};
    int width=3;
    for(int i=0;i<5;++i) {
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
    int button_x=wide?r.x+cols-button_w-2:r.x+1;
    int info_end=wide?button_x-2:r.x+cols;
    int progress_x=r.x+title_w+3;
    int bpm_x=info_end-bpm_w;
    int progress_w=bpm_x-progress_x-2;
    if(progress_w<0)progress_w=0;
    f80_layout(tracker.header_title,f80_rect(r.x+1,r.y,title_w,1));
    f80_layout(tracker.progress,f80_rect(progress_x,r.y,progress_w,1));
    f80_layout(tracker.bpm_label,f80_rect(bpm_x,r.y,bpm_w,1));
    F80Widget *buttons[]={tracker.play_button,tracker.stop_button,
                        tracker.follow_button,tracker.mode_button,
                        tracker.pattern_lock_button};
    int x=button_x;
    for(int i=0;i<5;++i) {
        int w,h;f80_fit_size(buttons[i],&w,&h);
        f80_layout(buttons[i],f80_rect(x,r.y+(wide?0:2),w,2));
        x+=w+1;
    }
}
static int frame(void *user) {
    (void)user;
    if(!tracker.app.running) {
        while(tracker.app.modal)f80_hide_modal(&tracker.app);
        tt_audio_close();
        if(edit_history) {
            tt_edit_history_free(edit_history);edit_history=NULL;
        } else {
            tt_free(&tracker.wave);
            for(int index=0;index<tracker.sample_count;++index)
                tt_free(&tracker.samples[index].wave);
        }
        f80_free(tracker.root);tracker.root=NULL;
        return 0;
    }
#ifndef __EMSCRIPTEN__
    native_poll_hold();
#endif
    tt_play_advance();
    update_labels();
    const F80StatusItem *items=current_status();
    tracker.status->status_items=items;
    tracker.status->status_count=10;
    int rows,cols;getmaxyx(stdscr,rows,cols);
    int editing=0;
#ifdef __EMSCRIPTEN__
    editing=tt_web_compact_editing();
#endif
    if(editing && !tracker.compact_editing)tracker.reveal_cursor=1;
    tracker.compact_editing=editing;
    int wide_header=header_is_wide(cols);
    tracker.header->height=f80_fixed(wide_header?2:4);
    tracker.wave_row->height=f80_fixed(editing?3:rows>=32?14:rows>=23?10:6);
    f80_layout(tracker.root,f80_rect(0,0,cols,rows));
    if(!tracker.console_visible)layout_header(cols,wide_header);
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
            if(tracker.app.modal==tracker.order_dialog) { w=cols>=92?90:50;h=30; }
            if(tracker.app.modal==tracker.table_dialog) { w=46;h=25; }
            if(tracker.app.modal==tracker.settings_dialog) { w=48;h=16; }
        }
        if(w>cols-2)w=cols-2;
        if(h>rows-2)h=rows-2;
        f80_layout(tracker.app.modal,f80_rect((cols-w)/2,(rows-h)/2,w,h));
        if(tracker.app.modal==tracker.order_dialog)order_refresh();
        if(tracker.app.modal==tracker.settings_dialog) {
            F80Rect r=tracker.app.modal->rect;
            int left=r.x+2,label_w=r.w>=30?21:r.w/2;
            int input_w=r.w-label_w-5;
            if(input_w<1)input_w=1;
            for(int i=0;i<5;++i) {
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
            int wide=r.w>=36;
            int left=r.x+2,inner=r.w-4;
            int bank_y=r.y+2,bank_list_y=r.y+3,bank_h;
            int seq_y,seq_list_y,seq_h,seq_x=left,bank_w=inner,seq_w=inner;
            if(wide) {
                bank_w=(inner-2)/2;seq_x=left+bank_w+2;seq_w=inner-bank_w-2;
                seq_y=bank_y;seq_list_y=bank_list_y;bank_h=seq_h=r.h-19;
                if(bank_h<2)bank_h=seq_h=2;
            } else {
                bank_h=(r.h-21)/2;if(bank_h<2)bank_h=2;
                seq_y=bank_list_y+bank_h+1;seq_list_y=seq_y+1;seq_h=bank_h;
            }
            f80_layout(tracker.order_bank_label,f80_rect(left,bank_y,bank_w,1));
            f80_layout(tracker.order_bank_bg,f80_rect(left,bank_list_y,bank_w,bank_h));
            f80_layout(tracker.order_bank,f80_rect(left,bank_list_y,bank_w,bank_h));
            f80_layout(tracker.order_seq_label,f80_rect(seq_x,seq_y,seq_w,1));
            f80_layout(tracker.order_seq_bg,f80_rect(seq_x,seq_list_y,seq_w,seq_h));
            f80_layout(tracker.order_seq,f80_rect(seq_x,seq_list_y,seq_w,seq_h));
            int input_y=wide?bank_list_y+bank_h+1:seq_list_y+seq_h+1;
            f80_layout(tracker.order_name,f80_rect(left,input_y,bank_w,1));
            int actions_y=input_y+2;
            F80Widget *close=tracker.app.modal->children[15];
            int close_w,close_h;f80_fit_size(close,&close_w,&close_h);
            int close_x=seq_x+seq_w-close_w;
            int close_y=r.y+r.h-5;
            f80_wrap_layout(&order_pattern_actions,
                f80_rect(left,actions_y,bank_w,close_y-actions_y),
                order_pattern_buttons,4);
            f80_wrap_layout(&order_play_actions,
                f80_rect(seq_x,actions_y,seq_w,close_y-actions_y),
                order_play_buttons,4);
            f80_layout(close,f80_rect(close_x,close_y,close_w,close_h));
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
    } else tracker.app.focus=tracker.console_visible?tracker.console:
               tracker.focus==FOCUS_ORDER?tracker.order_list:
               tracker.focus==FOCUS_PHRASE?tracker.phrase_canvas:tracker.wave_canvas;
    f80_draw_screen(&tracker.app);
#ifdef __EMSCRIPTEN__
    if(!web_audio_lifecycle_installed) {
        tt_web_install_audio_lifecycle();
        web_audio_lifecycle_installed=1;
    }
    if(tracker.app.modal==tracker.table_dialog && tracker.table_canvas) {
        F80Rect table=tracker.table_canvas->rect;
        tt_web_table_rect(table.x,table.y,table.w,table.h,
                              tracker.table_grid.show_h,tracker.table_grid.show_v);
        F80Rect actions=tracker.table_actions.rect;
        tt_web_wrap_rect(actions.x,actions.y,actions.w,actions.h);
    } else {
        tt_web_table_rect(0,0,0,0,0,0);
        tt_web_wrap_rect(0,0,0,0);
    }
    if(!tracker.app.modal && !tracker.console_visible) {
        F80Rect grid=tracker.phrase_canvas->rect;
        if(grid.w>6 && grid.h>3) {
            tt_web_grid_rect(grid.x+5,grid.y+3,grid.w-6,grid.h-3);
            int body=grid.h-3,first=tracker.edit_order>=0?
                tracker.edit_order*tracker.song.pattern_rows-tracker.phrase_scroll:0;
            int top=first>0?first:0;
            int bottom=first+tracker.song.pattern_rows<body?first+tracker.song.pattern_rows:body;
            if(bottom>top)
                tt_web_editable_grid_rect(grid.x+5,grid.y+3+top,
                                              grid.w-6,bottom-top);
        }
        F80Rect wave=tracker.wave_canvas->rect;
        if(tracker.wave.pcm && wave.w>0 && wave.h>1)
            tt_web_wave_rect(wave.x,wave.y,wave.w,wave.h-1);
        F80Rect order=tracker.order_list->rect;
        if(order.w>0 && order.h>0)
            tt_web_order_rect(order.x,order.y,order.w,order.h);
    } else {
        tt_web_grid_rect(0,0,0,0);
        tt_web_editable_grid_rect(0,0,0,0);
        tt_web_wave_rect(0,0,0,0);
        tt_web_order_rect(0,0,0,0);
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
    }
#ifndef __EMSCRIPTEN__
    else if(key==KEY_F(64))native_sgr_mouse_event();
#endif
    else if(key!=ERR)handle_key(f80_curses_key(key));
    return 1;
}
static int load_default_song(const char *executable) {
    const char *initial_song=getenv("INIT_SONG");
#ifdef __EMSCRIPTEN__
    (void)executable;
    if(initial_song && initial_song[0]) {
        char path[512];
        const char *selected=initial_song;
        if(initial_song[0]!='/') {
            if(snprintf(path,sizeof(path),"/%s",initial_song)>=(int)sizeof(path)) {
                tt_tracker_message("INIT_SONG path is too long");return 0;
            }
            selected=path;
        }
        if(!load_song_at(selected))return 0;
    } else if(!load_song_at("/default.song"))return 0;
#else
    char path[PATH_MAX],resolved[PATH_MAX],directory[PATH_MAX];
    if(initial_song && initial_song[0]) {
        if(!load_song_at(initial_song))return 0;
        goto loaded;
    }
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
        tt_tracker_message("default.song is missing");return 0;
    }
    if(!load_song_at(resolved))return 0;
loaded:
#endif
    for(int i=0;i<tracker.sample_count;++i) {
        TtWave *wave=i==tracker.sample_index?&tracker.wave:&tracker.samples[i].wave;
        if(!wave->pcm) { tt_tracker_message("Default song has a missing WAV");return 0; }
    }
    return 1;
}
static int initial_play_requested(void) {
    const char *value=getenv("INIT_PLAY");
    return value && value[0] && strcmp(value,"0")!=0;
}
int main(int argc,char **argv) {
    setlocale(LC_ALL,"");
    memset(&tracker,0,sizeof(tracker));
    tracker.entry_octave=4;tracker.loop=tracker.follow=1;tracker.active_step=-1;
    tracker.focus=FOCUS_PHRASE;
    tracker.edit_order=0;tracker.reveal_cursor=1;
    tracker.active_pattern_index=tracker.active_order_index=-1;
    if(!load_default_song(argv[0])) {
        fprintf(stderr,"Could not load default.song: %s\n",tracker.message);
        tt_free(&tracker.wave);
        for(int i=0;i<tracker.sample_count;++i)tt_free(&tracker.samples[i].wave);
        return 1;
    }
    TtEditDoc initial_doc=edit_doc();
    edit_history=tt_edit_history_new(&initial_doc);
    if(!edit_history) {
        fprintf(stderr,"Could not allocate undo history\n");
        tt_free(&tracker.wave);
        for(int i=0;i<tracker.sample_count;++i)tt_free(&tracker.samples[i].wave);
        return 1;
    }
    tt_tracker_message("Default song ready");
    if(argc>1)load_path(argv[1],NULL);
#ifndef __EMSCRIPTEN__
    struct termios original_terminal;
    int restore_terminal=tcgetattr(STDIN_FILENO,&original_terminal)==0;
#endif
    initscr();cbreak();noecho();keypad(stdscr,TRUE);curs_set(0);f80_colors();
#ifndef __EMSCRIPTEN__
    struct termios shortcut_terminal;
    if(tcgetattr(STDIN_FILENO,&shortcut_terminal)==0) {
        /* Deliver Ctrl-C to the shared exit-confirmation path as input. */
        shortcut_terminal.c_lflag&=(tcflag_t)~ISIG;
#ifdef VLNEXT
        /* Ctrl-V starts tracker visual selection; do not treat it as literal-next. */
        shortcut_terminal.c_cc[VLNEXT]=_POSIX_VDISABLE;
#endif
#ifdef VDSUSP
        shortcut_terminal.c_cc[VDSUSP]=_POSIX_VDISABLE;
#endif
#ifdef VDISCARD
        shortcut_terminal.c_cc[VDISCARD]=_POSIX_VDISABLE;
#endif
        tcsetattr(STDIN_FILENO,TCSANOW,&shortcut_terminal);
    }
    define_key("\033[1;5D",KEY_F(60));
    define_key("\033[1;5C",KEY_F(61));
    define_key("\033[5D",KEY_F(60));
    define_key("\033[5C",KEY_F(61));
    define_key("\033[1;2D",KEY_SLEFT);
    define_key("\033[1;2C",KEY_SRIGHT);
    define_key("\033[1;2A",KEY_F(62));
    define_key("\033[1;2B",KEY_F(63));
    define_key("\033[<",KEY_F(64));
#endif
#ifdef REPORT_MOUSE_POSITION
    mousemask(ALL_MOUSE_EVENTS|REPORT_MOUSE_POSITION,NULL);
#else
    mousemask(ALL_MOUSE_EVENTS,NULL);
#endif
#ifndef __EMSCRIPTEN__
    mouseinterval(0);
    /* SGR mouse reports use decimal coordinates beyond the legacy 223-column limit. */
    fputs("\033[?1006h\033[?1002h",stdout);fflush(stdout);
#endif
    tracker.root=f80_window(F80_WINDOW_BLUE,F80_COL);
    tracker.app.menu=f80_add(tracker.root,f80_menubar(menus,sizeof(menus)/sizeof(*menus)));
    tracker.header_gap=f80_add(tracker.root,f80_box(F80_ROW));
    tracker.header_gap->fill=1;
    f80_sized(tracker.header_gap,f80_grow(),f80_fixed(1));
    tracker.header=f80_add(tracker.root,f80_box(F80_ROW));
    f80_sized(tracker.header,f80_grow(),f80_fixed(2));
    tracker.header_title=f80_add(tracker.header,f80_label(tracker.header_text));
    tracker.progress=f80_add(tracker.header,f80_progress(0,0));
    tracker.bpm_label=f80_add(tracker.header,f80_label(tracker.bpm_text));
    tracker.play_button=f80_add(tracker.header,f80_button("~P~lay",toggle_play,NULL));
    tracker.stop_button=f80_add(tracker.header,f80_button("~S~top",stop_playback,NULL));
    tracker.follow_button=f80_add(tracker.header,f80_button("~F~ollow",toggle_follow,NULL));
    tracker.mode_button=f80_add(tracker.header,f80_button("~S~ong",toggle_mode,NULL));
    tracker.pattern_lock_button=f80_add(tracker.header,
        f80_button("~L~ock",toggle_pattern_lock,NULL));
    tracker.wave_row=f80_add(tracker.root,f80_box(F80_ROW));
    f80_sized(tracker.wave_row,f80_grow(),f80_fixed(14));
    tracker.order_frame=f80_add(tracker.wave_row,f80_box(F80_COL));
    tracker.order_frame->frame=1;
    tracker.order_frame->title="ORD";
    f80_sized(tracker.order_frame,f80_fixed(19),f80_grow());
    tracker.order_list=f80_add(tracker.order_frame,
        f80_listbox(tracker.order_panel_items,tracker.song.order_count,
                    order_panel_selected,NULL));
    tracker.order_list->range_cursor_pair=F80_PAIR_RANGE_CURSOR;
    f80_sized(tracker.order_list,f80_grow(),f80_grow());
    tracker.wave_frame=f80_add(tracker.wave_row,f80_box(F80_COL));
    tracker.wave_frame->frame=1;
    f80_sized(tracker.wave_frame,f80_grow(),f80_grow());
    tracker.wave_canvas=f80_add(tracker.wave_frame,f80_canvas(paint_wave,NULL));
    tracker.phrase_frame=f80_add(tracker.root,f80_box(F80_COL));
    tracker.phrase_frame->frame=1;
    tracker.phrase_canvas=f80_add(tracker.phrase_frame,f80_canvas(paint_phrase,NULL));
    tracker.details=f80_add(tracker.root,f80_label(tracker.details_text));
    tracker.details->window_role=F80_WINDOW_CONSOLE;
    tracker.details->style=F80_COLOR_TEXT;
    f80_sized(tracker.details,f80_grow(),f80_fixed(1));
    tracker.status=f80_add(tracker.root,f80_statusbar(wave_status,10));
    tracker.console=f80_add(tracker.root,f80_canvas(paint_event_console,NULL));
    tracker.console->window_role=F80_WINDOW_CONSOLE;
    tracker.console->visible=0;
    tracker.app.running=1;f80_set_root(&tracker.app,tracker.root);
    if(initial_play_requested())send_play(TT_PLAY_START,0,0);
    f80_curses_run(frame,NULL,30);
#ifndef __EMSCRIPTEN__
    if(restore_terminal)tcsetattr(STDIN_FILENO,TCSANOW,&original_terminal);
    fputs("\033[?1002l\033[?1006l",stdout);fflush(stdout);
#endif
    return 0;
}
