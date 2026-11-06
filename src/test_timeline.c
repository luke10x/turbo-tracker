#define main tt_tracker_application_main
#include "turbo_tracker.c"
#undef main
#include <assert.h>

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
    assert(demo.table[0].length==16);
    assert(demo.table[0].tick[0].effect[0].command=='M');
    assert(demo.table[0].tick[1].effect[0].command=='V' &&
           demo.table[0].tick[8].effect[0].value==0xfa);

    memset(&tracker,0,sizeof(tracker));
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.sample_count==4 && tracker.song.pattern_count==13 &&
           tracker.song.order_count==28);
    assert(tracker.song.bpm==157 && tracker.song.pattern_rows==16 &&
           tracker.song.rows_per_beat==8 && tracker.song.ticks_per_row==12);
    assert(tracker.song.order[27]==12 && tracker.phrase.marker[1]==3816);
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
    assert(tracker.song.table[0].length==32);
    for(int i=0;i<20;++i)table_resize(NULL,(void *)(long)1);
    assert(tracker.song.table[0].length==256);
    for(int i=0;i<20;++i)table_resize(NULL,(void *)(long)-1);
    assert(tracker.song.table[0].length==16);
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
    tracker.focus=FOCUS_PHRASE;
    tracker.entry_octave=4;
    tracker.step=0;tracker.channel=1;tracker.field=0;
    TtCell *note_entry=&tracker.pattern.cell[0][1];
    assert(note_entry->note<0 && note_entry->instrument<0 &&
           note_entry->effect==TT_EFFECT_NONE);
    handle_key('c');
    assert(note_entry->note==64 && note_entry->instrument<0 &&
           note_entry->effect==TT_EFFECT_NONE && note_entry->value<0);
    note_entry->instrument=1;
    note_entry->effect=TT_EFFECT_M;note_entry->value=0x40;
    handle_key('x');
    assert(note_entry->instrument==1 && note_entry->effect==TT_EFFECT_M &&
           note_entry->value==0x40);
    tracker.channel=2;tracker.field=2;
    TtCell *effect_entry=&tracker.pattern.cell[0][2];
    handle_key('s');
    assert(effect_entry->effect==TT_EFFECT_S && effect_entry->value==-1);
    tracker.field=3;handle_key('4');
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
    order_panel_selected(&order_choice,NULL);
    assert(tracker.play_order==1 && tracker.play_mode==PLAY_SONG);
    assert(tracker.follow==0);
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
    assert(tracker.step==31);
    assert(tracker.follow==0);
    tracker.song.pattern_rows=16;
    tracker.step=0;

    F80Widget header={0},wave={0},grid={0};
    tracker.header=&header;
    tracker.wave_canvas=&wave;
    tracker.phrase_canvas=&grid;
    grid.rect=f80_rect(0,0,80,30);
    tracker.step=4;
    tracker.follow=1;
    handle_mouse(10,19,0); /* order 2 is visible, but not editable */
    assert(tracker.pattern_index==0 && tracker.step==4);
    assert(tracker.follow==1);
    assert(!begin_touch_selection(10,19));
    tracker.phrase_scroll=32;
    handle_mouse(10,7,0); /* repeated pattern in order 3 is also gray */
    assert(tracker.pattern_index==0 && tracker.step==4);
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
    tracker.pattern.cell[0][0]=(TtCell){-1,-1,TT_EFFECT_T,0};
    tracker.play_mode=PLAY_PATTERN;tracker.loop_pattern_index=1;
    tracker.song.table[0].tick[0].note=60;
    tracker.song.table[0].tick[0].instrument=1;
    tracker.song.table[0].tick[0].effect[0]=(TtTableEffect){'M',0x33};
    tracker.song.table[0].tick[0].effect[1]=(TtTableEffect){'U',2};
    tracker.song.table[0].tick[1].effect[0]=(TtTableEffect){'X',1};
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
    tracker.play_mode=PLAY_PATTERN;
    tracker.loop_pattern_index=tracker.pattern_index;
    tracker.pattern.cell[0][5]=(TtCell){-1,-1,TT_EFFECT_P,0};
    tt_play_play_row(0);
    assert(tt_audio_pan(5)==0);
    tracker.song.table[0].tick[6].effect[0]=(TtTableEffect){'P',255};
    tracker.table_voice[5]=(TableVoice){.active=1,.table=0,.tick=6,.next_table=-1};
    tt_play_table_ticks();
    assert(tt_audio_pan(5)==1);
    tracker.pattern.cell[0][5]=(TtCell){-1,-1,TT_EFFECT_STRETCH,0x40};
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
    int original_note=tracker.pattern.cell[0][4].note;
    TtEditEvent change={.type=TT_EDIT_CELL_SET};
    change.cell_set=(TtEditCellSet){0,4,{72,1,TT_EFFECT_NONE,-1}};
    assert(emit_edit(&change) && tracker.pattern.cell[0][4].note==72);
    handle_key(26); /* Ctrl-Z */
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
    view_items[4].action(NULL,NULL);
    assert(tracker.console_visible && root.window_role==F80_WINDOW_CONSOLE);
    assert(!strcmp(view_items[4].label,"~H~ide event console"));
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
    assert(!strcmp(view_items[4].label,"~S~how event console"));
    assert(!console.visible && header.visible && details.visible && status.visible);
    puts("default song, ordered grid, and playback follow passed");
    return 0;
}
