#define main tt_tracker_application_main
#include "turbo_tracker.c"
#undef main
#include <assert.h>
static int header_clicks[5];
static void count_header_click(F80Widget *button,void *user) {
    (void)button;
    ++header_clicks[(long)user];
}

int main(void) {
    TtSong demo;
    tt_song_make_demo(&demo);
    assert(demo.pattern_count==4 && demo.order_count==4);
    assert(demo.order[0]==0 && demo.order[1]==2 &&
           demo.order[2]==1 && demo.order[3]==3);
    for(int pattern=0;pattern<4;++pattern) {
        int filled=0;
        for(int row=0;row<TT_STEPS;++row)
            for(int channel=0;channel<TT_CHANNELS;++channel)
                filled+=demo.pattern[pattern].cell[row][channel].note>=0;
        assert(filled==1);
        const TtCell *one=&demo.pattern[pattern].cell[0][0];
        assert(one->note==60 && one->instrument==pattern+1 &&
               one->effect==TT_EFFECT_NONE);
    }
    assert(memcmp(&demo.pattern[0],&demo.pattern[1],sizeof(TtPattern))!=0);
    assert(memcmp(&demo.pattern[1],&demo.pattern[2],sizeof(TtPattern))!=0);
    assert(demo.table[0].length==TT_TABLE_DEFAULT_TICKS);
    assert(demo.table[0].tick[0].effect[0].command=='M');
    assert(demo.table[0].tick[1].effect[0].command=='V' &&
           demo.table[0].tick[5].effect[0].value==0xfa);
    assert(demo.table[5].length==6);
    assert(inline_table_row_width()==42);
#ifndef __EMSCRIPTEN__
    int mouse_x=-1,mouse_y=-1;mmask_t mouse_state=0;
    assert(native_decode_sgr_mouse("0;241;17",'M',
        &mouse_x,&mouse_y,&mouse_state));
    assert(mouse_x==240 && mouse_y==16 && mouse_state==BUTTON1_PRESSED);
    assert(native_decode_sgr_mouse("0;241;17",'m',
        &mouse_x,&mouse_y,&mouse_state));
    assert(mouse_x==240 && mouse_y==16 && mouse_state==BUTTON1_RELEASED);
#endif
    for(int width=50;width<=100;width+=50) {
        memset(&tracker,0,sizeof(tracker));memset(header_clicks,0,sizeof(header_clicks));
        F80Widget *root=f80_box(F80_COL),*header=f80_box(F80_ROW);
        F80Widget wave_canvas={0};
        f80_sized(root,f80_grow(),f80_grow());
        f80_sized(header,f80_grow(),f80_fixed(4));
        f80_add(root,header);
        tracker.header=header;
        tracker.wave_canvas=&wave_canvas;
        tracker.header_title=f80_add(header,f80_label("Tracker"));
        tracker.progress=f80_add(header,f80_progress(0,0));
        tracker.bpm_label=f80_add(header,f80_label("BPM 157"));
        tracker.play_button=f80_add(header,f80_button("Play",count_header_click,(void *)0));
        tracker.stop_button=f80_add(header,f80_button("Stop",count_header_click,(void *)1));
        tracker.follow_button=f80_add(header,f80_button("Hold",count_header_click,(void *)2));
        tracker.mode_button=f80_add(header,f80_button("Song",count_header_click,(void *)3));
        tracker.pattern_lock_button=f80_add(header,f80_button("Lock",count_header_click,(void *)4));
        f80_set_root(&tracker.app,root);
        f80_layout(root,f80_rect(0,0,width,4));
        int wide=header_is_wide(width);
        layout_header(width,wide);
        F80Widget *header_buttons[]={tracker.play_button,tracker.stop_button,
            tracker.follow_button,tracker.mode_button,tracker.pattern_lock_button};
        for(int i=0;i<5;++i) {
            memset(header_clicks,0,sizeof(header_clicks));
            int x=header_buttons[i]->rect.x+header_buttons[i]->rect.w/2;
            int y=header_buttons[i]->rect.y;
            handle_mouse(x,y,0);
            for(int j=0;j<5;++j) {
                assert(header_clicks[j]==(i==j?1:0));
            }
        }
        f80_free(root);
    }
    F80Widget *flex_buttons[]={f80_button("Up",NULL,NULL),
        f80_button("Down",NULL,NULL),f80_button("New",NULL,NULL),
        f80_button("To order",NULL,NULL)};
    F80Wrap flex={0};f80_wrap_init(&flex,1,0);
    f80_wrap_layout(&flex,(F80Rect){0,0,21,8},flex_buttons,4);
    assert(flex_buttons[0]->rect.y==flex_buttons[1]->rect.y &&
           flex_buttons[2]->rect.y>flex_buttons[0]->rect.y);
    f80_wrap_layout(&flex,(F80Rect){0,0,42,8},flex_buttons,4);
    for(int i=1;i<4;++i)assert(flex_buttons[i]->rect.y==flex_buttons[0]->rect.y);
    for(int i=0;i<4;++i)f80_free(flex_buttons[i]);

    memset(&tracker,0,sizeof(tracker));
    tracker.song=demo;tracker.pattern=demo.pattern[0];tracker.pattern_index=0;
    tracker.app.running=1;tracker.document_dirty=1;
    handle_key(3); /* Ctrl-C uses the same dirty-exit confirmation. */
    assert(tracker.app.modal==tracker.exit_dialog && tracker.app.running);
    handle_key(27);
    assert(!tracker.app.modal && tracker.app.running);
    quit(NULL,NULL); /* Menu and status-line actions share this path. */
    assert(tracker.app.modal==tracker.exit_dialog && tracker.app.running);
    handle_ui_event((TtUiEvent){.type=TT_UI_EXIT_CONFIRM});
    assert(!tracker.app.running && !tracker.app.modal);
    tracker.app.running=1;tracker.document_dirty=0;
    handle_key(2010); /* F10 exits without prompting when clean. */
    assert(!tracker.app.running);
    memset(&tracker,0,sizeof(tracker));
    tracker.song=demo;tracker.pattern=demo.pattern[0];tracker.pattern_index=0;
    tracker.edit_order=0;tracker.song.order_count=2;tracker.song.order[1]=1;
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_TOGGLE,
        .index=5,.a=0,.b=1}));
    assert(tracker.inline_table_open && tracker.inline_table_index==5);
    assert(pattern_fields_per_channel(1)==4);
    assert(grid_total_rows()==grid_base_total_rows()+6);
    int inline_tick=-1;
    assert(grid_is_inline_table_row(1,&inline_tick) && inline_tick==0);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,
        .a=5,.b=13}));
    assert(tracker.inline_table_selected && tracker.inline_table_tick==5 &&
           tracker.inline_table_field==13);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_TOGGLE,
        .index=3,.a=0,.b=2}));
    assert(tracker.inline_table_open && tracker.inline_table_index==3 &&
           tracker.inline_table_channel==2);
    assert(!handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_CURSOR,
        .a=6,.b=0}));
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_TOGGLE,
        .index=3,.a=0,.b=2}));
    assert(!tracker.inline_table_open);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_TOGGLE,
        .index=5,.a=0,.b=5}));
    assert(pattern_fields_per_channel(5)==4);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_TOGGLE,
        .index=5,.a=0,.b=5}));
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_TOGGLE,
        .index=5,.a=0,.b=1}));
    tracker.focus=FOCUS_PHRASE;tracker.step=0;tracker.channel=1;tracker.field=2;
    handle_key(F80_KEY_DOWN);
    assert(tracker.inline_table_selected && tracker.inline_table_tick==0);
    handle_key('c');assert(tracker.note_input_length==1);
    handle_key('-');handle_key('4');
    assert(tracker.song.table[5].tick[0].note==60 && !tracker.note_input_length);
    handle_key(F80_KEY_DOWN);
    handle_key('c');handle_key('#');handle_key('4');
    assert(tracker.song.table[5].tick[1].note==61);
    handle_key('z');
    assert(tracker.song.table[5].tick[1].note==61);
    for(int tick=2;tick<TT_TABLE_DEFAULT_TICKS;++tick)handle_key(F80_KEY_DOWN);
    assert(tracker.inline_table_selected &&
           tracker.inline_table_tick==TT_TABLE_DEFAULT_TICKS-1);
    handle_key(F80_KEY_DOWN);
    assert(!tracker.inline_table_selected && tracker.step==1);
    handle_key(F80_KEY_UP);
    assert(tracker.inline_table_selected &&
           tracker.inline_table_tick==TT_TABLE_DEFAULT_TICKS-1);
    while(tracker.inline_table_tick>0)handle_key(F80_KEY_UP);
    handle_key(F80_KEY_UP);
    assert(!tracker.inline_table_selected && tracker.step==0);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_INLINE_TABLE_TOGGLE,
        .index=5,.a=0,.b=1}));
    tracker.song.pattern[1].cell[0][2].effect2=TT_EFFECT_M;
    tracker.song.pattern[1].cell[0][2].value2=0x40;
    assert(pattern_fields_per_channel(0)==4);
    assert(pattern_fields_per_channel(2)==6);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,.a=0,.b=4}));
    assert(pattern_fields_per_channel(0)==6);
    assert(pattern_fields_per_channel(1)==4);

    tracker.focus=FOCUS_PHRASE;tracker.step=0;tracker.channel=1;
    tracker.pattern.cell[0][1]=tt_cell_empty();
    tracker.field=2;
    handle_key('m');
    assert(tracker.pattern.cell[0][1].effect==TT_EFFECT_M);
    handle_key(' ');handle_key('4');handle_key('2');
    assert(tracker.pattern.cell[0][1].value==0x42);
    handle_key(F80_KEY_DELETE);
    assert(tracker.pattern.cell[0][1].effect==TT_EFFECT_M &&
           tracker.pattern.cell[0][1].value==-1);
    tracker.playing=0;
    handle_key(' ');
    assert(tracker.field==4 && tracker.channel==1 && !tracker.playing);
    handle_key('m');handle_key(' ');handle_key('4');handle_key('2');
    assert(tracker.pattern.cell[0][1].effect2==TT_EFFECT_M &&
           tracker.pattern.cell[0][1].value2==0x42);
    handle_key(F80_KEY_DELETE);
    assert(tracker.pattern.cell[0][1].effect2==TT_EFFECT_M &&
           tracker.pattern.cell[0][1].value2==-1);
    handle_key(' ');
    assert(tracker.field==6 && tracker.channel==1 && !tracker.playing);
    handle_key('p');handle_key(' ');handle_key('a');handle_key('f');
    assert(tracker.pattern.cell[0][1].effect3==TT_EFFECT_P &&
           tracker.pattern.cell[0][1].value3==0xaf);
    tracker.field=6;handle_key(F80_KEY_DELETE);
    assert(tracker.pattern.cell[0][1].effect3==TT_EFFECT_NONE &&
           tracker.pattern.cell[0][1].effect2==TT_EFFECT_M);
    tracker.pattern.cell[0][1].note=60;tracker.pattern.cell[0][1].instrument=1;
    tracker.pattern.cell[0][1].effect3=TT_EFFECT_R;
    tracker.pattern.cell[0][1].value3=0x80;
    TtCell selected_before=tracker.pattern.cell[0][1];
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_BEGIN_VISUAL_SELECTION}));
    handle_key('G');
    assert(!memcmp(&tracker.pattern.cell[0][1],&selected_before,sizeof(TtCell)));
    handle_key('y');
    assert(!memcmp(&tracker.clipboard[0][0],&selected_before,sizeof(TtCell)));
    assert(!tracker.visual_selecting && !tracker.selecting);
    tracker.step=1;tracker.field=0;clear_note_input();
    handle_key('c');handle_key('-');handle_key('4');
    assert(tracker.pattern.cell[1][1].note==60);
    tracker.focus=FOCUS_WAVE;tracker.started=tracker.playing=1;
    handle_key(13);
    assert(tracker.started && !tracker.playing);
    handle_key(' ');
    assert(!tracker.playing);

    memset(&tracker,0,sizeof(tracker));
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.sample_count==4 && tracker.song.pattern_count>0 &&
           tracker.song.order_count>0);
    assert(tracker.song.bpm==157 && tracker.song.pattern_rows==16 &&
           tracker.song.rows_per_beat==8 && tracker.song.ticks_per_row==12);
    assert(tracker.song.order[tracker.song.order_count-1]>=0 &&
           tracker.song.order[tracker.song.order_count-1]<tracker.song.pattern_count &&
           tracker.phrase.marker[1]==3816);
    F80Widget manager_dialog={.rect={.w=50}},manager_bank={0},manager_order={0};
    tracker.order_dialog=&manager_dialog;tracker.order_bank=&manager_bank;
    tracker.order_seq=&manager_order;
    order_refresh();
    char visible_pattern_name[10];
    snprintf(visible_pattern_name,sizeof(visible_pattern_name),"%.9s",
             tracker.song.name[tracker.song.order[0]]);
    assert(strstr(tracker.order_rows[0],visible_pattern_name));
    tracker.order_dialog=NULL;tracker.order_bank=NULL;tracker.order_seq=NULL;
    int first=-1,last=-1;
    assert(tt_cell_range(&tracker.phrase,&tracker.song.pattern[0].cell[0][0],
                             tracker.wave.frames,&first,&last));
    assert(first==0 && last==tracker.phrase.marker[8]);
    for(int i=0;i<4;++i) {
        TtWave *wave=i?&tracker.samples[i].wave:&tracker.wave;
        assert(wave->pcm && wave->frames==34413 && wave->rate==44100);
        tt_free(wave);
    }

    memset(&tracker,0,sizeof(tracker));
    tt_song_init(&tracker.song);
    tracker.tick_remainder=0;
    int minute_frames=0;
    const int tick_divisor=112*4*12;
    for(int tick=0;tick<tick_divisor;++tick)
        minute_frames+=tt_play_next_tick_frames(tick_divisor);
    assert(minute_frames==TT_AUDIO_OUTPUT_RATE*60 && tracker.tick_remainder==0);
    table_resize(NULL,(void *)(long)1);
    assert(tracker.song.table[0].length==7);
    for(int i=0;i<20;++i)table_resize(NULL,(void *)(long)1);
    assert(tracker.song.table[0].length==TT_TABLE_MAX_TICKS);
    for(int i=0;i<20;++i)table_resize(NULL,(void *)(long)-1);
    assert(tracker.song.table[0].length==TT_TABLE_MIN_TICKS);
    tracker.song.pattern_count=2;
    tt_pattern_init(&tracker.song.pattern[1]);
    tt_edit_clear_pattern(&tracker.song.pattern[0]);
    tt_edit_clear_pattern(&tracker.song.pattern[1]);
    tracker.song.order_count=3;
    tracker.song.order[0]=0;
    tracker.song.order[1]=1;
    tracker.song.order[2]=0;
    tracker.pattern=tracker.song.pattern[0];
    tracker.edit_order=0;
    TtEditDoc navigation_doc=edit_doc();
    edit_history=tt_edit_history_new(&navigation_doc);assert(edit_history);
    tracker.step=tracker.song.pattern_rows-1;
    tracker.pattern_lock=0;tracker.play_order=0;tracker.active_order_index=0;
    move_pattern_row(1);
    assert(tracker.pattern_index==1 && tracker.edit_order==1 && tracker.step==0 &&
           tracker.play_order==0 && tracker.active_order_index==0);
    tracker.step=tracker.song.pattern_rows-1;
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_LOCK,.a=1});
    move_pattern_row(1);
    assert(tracker.pattern_lock && tracker.pattern_index==1 &&
           tracker.edit_order==1 && tracker.step==tracker.song.pattern_rows-1);
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_LOCK,.a=0});
    handle_ui_event((TtUiEvent){.type=TT_UI_SELECT_ORDER,.index=0});
    tracker.step=0;
    tt_edit_history_free(edit_history);edit_history=NULL;
    tracker.focus=FOCUS_PHRASE;
    tracker.entry_octave=4;
    tracker.step=0;tracker.channel=1;tracker.field=0;
    TtCell *note_entry=&tracker.pattern.cell[0][1];
    assert(note_entry->note<0 && note_entry->instrument<0 &&
           note_entry->effect==TT_EFFECT_NONE);
    handle_key('c');handle_key('-');handle_key('4');handle_key(' ');
    assert(note_entry->note==60 && tracker.field==1 && note_entry->instrument<0 &&
           note_entry->effect==TT_EFFECT_NONE && note_entry->value<0);
    note_entry->instrument=1;
    note_entry->effect=TT_EFFECT_M;note_entry->value=0x40;
    handle_key(' ');handle_key('x');
    assert(note_entry->instrument==1 && note_entry->effect==TT_EFFECT_M &&
           note_entry->value==0x40);
    tracker.channel=2;tracker.field=2;
    TtCell *effect_entry=&tracker.pattern.cell[0][2];
    handle_key('s');
    assert(effect_entry->effect==TT_EFFECT_S && effect_entry->value==-1);
    handle_key(' ');handle_key('4');
    assert(effect_entry->value==4);
    tracker.follow=1;
    tracker.play_mode=PLAY_SONG;
    tracker.loop=0;
    toggle_loop(NULL,NULL);
    assert(tracker.loop==1);
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_TOGGLE_LOOP}));
    assert(tracker.loop==1);
    tt_play_flush();
    assert(tracker.loop==0);
    toggle_loop(NULL,NULL);
    toggle_follow(NULL,NULL);
    assert(tracker.follow==0);
    toggle_follow(NULL,NULL);
    assert(tracker.follow==1);
    F80Widget order_choice={0};order_choice.cursor=1;
    tracker.play_order=0;
    tracker.active_order_index=0;
    order_panel_selected(&order_choice,NULL);
    assert(tracker.play_order==1 && tracker.play_step==0 &&
           tracker.active_order_index==1 && tracker.active_step==0 &&
           tracker.play_mode==PLAY_SONG);
    assert(tracker.pattern_index==1 && tracker.edit_order==1 && tracker.phrase_scroll==16);
    assert(tracker.follow==0);
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_ROW_PLAYED,.a=0,.b=0,.c=2}));
    tt_play_flush();
    assert(order_choice.cursor==1 && tracker.edit_order==1 &&
           tracker.active_order_index==2);
    order_choice.cursor=2; /* the same pattern also appears at order 0 */
    order_panel_selected(&order_choice,NULL);
    assert(tracker.pattern_index==0 && tracker.edit_order==2 && tracker.phrase_scroll==32);
    assert(tracker.play_order==2 && tracker.active_order_index==2);
    order_choice.cursor=0;
    order_panel_selected(&order_choice,NULL);
    assert(tracker.pattern_index==0 && tracker.edit_order==0 && tracker.phrase_scroll==0);
    assert(tracker.play_order==0 && tracker.active_order_index==0);
    tracker.focus=FOCUS_WAVE;
    handle_key(9);
    assert(tracker.focus==FOCUS_PHRASE);
    handle_key(9);
    assert(tracker.focus==FOCUS_ORDER && current_status()==mode_status_visible);
    assert(strcmp(current_status()[0].label,"Or-")==0 &&
           current_status()[0].action==previous_order);
    tracker.play_mode=PLAY_PATTERN;
    assert(strcmp(current_status()[0].label,"Pt-")==0 &&
           current_status()[0].action==prev_pattern);
    tracker.focus=FOCUS_PHRASE;
    F80Widget pattern_choice={0};pattern_choice.cursor=0;
    tracker.order_list=&pattern_choice;
    order_panel_selected(&pattern_choice,NULL);
    assert(tracker.pattern_index==0 && tracker.edit_order==-1 &&
           tracker.loop_pattern_index==0 && tracker.play_mode==PLAY_PATTERN);
    next_pattern(NULL,NULL);
    assert(tracker.pattern_index==1 && tracker.edit_order==-1 &&
           tracker.loop_pattern_index==1);
    tracker.pattern_index=0;
    tracker.loop_pattern_index=0;
    tracker.follow=1;tracker.playing=1;tracker.active_pattern_index=0;
    tracker.active_step=8;tracker.phrase_scroll=0;
    ensure_pattern_visible(f80_rect(0,0,80,14));
    assert(tracker.phrase_scroll==3);
    tracker.follow=0;tracker.playing=0;
    tracker.focus=FOCUS_ORDER;
    handle_ui_event((TtUiEvent){.type=TT_UI_MODE_CONTEXT,.a=PLAY_SONG});
    tracker.play_mode=PLAY_SONG;
    assert(strcmp(current_status()[0].label,"Or-")==0);
    tracker.order_list=&order_choice;
    tracker.edit_order=0;
    tracker.phrase_scroll=0;
    tracker.play_order=0;
    tracker.active_order_index=0;
    handle_key(9);
    assert(tracker.focus==FOCUS_WAVE);
    handle_key(F80_KEY_BTAB);
    assert(tracker.focus==FOCUS_ORDER);
    order_choice.item_count=3;order_choice.rect=f80_rect(80,0,10,2);
    tracker.order_list=&order_choice;
    tracker.follow=1;
    handle_ui_event((TtUiEvent){TT_UI_SCROLL_ORDER,1});
    assert(!tracker.follow && order_choice.scroll==1 && order_choice.scroll_detached);
    assert(order_choice.cursor==0 && tracker.edit_order==0 &&
           tracker.play_order==0 && tracker.active_order_index==0);
    tracker.order_list=NULL;
    tracker.focus=FOCUS_PHRASE;
    toggle_follow(NULL,NULL);
    assert(tracker.follow==1);
    tracker.play_order=0;
    tracker.active_order_index=-1;
    tracker.active_step=-1;
    assert(grid_total_rows()==48);
    assert(grid_pattern_for_row(0)==0);
    assert(grid_pattern_for_row(16)==1);
    assert(grid_pattern_for_row(32)==0);
    F80Widget setting={0};
    snprintf(setting.input,sizeof(setting.input),"32");
    int value=0;
    assert(setting_number(&setting,16,TT_MAX_STEPS,&value) && value==32);
    snprintf(setting.input,sizeof(setting.input),"32x");
    assert(!setting_number(&setting,16,TT_MAX_STEPS,&value));
    tracker.song.pattern_rows=32;
    assert(grid_total_rows()==96);
    assert(grid_pattern_for_row(32)==1 && grid_pattern_for_row(64)==0);
    tracker.step=31;handle_key(F80_KEY_DOWN);
    assert(tracker.step==0 && tracker.edit_order==1 && tracker.pattern_index==1 &&
           tracker.play_order==0);
    assert(tracker.follow==0);
    tracker.song.pattern_rows=16;
    handle_ui_event((TtUiEvent){.type=TT_UI_SELECT_ORDER,.index=0});
    tracker.step=0;

    F80Widget header={0},wave={0},grid={0};
    tracker.header=&header;
    tracker.wave_canvas=&wave;
    tracker.phrase_canvas=&grid;
    grid.rect=f80_rect(0,0,80,30);
    order_choice.scroll=0;
    tracker.order_list=&order_choice;
    tracker.follow=1;
    handle_mouse(81,1,1); /* wheel scrolls the list, not its selection */
    assert(tracker.focus==FOCUS_ORDER && !tracker.follow);
    assert(order_choice.scroll==1 && order_choice.cursor==0);
    assert(tracker.play_order==0 && tracker.active_order_index==-1);
    tracker.order_list=NULL;
    tracker.focus=FOCUS_PHRASE;
    tracker.step=4;
    tracker.follow=1;
    handle_mouse(10,19,0); /* Mouse selects a visible order without seeking playback. */
    assert(tracker.pattern_index==1 && tracker.edit_order==1 && tracker.step==0);
    assert(tracker.follow==0 && tracker.play_order==0);
    assert(begin_touch_selection(10,19));
    tracker.selecting=0;
    tracker.phrase_scroll=32;
    handle_mouse(10,7,0); /* repeated patterns can be selected at another order */
    assert(tracker.pattern_index==0 && tracker.edit_order==2 && tracker.step==4);
    tracker.phrase_scroll=0;
    handle_mouse(10,5,0); /* an editable cell is manual navigation */
    assert(tracker.follow==0 && tracker.step==2);
    tracker.follow=1;
    handle_mouse(10,5,1);
    assert(tracker.follow==0 && tracker.phrase_scroll==3);
    tracker.follow=1;
    touch_scroll_rows(1);
    assert(tracker.follow==0 && tracker.phrase_scroll==4);
    tracker.follow=1;
    handle_mouse(79,10,0); /* grid scrollbar */
    assert(tracker.follow==0);

    tracker.phrase_scroll=0;
    tracker.follow=1;
    tracker.play_order=1;
    tt_play_play_row(0);
    assert(tracker.active_order_index==1);
    assert(tracker.pattern_index==0 && tracker.edit_order==0);

    tracker.playing=1;
    F80Rect viewport=f80_rect(0,0,80,14); /* 11 pattern rows */
    tracker.active_order_index=0;tracker.active_step=0;
    ensure_pattern_visible(viewport);
    assert(tracker.phrase_scroll==0);
    tracker.active_order_index=1;tracker.active_step=8;
    ensure_pattern_visible(viewport);
    assert(tracker.phrase_scroll==19);
    tracker.compact_editing=1;
    tracker.active_step=9;
    ensure_pattern_visible(viewport);
    assert(tracker.phrase_scroll==20);
    tracker.compact_editing=0;
    tracker.active_order_index=2;tracker.active_step=15;
    ensure_pattern_visible(viewport);
    assert(tracker.phrase_scroll==37);

    select_edit_pattern(1);
    assert(tracker.pattern_index==1 && tracker.edit_order==1);
    assert(tracker.phrase_scroll==16);
    tracker.pattern.cell[0][0]=(TtCell){-1,-1,TT_EFFECT_T,0,
                                          TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.play_mode=PLAY_PATTERN;tracker.loop_pattern_index=1;
    tracker.song.table[0].tick[0].note=60;
    tracker.song.table[0].tick[0].instrument=1;
    tracker.song.table[0].tick[0].effect[0]=(TtTableEffect){'M',0x33};
    tracker.song.table[0].tick[0].effect[1]=(TtTableEffect){'U',2};
    tracker.song.table[0].tick[1].effect[0]=(TtTableEffect){'X',1};
    tracker.song.table[0].length=2;
    tt_play_play_row(0);
    assert(tracker.table_voice[0].active && tracker.table_voice[0].table==0);
    tt_play_table_ticks();
    assert(tracker.table_voice[0].pending && tracker.table_voice[0].delay==2);
    tt_play_table_ticks();
    assert(tracker.table_voice[0].table==1 && tracker.table_voice[0].tick==0);
    tt_play_table_ticks();
    assert(!tracker.table_voice[0].pending);
    const int fields[]={3,2,3,3,3,3,3,3};
    f80_grid_init(&tracker.table_grid,16,8,fields,NULL);
    f80_grid_layout(&tracker.table_grid,f80_rect(0,0,20,8));
    F80Widget table_focus={0};
    tracker.app.focus=&table_focus;
    tracker.table_grid.row=4;tracker.table_grid.field=2;
    handle_table_key('s');
    assert(tracker.song.table[0].tick[4].effect[0].command=='S' &&
           tracker.song.table[0].tick[4].effect[0].value==-1);
    handle_table_key('4');
    assert(tracker.song.table[0].tick[4].effect[0].value==4);
    tracker.table_grid.row=5;
    handle_table_key('r');
    assert(tracker.song.table[0].tick[5].effect[0].command=='R');
    handle_table_key('8');handle_table_key('0');
    assert(tracker.song.table[0].tick[5].effect[0].value==0x80);
    tracker.table_grid.row=0;
    tracker.table_grid.field=2;
    f80_grid_hold(&tracker.table_grid);
    f80_grid_key(&tracker.table_grid,F80_KEY_SHIFT_DOWN);
    table_copy_area();table_clear_area();
    assert(!tracker.song.table[0].tick[0].effect[0].command);
    tracker.table_grid.row=2;tracker.table_grid.anchor_row=2;
    f80_grid_key(&tracker.table_grid,F80_KEY_SHIFT_DOWN);
    table_paste_area();
    assert(tracker.song.table[0].tick[2].effect[0].command=='M');
    assert(tracker.song.table[0].tick[3].effect[0].command=='X');
    tracker.song.table[0].length=TT_TABLE_MAX_TICKS;
    tracker.play_mode=PLAY_PATTERN;
    tracker.loop_pattern_index=tracker.pattern_index;
    tracker.pattern.cell[0][5]=(TtCell){-1,-1,TT_EFFECT_P,0,
                                          TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.pattern.cell[0][5].effect2=TT_EFFECT_P;
    tracker.pattern.cell[0][5].value2=255;
    tracker.pattern.cell[0][5].effect3=TT_EFFECT_P;
    tracker.pattern.cell[0][5].value3=0;
    tt_play_play_row(0);
    assert(tt_audio_pan(5)==0);
    tracker.song.table[0].tick[6].effect[0]=(TtTableEffect){'P',255};
    tracker.table_voice[5]=(TableVoice){.active=1,.table=0,.tick=6,.next_table=-1};
    tt_play_table_ticks();
    assert(tt_audio_pan(5)==1);
    tracker.pattern.cell[0][5]=(TtCell){-1,-1,TT_EFFECT_STRETCH,0x40,
                                          TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tt_play_play_row(0);
    assert(fabs(tt_audio_stretch(5)-2)<1e-10);
    tracker.song.table[0].tick[7].effect[0]=(TtTableEffect){'R',0xc0};
    tracker.table_voice[5]=(TableVoice){.active=1,.table=0,.tick=7,.next_table=-1};
    tt_play_table_ticks();
    assert(fabs(tt_audio_stretch(5)-.5)<1e-10);
    float silent[1600]={0};
    tracker.wave.pcm=silent;tracker.wave.frames=1600;tracker.wave.rate=44100;
    tt_phrase_init(&tracker.phrase,tracker.wave.frames);
    wave.rect=f80_rect(0,1,80,8);
    tracker.marker=0;
    int marker_x=wave_x_from_frame(tracker.phrase.marker[5],wave.rect.w*2)/2;
    handle_mouse(marker_x,3,0);
    assert(tracker.marker==5);
    handle_mouse(marker_x+2,3,0);
    assert(tracker.marker==5);
    tracker.wave.pcm=NULL; /* stack-backed smoke sample is not history-owned */
    TtEditDoc history_doc=edit_doc();
    edit_history=tt_edit_history_new(&history_doc);
    assert(edit_history);
    F80Widget order_input={0};
    order_input.kind=F80_LISTBOX;
    order_input.range_cursor_pair=F80_PAIR_RANGE_CURSOR;
    order_input.rect=f80_rect(0,0,12,4);order_input.item_count=tracker.song.order_count;
    order_input.range_active=1;order_input.range_first=1;order_input.range_last=2;
    order_input.cursor=1;
    assert(f80_listbox_row_pair(&order_input,1)==F80_PAIR_RANGE_CURSOR);
    assert(f80_listbox_row_pair(&order_input,2)==65);
    assert(f80_listbox_row_pair(&order_input,0)==2);
    F80Widget *saved_order_list=tracker.order_list;
    int saved_focus=tracker.focus,saved_mode=tracker.play_mode;
    tracker.order_list=&order_input;tracker.focus=FOCUS_ORDER;tracker.play_mode=PLAY_SONG;
    tracker.app.running=1;
    tracker.order_range_active=0;tracker.order_list->cursor=0;
    handle_key(22);tt_play_flush();
    assert(tracker.order_range_active);
    handle_key(27);tt_play_flush();
    assert(!tracker.order_range_active && tracker.app.running!=0);
    handle_key(22);tt_play_flush();
    assert(tracker.order_range_active);
    handle_key(22);tt_play_flush();
    assert(!tracker.order_range_active);
    tracker.order_list->cursor=1;
    handle_key(22);tt_play_flush();
    handle_key(F80_KEY_DOWN);tt_play_flush();
    assert(tracker.order_range_active && tracker.order_range_anchor==1 &&
           tracker.order_range_end==2 && tracker.order_list->cursor==2);
    handle_key(F80_KEY_SHIFT_UP);tt_play_flush();
    assert(tracker.order_range_active && tracker.order_range_end==1 &&
           tracker.order_list->cursor==1);
    handle_key(27);tt_play_flush();
    assert(!tracker.order_range_active);
    unsigned char activity_before=tracker.song.order_active[0];
    begin_order_range(0,0); /* Checkbox click in the panel. */
    assert(tracker.song.order_active[0]==!activity_before);
    assert(tt_edit_undo(edit_history,&history_doc));
    assert(tracker.song.order_active[0]==activity_before);
    tracker.order_list=saved_order_list;tracker.focus=saved_focus;tracker.play_mode=saved_mode;
    tracker.focus=FOCUS_PHRASE;
    tracker.anchor_step=2;tracker.step=3;
    tracker.anchor_channel=0;tracker.channel=1;
    tracker.selecting=1;
    tracker.pattern.cell[2][0]=(TtCell){60,1,TT_EFFECT_NONE,-1,
                                          TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.pattern.cell[2][1]=(TtCell){61,1,TT_EFFECT_M,0x12,
                                          TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.pattern.cell[3][0]=(TtCell){62,1,TT_EFFECT_NONE,-1,
                                          TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.pattern.cell[3][1]=(TtCell){63,1,TT_EFFECT_S,0x34,
                                          TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    handle_key('y');
    assert(tracker.clip_rows==2 && tracker.clip_channels==2 &&
           tracker.clipboard[0][0].note==60 && tracker.clipboard[1][1].value==0x34);
    handle_key('x');
    assert(tracker.pattern.cell[2][0].note<0 && tracker.pattern.cell[3][1].effect==TT_EFFECT_NONE);
    handle_key('p');
    assert(tracker.pattern.cell[2][0].note==60 && tracker.pattern.cell[3][1].value==0x34);
    tracker.selecting=0;
    tracker.step=5;tracker.channel=2;
    tracker.pattern.cell[5][3]=tt_cell_empty();
    handle_key(22); /* Ctrl-V */
    assert(tracker.selecting && tracker.visual_selecting &&
           tracker.anchor_step==5 && tracker.anchor_channel==2 &&
           selected_cell(grid_edit_row(),tracker.channel));
    handle_key(F80_KEY_RIGHT);
    assert(tracker.channel==3 && tracker.selecting);
    handle_key('d');
    assert(tracker.pattern.cell[5][3].note<0);
    handle_key(22);
    handle_key('y');
    assert(!tracker.selecting && !tracker.visual_selecting);
    handle_key(22);
    handle_key('x');
    assert(!tracker.selecting && !tracker.visual_selecting);
    handle_key(22);
    handle_key('p');
    assert(!tracker.selecting && !tracker.visual_selecting);
    handle_key(22);
    handle_key(2005); /* F5 */
    assert(!tracker.selecting && !tracker.visual_selecting);
    handle_key(22);
    handle_key(2006); /* F6 */
    assert(!tracker.selecting && !tracker.visual_selecting);
    handle_key(22);
    handle_key(27);
    assert(!tracker.selecting && !tracker.visual_selecting);
    handle_key(22);
    handle_key(2007); /* F7 */
    assert(!tracker.selecting && !tracker.visual_selecting);
    int original_note=tracker.pattern.cell[0][4].note;
    TtEditEvent change={.type=TT_EDIT_CELL_SET};
    change.cell_set=(TtEditCellSet){0,4,{72,1,TT_EFFECT_NONE,-1}};
    assert(emit_edit(&change) && tracker.pattern.cell[0][4].note==72);
    handle_key(26); /* Ctrl-Z is reserved for suspending the terminal. */
    assert(tracker.pattern.cell[0][4].note==72);
    handle_key(21); /* Ctrl-U */
    assert(tracker.pattern.cell[0][4].note==original_note);
    handle_key(25); /* Ctrl-Y */
    assert(tracker.pattern.cell[0][4].note==72);
    assert(!strcmp(menus[1].title,"~E~dit"));
    edit_items[0].action(NULL,NULL);
    assert(tracker.pattern.cell[0][4].note==original_note);
    edit_items[1].action(NULL,NULL);
    assert(tracker.pattern.cell[0][4].note==72);
    tt_edit_history_free(edit_history);edit_history=NULL;
    F80Widget root={.visible=1,.window_role=F80_WINDOW_BLUE};
    F80Widget gap={.visible=1},wave_row={.visible=1},phrase_frame={.visible=1};
    F80Widget details={.visible=1},status={.visible=1};
    F80Widget console={.rect={0,1,80,10}};
    header.visible=1;
    tracker.root=&root;tracker.header_gap=&gap;tracker.header=&header;
    tracker.wave_row=&wave_row;tracker.phrase_frame=&phrase_frame;
    tracker.details=&details;tracker.status=&status;tracker.console=&console;
    int logged=event_log_count;
    view_items[5].action(NULL,NULL);
    assert(tracker.console_visible && root.window_role==F80_WINDOW_CONSOLE);
    assert(!strcmp(view_items[5].label,"~H~ide event console"));
    assert(console.visible && !header.visible && !details.visible && !status.visible);
    assert(event_log_count>logged);
    int log_head_before=event_log_head;
    assert(tt_play_emit((TtPlayEvent){TT_PLAY_ROW_PLAYED,1,0,0}));
    tt_play_flush();
    assert(event_log_head==log_head_before);
    assert(tt_play_emit((TtPlayEvent){TT_PLAY_ROW_PLAYED,0,0,0}));
    tt_play_flush();
    assert(event_log_head==(log_head_before+1)%TT_EVENT_LOG_CAPACITY);
    for(int i=0;i<20;++i)tt_tracker_log_event("TEST","stream entry");
    handle_key(F80_KEY_PPAGE);
    assert(tracker.console_scroll>0);
    handle_key(F80_KEY_END);
    assert(tracker.console_scroll==0);
    for(int i=0;i<TT_EVENT_LOG_CAPACITY+5;++i)
        tt_tracker_log_event("TEST","bounded stream");
    assert(event_log_count==TT_EVENT_LOG_CAPACITY);
    assert(strstr(event_log[(event_log_head+TT_EVENT_LOG_CAPACITY-1)%TT_EVENT_LOG_CAPACITY],
                  "bounded stream"));
    handle_key(15); /* Ctrl-O */
    assert(!tracker.console_visible && root.window_role==F80_WINDOW_BLUE);
    assert(!strcmp(view_items[5].label,"~S~how event console"));
    assert(!console.visible && header.visible && details.visible && status.visible);
    int patterns_before=tracker.song.pattern_count,orders_before=tracker.song.order_count;
    order_choice.item_count=orders_before;order_choice.cursor=0;
    tracker.order_list=&order_choice;
    tracker.focus=FOCUS_ORDER;
    tracker.play_mode=PLAY_SONG;
    unsigned char was_active=tracker.song.order_active[0];
    assert(!strcmp(order_status[7].label,"Act"));
    order_status[7].action(NULL,NULL);
    assert(tracker.song.pattern_count==patterns_before &&
           tracker.song.order_count==orders_before &&
           tracker.song.order_active[0]==!was_active);
    puts("default song, ordered grid, and playback follow passed");
    return 0;
}
