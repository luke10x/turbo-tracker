#define main tt_tracker_application_main
#include "turbo_tracker.c"
#undef main
#include <assert.h>
#include "../third_party/miniz/miniz.h"
static int header_clicks[5];
static void count_header_click(F80Widget *button,void *user) {
    (void)button;
    ++header_clicks[(long)user];
}

int main(void) {
    static const char expected_effect_picker_layout[6][7]={
        "123456","7890ET","AOINSR","HLDCUM","FYWGPB","VKXQJZ"
    };
    for(int row=0;row<6;++row)for(int column=0;column<6;++column)
        assert(effect_picker_layout[row][column]==
               expected_effect_picker_layout[row][column]);
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    char compact_order[16];
    order_panel_row_text(compact_order,sizeof(compact_order),0,0,1,1);
    assert(!strcmp(compact_order,"[x]00>00"));
    order_panel_row_text(compact_order,sizeof(compact_order),1,0,1,0);
    assert(!strcmp(compact_order,"[x]01 00"));
    unsetenv("INIT_PLAY");
    assert(!initial_play_requested());
    assert(setenv("INIT_PLAY","1",1)==0 && initial_play_requested());
    assert(setenv("INIT_PLAY","0",1)==0 && !initial_play_requested());
    assert(unsetenv("INIT_PLAY")==0);
    F80PointerGesture pointer_gesture={0};
    f80_pointer_gesture_begin(&pointer_gesture,4,7,100);
    assert(!f80_pointer_gesture_tick(&pointer_gesture,1099));
    assert(f80_pointer_gesture_move(&pointer_gesture,4,8,300)==F80_POINTER_MOVE);
    assert(!f80_pointer_gesture_tick(&pointer_gesture,2000));
    f80_pointer_gesture_end(&pointer_gesture);
    f80_pointer_gesture_begin(&pointer_gesture,10,10,5000);
    assert(f80_pointer_gesture_move(&pointer_gesture,11,10,5010)==F80_POINTER_MOVE);
    assert(!f80_pointer_gesture_tick(&pointer_gesture,7000));
    f80_pointer_gesture_end(&pointer_gesture);
    f80_pointer_gesture_begin(&pointer_gesture,400,700,2100);
    assert(f80_pointer_gesture_move(&pointer_gesture,401,701,2600)==F80_POINTER_MOVE);
    assert(!f80_pointer_gesture_tick(&pointer_gesture,3599));
    assert(f80_pointer_gesture_move(&pointer_gesture,410,710,3600)==F80_POINTER_MOVE);
    f80_pointer_gesture_end(&pointer_gesture);
    f80_pointer_gesture_begin(&pointer_gesture,4,7,3000);
    assert(!f80_pointer_gesture_tick(&pointer_gesture,3999));
    assert(f80_pointer_gesture_tick(&pointer_gesture,4000));
    pointer_gesture.selecting=1;
    assert(f80_pointer_gesture_move(&pointer_gesture,5,7,4010)==F80_POINTER_SELECT);
    f80_pointer_gesture_end(&pointer_gesture);
    char zip_dir[]="/tmp/tt-browse-XXXXXX";
    assert(mkdtemp(zip_dir));
    char zip_path[1024];assert(snprintf(zip_path,sizeof(zip_path),
        "%s/archive.zip",zip_dir)<(int)sizeof(zip_path));
    mz_zip_archive browse_zip={0};
    assert(mz_zip_writer_init_file(&browse_zip,zip_path,0));
    assert(mz_zip_writer_add_mem(&browse_zip,"root.wav","WAV",3,MZ_NO_COMPRESSION));
    assert(mz_zip_writer_add_mem(&browse_zip,"nested.zip","ZIP",3,MZ_NO_COMPRESSION));
    assert(mz_zip_writer_add_mem(&browse_zip,"drums/kick.wav","KCK",3,MZ_NO_COMPRESSION));
    assert(mz_zip_writer_add_mem(&browse_zip,"drums/loops/snare.wav","SNR",3,MZ_NO_COMPRESSION));
    assert(mz_zip_writer_finalize_archive(&browse_zip));
    assert(mz_zip_writer_end(&browse_zip));
    const F80FileBrowseApi *zip_browse=tt_zip_browse_api();
    F80FileEntry browse_entries[16];int browse_count=0,is_directory=0;
    unsigned long long browse_size=0;char browse_error[160],browse_subdir[1024];
    assert(zip_browse->stat(zip_browse->user,zip_path,&is_directory,&browse_size));
    assert(is_directory);
    assert(zip_browse->list(zip_browse->user,zip_path,browse_entries,16,
                            &browse_count,browse_error,sizeof(browse_error)));
    assert(browse_count==2);
    char nested_zip[1024];
    assert(snprintf(nested_zip,sizeof(nested_zip),"%s!nested.zip",zip_path)<
           (int)sizeof(nested_zip));
    assert(!zip_browse->stat(zip_browse->user,nested_zip,&is_directory,NULL));
    assert(zip_browse->join(zip_browse->user,zip_path,"drums",browse_subdir,
                            sizeof(browse_subdir)));
    assert(strstr(browse_subdir,".zip!drums"));
    assert(zip_browse->stat(zip_browse->user,browse_subdir,&is_directory,NULL) &&
           is_directory);
    assert(zip_browse->list(zip_browse->user,browse_subdir,browse_entries,16,
                            &browse_count,browse_error,sizeof(browse_error)));
    assert(browse_count==2);
    char browse_wav[1024],browse_parent[1024];
    assert(zip_browse->join(zip_browse->user,browse_subdir,"kick.wav",browse_wav,
                            sizeof(browse_wav)));
    assert(zip_browse->stat(zip_browse->user,browse_wav,&is_directory,&browse_size));
    assert(!is_directory && browse_size==3);
    assert(zip_browse->parent(zip_browse->user,browse_subdir,browse_parent,
                              sizeof(browse_parent)));
    char expected_parent[1024];
    assert(snprintf(expected_parent,sizeof(expected_parent),"%s!",zip_path)<
           (int)sizeof(expected_parent));
    assert(!strcmp(browse_parent,expected_parent));
    char syx_path[1024];
    assert(snprintf(syx_path,sizeof(syx_path),"%s/test.syx",zip_dir)<
           (int)sizeof(syx_path));
    uint8_t syx_bank[4104]={0xf0,0x43,0,9,32,0};
    for(int voice=0;voice<32;++voice) {
        memset(syx_bank+6+voice*128+118,' ',10);
        syx_bank[6+voice*128+118]=(uint8_t)('A'+voice%26);
    }
    unsigned syx_sum=0;
    for(int i=6;i<4102;++i)syx_sum+=syx_bank[i];
    syx_bank[4102]=(uint8_t)((-syx_sum)&127);syx_bank[4103]=0xf7;
    FILE *syx_file=fopen(syx_path,"wb");assert(syx_file);
    assert(fwrite(syx_bank,1,sizeof(syx_bank),syx_file)==sizeof(syx_bank));
    assert(!fclose(syx_file));
    const F80FileBrowseApi *syx_browse=tt_zip_syx_browse_api();
    assert(syx_browse->stat(syx_browse->user,syx_path,&is_directory,NULL) &&
           is_directory);
    F80FileEntry voices[32];int voice_count=0;
    assert(syx_browse->list(syx_browse->user,syx_path,voices,32,&voice_count,
                            browse_error,sizeof(browse_error)) && voice_count==32);
    assert(!strcmp(voices[5].name,"05 - F.syx"));
    char voice_path[1024];
    assert(syx_browse->join(syx_browse->user,syx_path,voices[5].name,
                            voice_path,sizeof(voice_path)));
    assert(strstr(voice_path,"test.syx!05 - F") &&
           syx_browse->stat(syx_browse->user,voice_path,&is_directory,NULL) &&
           !is_directory);
    assert(syx_browse->parent(syx_browse->user,voice_path,browse_parent,
                              sizeof(browse_parent)) && !strcmp(browse_parent,syx_path));
    char recent_path[]="/tmp/tt-recent-XXXXXX";
    int recent_fd=mkstemp(recent_path);assert(recent_fd>=0);close(recent_fd);unlink(recent_path);
    f80_file_dialog_recent_storage(recent_path);
    f80_file_dialog_recent_add(browse_wav);
    FILE *recent_file=fopen(recent_path,"r");assert(recent_file);
    char recent_line[1200];assert(fgets(recent_line,sizeof(recent_line),recent_file));
    fclose(recent_file);assert(strstr(recent_line,".zip!drums/kick.wav"));
    F80App recent_app={0};F80Widget *recent_root=f80_box(F80_COL);
    assert(recent_root);f80_set_root(&recent_app,recent_root);
    F80FileDialog recent_dialog={0};
    assert(f80_file_dialog_show_browse(&recent_dialog,&recent_app,F80_FILE_OPEN,
        "Recent test",zip_path,".wav",zip_browse,NULL,NULL));
    recent_dialog.recent_count=1;
    recent_dialog.recent_items[0]=browse_wav;
    recent_dialog.autocomplete.chosen(0,recent_dialog.autocomplete.user);
    assert(!strcmp(recent_dialog.directory,browse_subdir));
    assert(!strcmp(recent_dialog.input->input,"kick.wav"));
    f80_file_dialog_close(&recent_dialog);f80_free(recent_root);
    unlink(recent_path);unlink(syx_path);unlink(zip_path);rmdir(zip_dir);
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
    int mouse_x=-1,mouse_y=-1,mouse_wheel=0;mmask_t mouse_state=0;
    assert(native_decode_sgr_mouse("0;241;17",'M',
        &mouse_x,&mouse_y,&mouse_state,&mouse_wheel));
    assert(mouse_x==240 && mouse_y==16 && mouse_state==BUTTON1_PRESSED);
    assert(native_decode_sgr_mouse("0;241;17",'m',
        &mouse_x,&mouse_y,&mouse_state,&mouse_wheel));
    assert(mouse_x==240 && mouse_y==16 && mouse_state==BUTTON1_RELEASED);
    assert(native_decode_sgr_mouse("64;241;17",'M',
        &mouse_x,&mouse_y,&mouse_state,&mouse_wheel));
    assert(mouse_x==240 && mouse_y==16 && mouse_wheel==-1);
    assert(native_decode_sgr_mouse("65;241;17",'M',
        &mouse_x,&mouse_y,&mouse_state,&mouse_wheel));
    assert(mouse_x==240 && mouse_y==16 && mouse_wheel==1);
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
        assert(handle_ui_event((TtUiEvent){
            .type=TT_UI_CONTROLLER_SCREEN_NAV_START,.a=1}));
        assert(tracker.controller_screen_navigation &&
               tracker.app.focus==tracker.play_button);
        assert(handle_ui_event((TtUiEvent){
            .type=TT_UI_CONTROLLER_SCREEN_NAV_MOVE,.a=1}));
        assert(tracker.app.focus==tracker.stop_button);
        assert(handle_ui_event((TtUiEvent){
            .type=TT_UI_CONTROLLER_SCREEN_NAV_MOVE,.a=-1}));
        assert(tracker.app.focus==tracker.play_button);
        controller_navigation_activate();
        assert(header_clicks[0]==1 && tracker.controller_screen_navigation);
        handle_ui_event((TtUiEvent){.type=TT_UI_CONTROLLER_SCREEN_NAV_EXIT});
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

    F80App modal_app={0};
    F80Widget *background=f80_box(F80_COL);
    F80Widget *background_button=f80_add(background,f80_button("Background",NULL,NULL));
    F80Widget *modal=f80_box(F80_COL);
    F80Widget *modal_first=f80_add(modal,f80_button("First",NULL,NULL));
    F80Widget *modal_second=f80_add(modal,f80_button("Second",NULL,NULL));
    f80_set_root(&modal_app,background);
    assert(modal_app.focus==background_button);
    f80_show_modal(&modal_app,modal);
    assert(modal_app.focus==modal_first);
    f80_focus_next(&modal_app,1);
    assert(modal_app.focus==modal_second);
    f80_focus_next(&modal_app,1);
    assert(modal_app.focus==modal_first && modal_app.focus!=background_button);
    f80_hide_modal(&modal_app);
    assert(modal_app.focus==background_button);
    f80_free(background);

    F80App panel_app={0};
    F80Widget *panels=f80_box(F80_ROW);
    F80Widget *left_panel=f80_add(panels,f80_box(F80_COL));left_panel->frame=1;
    F80Widget *left_first=f80_add(left_panel,f80_button("Left 1",NULL,NULL));
    F80Widget *left_second=f80_add(left_panel,f80_button("Left 2",NULL,NULL));
    F80Widget *right_panel=f80_add(panels,f80_box(F80_COL));right_panel->frame=1;
    F80Widget *right_button=f80_add(right_panel,f80_button("Right",NULL,NULL));
    f80_set_root(&panel_app,panels);
    f80_layout(panels,f80_rect(0,0,30,8));
    panel_app.focus=left_first;
    f80_focus_next(&panel_app,1);
    assert(panel_app.focus==left_second); /* Focus loops inside the panel. */
    f80_focus_next(&panel_app,1);
    assert(panel_app.focus==left_first);
    assert(f80_focus_panel_next(&panel_app,1));
    assert(panel_app.focus==right_button); /* Option advances to the next pane. */
    assert(f80_focus_panel_next(&panel_app,-1));
    assert(panel_app.focus==left_first || panel_app.focus==left_second);
    f80_free(panels);

    F80App directional_app={0};
    F80Widget *directional_root=f80_box(F80_ROW);
    F80Widget *input_panel=f80_add(directional_root,f80_box(F80_COL));
    input_panel->frame=1;
    F80Widget *input=f80_add(input_panel,f80_input());
    F80Widget *input_button=f80_add(input_panel,f80_button("Apply",NULL,NULL));
    F80Widget *adjacent_panel=f80_add(directional_root,f80_box(F80_COL));
    adjacent_panel->frame=1;
    F80Widget *adjacent_button=f80_add(adjacent_panel,
        f80_button("Adjacent",NULL,NULL));
    f80_set_root(&directional_app,directional_root);
    input_panel->rect=f80_rect(0,0,20,8);
    input->rect=f80_rect(1,1,10,1);
    input_button->rect=f80_rect(1,3,10,1);
    adjacent_panel->rect=f80_rect(21,0,20,8);
    adjacent_button->rect=f80_rect(22,1,10,1);
    directional_app.focus=input;
    assert(f80_key(&directional_app,F80_KEY_LEFT)); /* Input captures caret keys. */
    assert(directional_app.focus==input);
    assert(f80_key(&directional_app,F80_KEY_DOWN));
    assert(directional_app.focus==input_button);
    assert(f80_key(&directional_app,F80_KEY_RIGHT));
    assert(directional_app.focus==input_button); /* Arrows stay within this pane. */
    directional_app.focus=input;
    assert(f80_key(&directional_app,27)); /* Negative exits a captive control. */
    assert(directional_app.focus==input_button);
    F80Widget *single_panel=f80_box(F80_COL);single_panel->frame=1;
    F80Widget *single_input=f80_add(single_panel,f80_input());
    F80App single_app={0};f80_set_root(&single_app,single_panel);
    single_app.focus=single_input;
    assert(f80_key(&single_app,27) && single_app.focus==single_input);
    f80_free(single_panel);f80_free(directional_root);

    const char *checkbox_items[]={"First","Second"};
    F80Widget *checkboxes=f80_check_group(checkbox_items,2,NULL,NULL);
    F80App checkbox_app={0};
    f80_set_root(&checkbox_app,checkboxes);
    checkbox_app.focus=checkboxes;
    assert(!f80_checked(checkboxes,0));
    assert(f80_key(&checkbox_app,27)); /* Negative toggles a checkbox in place. */
    assert(f80_checked(checkboxes,0) && checkbox_app.focus==checkboxes &&
           checkboxes->cursor==0);
    assert(f80_key(&checkbox_app,F80_KEY_DOWN) && checkboxes->cursor==1);
    assert(f80_key(&checkbox_app,27) && f80_checked(checkboxes,1));
    f80_free(checkboxes);

    const char *order_rows[]={"00o > 01p","01o > 02p"};
    F80Widget *list_color_root=f80_box(F80_ROW);
    F80Widget *blue_list=f80_add(list_color_root,
        f80_listbox(order_rows,2,NULL,NULL));
    blue_list->window_role=F80_WINDOW_BLUE;
    F80Widget *other_focus=f80_add(list_color_root,
        f80_button("Other",NULL,NULL));
    F80App list_color_app={0};f80_set_root(&list_color_app,list_color_root);
    list_color_app.focus=other_focus;
    assert(f80_listbox_row_pair(blue_list,0)==F80_PAIR_LIST_INACTIVE_SELECTED);
    assert(f80_listbox_row_pair(blue_list,1)==F80_PAIR_LIST_INACTIVE);
    /* Explicit exception to F80's default gray inactive-list colors. */
    f80_listbox_preserve_window_colors_when_unfocused(blue_list,1);
    assert(f80_listbox_row_pair(blue_list,0)==F80_COLOR_HIGHLIGHT);
    assert(f80_listbox_row_pair(blue_list,1)==F80_COLOR_TEXT);
    f80_free(list_color_root);

    F80App pane_owner_app={0};
    F80Widget *pane_owner_root=f80_box(F80_ROW);
    F80Widget *global_panel=f80_add(pane_owner_root,f80_box(F80_COL));
    global_panel->frame=1;global_panel->rect=f80_rect(0,0,10,6);
    F80Widget *global_focus_canvas=f80_add(global_panel,f80_canvas(NULL,NULL));
    global_focus_canvas->rect=f80_rect(8,1,6,3); /* Center crosses into adjacent pane. */
    F80Widget *operators_panel=f80_add(pane_owner_root,f80_box(F80_COL));
    operators_panel->frame=1;operators_panel->rect=f80_rect(11,0,10,6);
    F80Widget *operators_canvas=f80_add(operators_panel,f80_canvas(NULL,NULL));
    operators_canvas->rect=f80_rect(12,1,8,3);
    f80_set_root(&pane_owner_app,pane_owner_root);
    pane_owner_app.focus=global_focus_canvas;
    assert(f80_focused_panel(&pane_owner_app)==global_panel);
    assert(f80_key(&pane_owner_app,F80_KEY_RIGHT));
    assert(pane_owner_app.focus==global_focus_canvas &&
           f80_focused_panel(&pane_owner_app)==global_panel);
    f80_free(pane_owner_root);

    F80App nested_panel_app={0};
    F80Widget *nested_root=f80_box(F80_COL);
    F80Widget *wave_panel=f80_add(nested_root,f80_box(F80_COL));wave_panel->frame=1;
    F80Widget *wave_focus=f80_add(wave_panel,f80_button("Wave",NULL,NULL));
    F80Widget *nested_row=f80_add(nested_root,f80_box(F80_ROW));
    F80Widget *orders_panel=f80_add(nested_row,f80_box(F80_COL));orders_panel->frame=1;
    F80Widget *orders_focus=f80_add(orders_panel,f80_button("Orders",NULL,NULL));
    F80Widget *grid_panel=f80_add(nested_row,f80_box(F80_COL));grid_panel->frame=1;
    F80Widget *grid_focus=f80_add(grid_panel,f80_button("Grid",NULL,NULL));
    f80_set_root(&nested_panel_app,nested_root);
    wave_panel->rect=f80_rect(0,0,20,3);
    nested_row->rect=f80_rect(0,4,20,6);
    orders_panel->rect=f80_rect(0,4,5,6);
    grid_panel->rect=f80_rect(6,4,14,6);
    wave_focus->rect=wave_panel->rect;
    orders_focus->rect=orders_panel->rect;
    grid_focus->rect=grid_panel->rect;
    nested_panel_app.focus=grid_focus;
    assert(f80_focus_panel_next(&nested_panel_app,1));
    assert(nested_panel_app.focus==wave_focus);
    assert(f80_focus_panel_next(&nested_panel_app,1));
    assert(nested_panel_app.focus==orders_focus);
    assert(f80_focus_panel_next(&nested_panel_app,1));
    assert(nested_panel_app.focus==grid_focus); /* Wraps through the flex tree. */
    f80_free(nested_root);

    F80App layout_order_app={0};
    F80Widget *layout_order_root=f80_box(F80_COL);
    F80Widget *layout_order_panels[5];F80Widget *layout_order_controls[5];
    for(int i=0;i<5;++i) {
        layout_order_panels[i]=f80_add(layout_order_root,f80_box(F80_COL));
        layout_order_panels[i]->frame=1;
        layout_order_controls[i]=f80_add(layout_order_panels[i],
            f80_button("Pane",NULL,NULL));
    }
    f80_set_root(&layout_order_app,layout_order_root);
    /* Deliberately add in a different order from the visual flex layout. */
    layout_order_panels[0]->rect=f80_rect(0,0,80,3); /* Wave editor */
    layout_order_panels[1]->rect=f80_rect(0,5,30,3); /* Markers */
    layout_order_panels[2]->rect=f80_rect(30,5,30,3); /* Sample settings */
    layout_order_panels[3]->rect=f80_rect(0,10,25,3); /* Instrument */
    layout_order_panels[4]->rect=f80_rect(25,10,55,3); /* Instrument settings */
    for(int i=0;i<5;++i)layout_order_controls[i]->rect=layout_order_panels[i]->rect;
    layout_order_app.focus=layout_order_controls[3];
    assert(f80_focused_panel(&layout_order_app)==layout_order_panels[3]);
    assert(f80_focus_panel_next(&layout_order_app,1) &&
           layout_order_app.focus==layout_order_controls[4]);
    assert(f80_focused_panel(&layout_order_app)==layout_order_panels[4]);
    assert(f80_focus_panel_next(&layout_order_app,1) &&
           layout_order_app.focus==layout_order_controls[0]);
    assert(f80_focus_panel_next(&layout_order_app,1) &&
           layout_order_app.focus==layout_order_controls[1]);
    assert(f80_focus_panel_next(&layout_order_app,1) &&
           layout_order_app.focus==layout_order_controls[2]);
    assert(f80_focus_panel_next(&layout_order_app,1) &&
           layout_order_app.focus==layout_order_controls[3]);
    f80_free(layout_order_root);

    memset(&tracker,0,sizeof(tracker));
    tracker.perspective=TT_PERSPECTIVE_INSTRUMENTS;
    tracker.focus=FOCUS_WAVE;
    tracker.root=f80_box(F80_COL);
    tracker.instrument_panels=f80_add(tracker.root,f80_box(F80_COL));
    F80Widget *instrument_list_panel=f80_add(tracker.instrument_panels,
        f80_box(F80_COL));instrument_list_panel->frame=1;
    const char *instrument_items[]={"One","Two"};
    tracker.manage_list=f80_add(instrument_list_panel,
        f80_listbox(instrument_items,2,NULL,NULL));
    F80Widget *instrument_action_panel=f80_add(tracker.instrument_panels,
        f80_box(F80_COL));instrument_action_panel->frame=1;
    F80Widget *instrument_action=f80_add(instrument_action_panel,
        f80_button("Action",NULL,NULL));
    f80_set_root(&tracker.app,tracker.root);
    f80_layout(tracker.root,f80_rect(0,0,40,12));
    tracker.manage_list->cursor=1;
    handle_key(9);
    assert(tracker.app.focus==instrument_action);
    handle_ui_event((TtUiEvent){.type=TT_UI_INSTRUMENT_FOCUS_RECONCILE});
    assert(tracker.app.focus==instrument_action); /* Reconcile preserves pane focus. */
    handle_key(9);
    assert(tracker.app.focus==tracker.manage_list); /* Tab wraps to the first pane. */
    handle_key(F80_KEY_DOWN);
    assert(tracker.app.focus==tracker.manage_list && tracker.manage_list->cursor==1);
    f80_free(tracker.root);

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
    handle_key( ' ');
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
    assert(setenv("INIT_SONG","examples/time-stretch/time-stretch.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.pattern_count==1 && tracker.song.order_count==1 &&
           tracker.song.pattern_rows==64 && tracker.song.bpm==110 &&
           tracker.sample_count==4 && tracker.wave.pcm &&
           tracker.song.pattern[0].cell[0][0].effect==TT_EFFECT_R &&
           tracker.song.pattern[0].cell[0][0].value==0x80 &&
           tracker.song.pattern[0].cell[8][0].effect==TT_EFFECT_R &&
           tracker.song.pattern[0].cell[8][0].value==0x00 &&
           tracker.song.pattern[0].cell[32][0].effect==TT_EFFECT_R &&
           tracker.song.pattern[0].cell[32][0].value==0xff);
    F80Widget instrument_list={.cursor=1},instrument_name={0};
    tracker.manage_list=&instrument_list;tracker.manage_name=&instrument_name;
    char expected_instrument[64];
    snprintf(expected_instrument,sizeof(expected_instrument),"%s",
             tracker.samples[1].wave.name);
    manage_selected(&instrument_list,NULL);
    assert(tracker.sample_index==1 && instrument_list.cursor==1 &&
           !strcmp(tracker.wave.name,expected_instrument));
    instrument_list.cursor=0;manage_selected(&instrument_list,NULL);
    assert(tracker.sample_index==0 && instrument_list.cursor==0);
    tracker.manage_list=NULL;tracker.manage_name=NULL;
    assert(setenv("INIT_SONG","examples/glide/glide.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.pattern_count==3 && tracker.song.order_count==3 &&
           tracker.song.pattern_rows==32 && tracker.song.bpm==180 &&
           tracker.song.order[0]==0 && tracker.song.order[1]==1 &&
           tracker.song.order[2]==2 &&
           tracker.sample_count==1 && tracker.wave.pcm &&
           tracker.wave.frames==110239 && tracker.wave.rate==44100 &&
           tracker.song.pattern[0].cell[0][0].note==60 &&
           tracker.song.pattern[0].cell[0][0].effect==TT_EFFECT_NONE &&
           tracker.song.pattern[0].cell[2][0].note==64 &&
           tracker.song.pattern[0].cell[2][0].effect==TT_EFFECT_NONE &&
           tracker.song.pattern[1].cell[0][0].note==60 &&
           tracker.song.pattern[1].cell[2][0].note==64 &&
           tracker.song.pattern[1].cell[2][0].effect==TT_EFFECT_G &&
           tracker.song.pattern[1].cell[2][0].value==0x04 &&
           tracker.song.pattern[2].cell[2][0].effect==TT_EFFECT_G &&
           tracker.song.pattern[2].cell[2][0].value==0x08);
    assert(setenv("INIT_SONG","examples/slc-scale/scale.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.pattern_count==1 && tracker.song.pattern_rows==16 &&
           tracker.sample_count==1 && tracker.phrase.slice_note_offset[8]==12 &&
           tracker.song.pattern[0].cell[0][0].note==72 &&
           tracker.song.pattern[0].cell[0][0].effect==TT_EFFECT_S &&
           tracker.song.pattern[0].cell[0][0].value==0x8b &&
           tracker.song.pattern[0].cell[14][0].note==84);
    assert(setenv("INIT_SONG","examples/slc-break/break.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.pattern_rows==64 && tracker.song.bpm==144 &&
           tracker.song.order_count==1 && tracker.sample_count==1 &&
           tracker.phrase.marker[1]-tracker.phrase.marker[0]==2294 &&
           tracker.song.pattern[0].cell[0][0].value==0x01 &&
           tracker.song.pattern[0].cell[14][0].value==0xef &&
           tracker.song.pattern[0].cell[15][0].value==0x0f &&
           tracker.song.pattern[0].cell[30][0].value==0xf0 &&
           tracker.song.pattern[0].cell[45][0].value==0xcb &&
           tracker.song.pattern[0].cell[46][0].value==0x23 &&
           tracker.song.pattern[0].cell[50][0].value==0xfd &&
           tracker.song.pattern[0].cell[55][0].value==0xed &&
           tracker.song.pattern[0].cell[59][0].value==0x10);
    TtCell full_break=tracker.song.pattern[0].cell[15][0];
    assert(full_break.effect==TT_EFFECT_S && full_break.value==0x0f);
    tracker.phrase.slice_note_offset[0]=-24; /* tone detection result */
    assert(
           tt_play_cell_reference_note(&tracker.phrase,&full_break)==
               tracker.phrase.reference_note);
    TtCell detected_slice=full_break;detected_slice.value=0x01;
    assert(tt_play_cell_reference_note(&tracker.phrase,&detected_slice)==
           tt_slice_base_note(&tracker.phrase,0));
    assert(setenv("INIT_SONG","examples/tbl-arpeggio/tbl-arpeggio.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.table[0].tick[0].note==60 &&
           tracker.song.table[0].tick[4].note==64 &&
           tracker.song.table[0].tick[8].note==67 &&
           tracker.song.table[0].tick[12].note==72);
    assert(setenv("INIT_SONG","examples/tbl-relative-notes/tbl-relative-notes.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.table[0].tick[0].note==tt_note_relative(1,0) &&
           tracker.song.table[0].tick[4].note==tt_note_relative(1,4) &&
           tracker.song.pattern[0].cell[4][0].note==62);
    assert(setenv("INIT_SONG","examples/chord-voicings/chord-voicings.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.pattern[0].cell[0][1].note==tt_note_left_relative(4) &&
           tracker.song.pattern[0].cell[0][2].note==tt_note_left_relative(3));
    assert(setenv("INIT_SONG","examples/relative-note-steps/relative-note-steps.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.pattern[0].cell[2][0].note==tt_note_relative(1,1) &&
           tracker.song.pattern[0].cell[6][0].note==tt_note_relative(0,2));
    assert(setenv("INIT_SONG","examples/tbl-loop/tbl-loop.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.table[0].tick[11].effect[0].command=='Y' &&
           tracker.song.table[0].tick[11].effect[0].value==4 &&
           tracker.song.pattern[0].cell[8][0].note==TT_NOTE_OFF);
    assert(setenv("INIT_SONG","examples/tbl-retrigger/tbl-retrigger.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.table[0].tick[3].effect[0].command=='X' &&
           tracker.song.table[0].tick[3].effect[0].value==0);
    assert(setenv("INIT_SONG","examples/tbl-automation/tbl-automation.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.table[0].tick[1].effect[0].command=='M' &&
           tracker.song.table[0].tick[1].effect[1].command=='P' &&
           tracker.song.table[0].tick[1].effect[2].command=='V' &&
           tracker.song.table[0].tick[4].effect[3].command=='S' &&
           tracker.song.table[0].tick[4].effect[3].value==0x5a);
    assert(setenv("INIT_SONG","examples/udl/udl.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.pattern_count==3 && tracker.song.order_count==3 &&
           tracker.song.bpm==80 && tracker.song.pattern_rows==16 &&
           tracker.song.pattern[0].cell[0][0].effect==TT_EFFECT_S &&
           tracker.song.pattern[1].cell[8][0].effect3==TT_EFFECT_U &&
           tracker.song.pattern[1].cell[8][0].value3==0x80 &&
           tracker.song.pattern[2].cell[0][0].effect==TT_EFFECT_T &&
           tracker.song.table[1].tick[8].effect[1].command=='U' &&
           tracker.song.table[1].tick[8].effect[1].value==0x80);
    assert(setenv("INIT_SONG","examples/ztm/ztm.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.bpm==80 && tracker.song.ticks_per_row==12 &&
           tracker.song.pattern[0].cell[0][0].effect==TT_EFFECT_Z &&
           tracker.song.pattern[0].cell[0][0].value==0x78 &&
           tracker.song.pattern[0].cell[4][0].value==0 &&
           tracker.song.table[0].tick[0].effect[0].command=='Z' &&
           tracker.song.table[0].tick[0].effect[0].value==3 &&
           tracker.song.table[0].tick[8].effect[0].value==12);
    assert(setenv("INIT_SONG","examples/register-reference/register.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.pattern[0].cell[0][0].effect=='0' &&
           tracker.song.pattern[0].cell[0][0].value==0x30 &&
           tracker.song.pattern[0].cell[0][0].effect2==TT_EFFECT_M &&
           tracker.song.pattern[0].cell[0][0].value2==
               tt_effect_register_value(0) &&
           tracker.song.pattern[0].cell[4][0].value==0x80 &&
           tracker.song.pattern[0].cell[4][0].value2==
               tt_effect_register_value(0));
    const char *effect_examples[]={
        "examples/iin-fade-in/iin.ttt",
        "examples/pitch-bends/pitch-bends.ttt",
        "examples/mvl-volume/mvl.ttt",
        "examples/npn-auto-pan/npn.ttt",
        "examples/out-fade-out/out.ttt",
        "examples/pan-position/pan.ttt",
        "examples/vib-vibrato/vib.ttt",
        "examples/wbl-tremolo/wbl.ttt",
        "examples/xjp-pattern-jump/xjp.ttt",
        "examples/yrj-pattern-row-jump/yrj.ttt"
    };
    for(size_t i=0;i<sizeof(effect_examples)/sizeof(effect_examples[0]);++i) {
        assert(setenv("INIT_SONG",effect_examples[i],1)==0);
        assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    }
    assert(tracker.song.pattern[0].cell[1][0].effect==TT_EFFECT_Y &&
           tracker.song.pattern[0].cell[1][0].value==8 &&
           tracker.song.pattern[0].cell[8][0].note==64);
    assert(setenv("INIT_SONG","examples/xjp-pattern-jump/xjp.ttt",1)==0);
    assert(load_default_song("build/macos/bin/test-sample-tracker-timeline"));
    assert(tracker.song.pattern_count==3 && tracker.song.order_count==3 &&
           tracker.song.pattern[0].cell[4][0].effect==TT_EFFECT_X &&
           tracker.song.pattern[0].cell[4][0].value==2);
    assert(unsetenv("INIT_SONG")==0);
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
    for(int i=0;i<TT_TABLE_MAX_TICKS;++i)table_resize(NULL,(void *)(long)1);
    assert(tracker.song.table[0].length==TT_TABLE_MAX_TICKS);
    for(int i=0;i<TT_TABLE_MAX_TICKS;++i)table_resize(NULL,(void *)(long)-1);
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
    tracker.pattern_index=0;tracker.channel=0;tracker.field=2;
    tracker.focus=FOCUS_PHRASE;
    tracker.pattern.cell[0][0].effect2=TT_EFFECT_M;
    tracker.pattern.cell[0][0].value2=0x45;
    tracker.song.pattern[1].cell[0][0].effect3=TT_EFFECT_P;
    tracker.song.pattern[1].cell[0][0].value3=0x67;
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,
        .a=1,.b=0}));
    assert(tracker.pattern.cell[0][0].effect==TT_EFFECT_M &&
           tracker.pattern.cell[0][0].value==0x45 &&
           tracker.pattern.cell[0][0].effect2==TT_EFFECT_NONE &&
           tracker.song.pattern[1].cell[0][0].effect==TT_EFFECT_P &&
           tracker.song.pattern[1].cell[0][0].value==0x67 &&
           tracker.song.pattern[1].cell[0][0].effect3==TT_EFFECT_NONE);
    tracker.focus=FOCUS_PHRASE;tracker.field=2;
    tracker.pattern.cell[1][0].effect3=TT_EFFECT_R;
    tracker.pattern.cell[1][0].value3=0x89;
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_FOCUS,
        .index=FOCUS_WAVE}));
    assert(tracker.pattern.cell[1][0].effect==TT_EFFECT_R &&
           tracker.pattern.cell[1][0].value==0x89 &&
           tracker.pattern.cell[1][0].effect3==TT_EFFECT_NONE);
    tracker.focus=FOCUS_PHRASE;tracker.channel=0;tracker.field=0;
    tracker.step=tracker.song.pattern_rows-1;
    tracker.phrase_scroll=4;
    tracker.pattern_lock=0;tracker.play_order=0;tracker.active_order_index=0;
    move_pattern_row(1);
    assert(tracker.pattern_index==1 && tracker.edit_order==1 && tracker.step==0 &&
           tracker.play_order==0 && tracker.active_order_index==0);
    assert(tracker.phrase_scroll==5); /* Row 00 stays at the former row 15 screen position. */
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
    handle_key(' ');handle_key('q');
    assert(note_entry->instrument==1 && note_entry->effect==TT_EFFECT_M &&
           note_entry->value==0x40);
    tracker.channel=2;tracker.field=2;
    TtCell *effect_entry=&tracker.pattern.cell[0][2];
    handle_key('s');
    assert(effect_entry->effect==TT_EFFECT_S && effect_entry->value==-1);
    handle_key(' ');handle_key('4');
    assert(effect_entry->value==4);
    tracker.step=1;tracker.field=2;
    TtCell *register_input=&tracker.pattern.cell[1][2];
    handle_key('0');
    tracker.field=3;handle_key('3');handle_key('0');
    tracker.field=4;handle_key('m');
    tracker.field=5;handle_key('$');handle_key('0');
    assert(register_input->effect=='0' && register_input->value==0x30 &&
           register_input->effect2==TT_EFFECT_M &&
           register_input->value2==tt_effect_register_value(0));
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
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_AUDIO_RECOVER}));
    tt_play_flush();
    assert(tracker.audio_recovery_result==1);
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
    assert(tracker.focus==FOCUS_WAVE);
    handle_key(9);
    assert(tracker.focus==FOCUS_WAVE && current_status()==perspective_status_visible);
    assert(!strcmp(current_status()[0].label,"Focus") &&
           !strcmp(current_status()[1].label,"Option") &&
           !strcmp(current_status()[2].label,"Forward") &&
           !strcmp(current_status()[3].label,"Back"));
    int saved_marker=tracker.marker,saved_view=tracker.view_start;
    int saved_instrument_type=tracker.wave.instrument_type;
    tracker.marker=0;tracker.wave.instrument_type=TT_INSTRUMENT_SAMPLE;
    handle_key(2001);
    assert(tracker.marker==1);
    tracker.marker=TT_MARKERS-1;
    handle_key(2001);
    assert(tracker.marker==0);
    tracker.marker=saved_marker;tracker.view_start=saved_view;
    tracker.wave.instrument_type=saved_instrument_type;
    tracker.focus=FOCUS_ORDER;
    assert(strcmp(current_status()[0].label,"Focus")==0);
    tracker.play_mode=PLAY_PATTERN;
    assert(strcmp(current_status()[1].label,"Option")==0);
    int saved_field=tracker.field;
    tracker.focus=FOCUS_PHRASE;tracker.field=0;
    assert(strcmp(current_status()[0].label,"Focus")==0 &&
           strcmp(current_status()[3].label,"Back")==0);
    tracker.field=1;
    assert(strcmp(current_status()[0].label,"Focus")==0);
    tracker.field=2;
    assert(strcmp(current_status()[0].label,"Focus")==0);
    tracker.field=3;
    assert(strcmp(current_status()[0].label,"Focus")==0);
    tracker.field=saved_field;tracker.focus=FOCUS_ORDER;
    F80Widget fake_wave_row={.visible=1},fake_grid_row={.visible=1},
        fake_phrase_frame={.visible=1};
    F80Widget fake_instrument_panels={.visible=0},fake_order_frame={.visible=1};
    F80Widget fake_wave_frame={.visible=1};
    F80Widget fake_wave_canvas={.visible=1},fake_order_dialog={.visible=0};
    F80Widget fake_manage_dialog={.visible=1},fake_slices_dialog={.visible=1};
    tracker.wave_row=&fake_wave_row;tracker.grid_row=&fake_grid_row;
    tracker.phrase_frame=&fake_phrase_frame;
    tracker.instrument_panels=&fake_instrument_panels;tracker.order_frame=&fake_order_frame;
    tracker.wave_frame=&fake_wave_frame;
    tracker.wave_canvas=&fake_wave_canvas;tracker.order_dialog=&fake_order_dialog;
    tracker.manage_dialog=&fake_manage_dialog;tracker.slices_dialog=&fake_slices_dialog;
    tracker.order_list=&order_choice;tracker.playing=1;
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_SET_PERSPECTIVE,
                                       .a=TT_PERSPECTIVE_ORDERS}));
    assert(fake_order_dialog.visible && !fake_phrase_frame.visible &&
           fake_wave_canvas.visible==0 && tracker.playing);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_SET_PERSPECTIVE,
                                       .a=TT_PERSPECTIVE_INSTRUMENTS}));
    assert(fake_instrument_panels.visible && !fake_order_frame.visible &&
           !fake_order_dialog.visible && tracker.playing);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_SET_PERSPECTIVE,
                                       .a=TT_PERSPECTIVE_MAIN}));
    tracker.app.modal=NULL;tracker.console_visible=0;
    handle_key(';');handle_key('x');
    assert(!tracker.leader_pending &&
           tracker.perspective==TT_PERSPECTIVE_MAIN && tracker.playing);
    tracker.wave_row=tracker.grid_row=tracker.phrase_frame=tracker.instrument_panels=NULL;
    tracker.order_frame=tracker.wave_frame=tracker.wave_canvas=tracker.order_dialog=NULL;
    tracker.manage_dialog=tracker.slices_dialog=NULL;
    tracker.playing=0;
    tracker.focus=FOCUS_PHRASE;
    handle_key(';');
    assert(tracker.leader_pending);
    handle_key(27);
    assert(!tracker.leader_pending);
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
    assert(strcmp(current_status()[0].label,"Focus")==0);
    tracker.order_list=&order_choice;
    tracker.edit_order=0;
    tracker.phrase_scroll=0;
    tracker.play_order=0;
    tracker.active_order_index=0;
    handle_key(9);
    assert(tracker.focus==FOCUS_ORDER);
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
    tracker.inline_table_selected=0;
    tracker.grid_value_editing=0;
    tracker.selecting=tracker.visual_selecting=0;
    tracker.step=10;tracker.channel=0;tracker.field=0;
    handle_key(F80_KEY_FOCUS); /* Focus no longer advances grid subcells. */
    assert(tracker.field==0 && tracker.channel==0);
    handle_key(9); /* Option/Tab no longer advances the grid subcell. */
    assert(tracker.field==0 && tracker.channel==0);
    handle_key(F80_KEY_SHIFT_RIGHT); /* Focus+arrow selects in the grid. */
    assert(tracker.field==0 && tracker.visual_selecting && tracker.selecting);
    handle_key(27);
    assert(!tracker.visual_selecting && !tracker.selecting);
    tracker.channel=0;tracker.field=0;
    handle_key(' '); /* Space advances directly to the next grid subcell. */
    assert(tracker.field==1 && tracker.channel==0);
    handle_key(F80_KEY_SHIFT_DOWN); /* Shift+arrow is keyboard visual select. */
    assert(tracker.visual_selecting && tracker.selecting && tracker.step==11);
    handle_key(27);
    assert(!tracker.visual_selecting && !tracker.selecting);
    TtCell effect_width_cell=tracker.pattern.cell[0][0];
    int effect_width_edit_order=tracker.edit_order;
    int effect_width_channel=tracker.channel,effect_width_field=tracker.field;
    tracker.edit_order=-1;tracker.channel=0;tracker.field=0;
    tracker.pattern.cell[0][0].effect=TT_EFFECT_R;
    tracker.pattern.cell[0][0].value=0;
    tracker.pattern.cell[0][0].effect2=TT_EFFECT_NONE;
    tracker.pattern.cell[0][0].value2=-1;
    tracker.pattern.cell[0][0].effect3=TT_EFFECT_NONE;
    tracker.pattern.cell[0][0].value3=-1;
    assert(pattern_effect_slots_used(0)==1);
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,.a=0,.b=2});
    assert(pattern_effect_slots_visible(0)==2); /* Trigger opens one spare. */
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,.a=0,.b=4});
    assert(pattern_effect_slots_visible(0)==2); /* Spare remains while visited. */
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,.a=0,.b=0});
    assert(pattern_effect_slots_visible(0)==1); /* Empty spare collapses. */
    tracker.pattern.cell[0][0].effect2='G';
    tracker.pattern.cell[0][0].value2=0;
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,.a=0,.b=4});
    assert(pattern_effect_slots_used(0)==2 && pattern_effect_slots_visible(0)==3);
    handle_ui_event((TtUiEvent){.type=TT_UI_PATTERN_CURSOR,.a=0,.b=0});
    assert(pattern_effect_slots_visible(0)==2); /* Used effect column remains. */
    tracker.pattern.cell[0][0]=effect_width_cell;
    tracker.edit_order=effect_width_edit_order;
    tracker.channel=effect_width_channel;tracker.field=effect_width_field;
    F80Widget *keyboard_panels=f80_box(F80_ROW);
    F80Widget *keyboard_left=f80_add(keyboard_panels,f80_box(F80_COL));
    keyboard_left->frame=1;
    F80Widget *keyboard_left_focus=f80_add(keyboard_left,
        f80_button("Orders",NULL,NULL));
    F80Widget *keyboard_right=f80_add(keyboard_panels,f80_box(F80_COL));
    keyboard_right->frame=1;
    F80Widget *keyboard_grid_focus=f80_add(keyboard_right,
        f80_button("Grid",NULL,NULL));
    tracker.perspective=TT_PERSPECTIVE_MAIN;
    f80_set_root(&tracker.app,keyboard_panels);
    f80_layout(keyboard_panels,f80_rect(0,0,30,8));
    tracker.app.focus=keyboard_grid_focus;
    handle_key(9); /* A short Option/Tab press switches panes immediately. */
    assert(tracker.app.focus==keyboard_left_focus);
    f80_set_root(&tracker.app,NULL);
    f80_free(keyboard_panels);
    tracker.step=10;tracker.channel=0;tracker.field=0;
    tracker.pattern.cell[10][0].note=48;
    tracker.playing=0;
    handle_key(13);
    assert(tracker.grid_value_editing && !tracker.playing);
    tracker.app.tick=0;assert(grid_value_edit_pair()==7);
    tracker.app.tick=9;assert(grid_value_edit_pair()==3);
    tracker.app.tick=0;
    handle_key(F80_KEY_UP);handle_key(F80_KEY_RIGHT);
    assert(tracker.pattern.cell[10][0].note==61 && !tracker.playing);
    handle_key(F80_KEY_DOWN);handle_key(F80_KEY_LEFT);
    assert(tracker.pattern.cell[10][0].note==48);
    handle_key(13);
    assert(!tracker.grid_value_editing && !tracker.playing);
    tracker.field=1;tracker.pattern.cell[10][0].instrument=0x22;
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_GRID_VALUE_EDITING,.a=1}));
    handle_key(F80_KEY_UP);handle_key(F80_KEY_LEFT);
    assert(tracker.pattern.cell[10][0].instrument==0x31);
    handle_key(F80_KEY_DOWN);handle_key(F80_KEY_RIGHT);
    assert(tracker.pattern.cell[10][0].instrument==0x22);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_GRID_VALUE_EDITING,.a=0}));
    tracker.field=3;
    tracker.pattern.cell[10][0].effect='M';
    tracker.pattern.cell[10][0].value=0x20;
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_GRID_VALUE_EDITING,.a=1}));
    handle_key(F80_KEY_UP);handle_key(F80_KEY_RIGHT);
    assert(tracker.pattern.cell[10][0].value==0x31);
    handle_key(27);
    assert(tracker.pattern.cell[10][0].value==-1 &&
           !tracker.grid_value_editing && !tracker.playing);
    memset(&controller_input,0,sizeof(controller_input));
    tracker.field=0;tracker.pattern.cell[10][0].note=48;tracker.playing=1;
    controller_button_input(SDL_GAMEPAD_BUTTON_SOUTH,1);
    controller_button_input(SDL_GAMEPAD_BUTTON_DPAD_UP,1);
    controller_button_input(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,1);
    controller_button_input(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,1);
    assert(tracker.grid_value_editing && tracker.pattern.cell[10][0].note==60 &&
           tracker.playing);
    controller_button_input(SDL_GAMEPAD_BUTTON_SOUTH,0);
    assert(!tracker.grid_value_editing && tracker.playing);
    memset(&controller_input,0,sizeof(controller_input));
    tracker.pattern.cell[10][0].note=48;
    controller_raw_button(0,1); /* Unmapped Xbox fallback: A */
    controller_raw_hat(SDL_HAT_UP);
    controller_raw_button(4,1); /* LB */
    controller_raw_button(5,1); /* RB */
    assert(tracker.grid_value_editing && tracker.pattern.cell[10][0].note==60 &&
           tracker.playing);
    controller_raw_button(0,0);
    controller_raw_hat(SDL_HAT_CENTERED);
    memset(&controller_input,0,sizeof(controller_input));
    static const F80MenuItem controller_menu_one[]={
        {"One",NULL,count_header_click,1},{NULL,NULL,NULL,0},
        {"Two",NULL,count_header_click,1}};
    static const F80MenuItem controller_menu_two[]={
        {"Three",NULL,count_header_click,1}};
    static const F80Menu controller_menus[]={
        {"First",controller_menu_one,3},{"Second",controller_menu_two,1}};
    F80Widget *controller_root=f80_box(F80_COL);
    tracker.app.menu=f80_add(controller_root,
        f80_menubar(controller_menus,2));
    controller_button_input(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,1);
    assert(tracker.app.menu->menu_active && tracker.app.menu->menu_index==0 &&
           tracker.app.menu->menu_item==0);
    controller_button_input(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,0);
    controller_button_input(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,1);
    assert(tracker.app.menu->menu_index==0 && tracker.app.menu->menu_item==2);
    controller_button_input(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,0);
    controller_button_input(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,1);
    assert(tracker.app.menu->menu_index==1 && tracker.app.menu->menu_item==0);
    controller_button_input(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,0);
    f80_menu_close(tracker.app.menu);
    F80Widget fake_controller_play={.action=count_header_click,.user=(void *)2};
    F80Widget fake_controller_stop={.action=count_header_click,.user=(void *)2};
    tracker.play_button=&fake_controller_play;
    tracker.stop_button=&fake_controller_stop;
    header_clicks[2]=0;
    controller_trigger_input(1,1);
    assert(tracker.controller_toolbar_mode &&
           tracker.controller_toolbar_index==0);
    controller_button_input(SDL_GAMEPAD_BUTTON_SOUTH,1);
    assert(tracker.controller_toolbar_mode && header_clicks[2]==1);
    controller_trigger_input(1,0);
    controller_trigger_input(1,1);
    assert(tracker.controller_toolbar_mode &&
           tracker.controller_toolbar_index==1);
    controller_button_input(SDL_GAMEPAD_BUTTON_SOUTH,0);
    controller_button_input(SDL_GAMEPAD_BUTTON_SOUTH,1);
    assert(tracker.controller_toolbar_mode && header_clicks[2]==2);
    controller_trigger_input(1,0);
    controller_navigation_exit();
    tracker.play_button=NULL;tracker.stop_button=NULL;tracker.app.menu=NULL;
    memset(&controller_input,0,sizeof(controller_input));
    tracker.controller_screen_navigation=0;
    tracker.controller_toolbar_mode=0;
    F80Widget *controller_wave=f80_add(controller_root,f80_box(F80_COL));
    controller_wave->frame=1;controller_wave->rect=f80_rect(0,0,20,3);
    F80Widget *controller_wave_item=f80_add(controller_wave,
        f80_button("Wave",NULL,NULL));controller_wave_item->rect=controller_wave->rect;
    F80Widget *controller_row=f80_add(controller_root,f80_box(F80_ROW));
    F80Widget *controller_orders=f80_add(controller_row,f80_box(F80_COL));
    controller_orders->frame=1;controller_orders->rect=f80_rect(0,4,5,6);
    F80Widget *controller_order_item=f80_add(controller_orders,
        f80_button("Orders",NULL,NULL));controller_order_item->rect=controller_orders->rect;
    F80Widget *controller_grid=f80_add(controller_row,f80_box(F80_COL));
    controller_grid->frame=1;controller_grid->rect=f80_rect(6,4,14,6);
    F80Widget *controller_grid_item=f80_add(controller_grid,
        f80_button("Grid",NULL,NULL));controller_grid_item->rect=controller_grid->rect;
    tracker.wave_frame=controller_wave;tracker.order_frame=controller_orders;
    tracker.phrase_frame=controller_grid;tracker.perspective=TT_PERSPECTIVE_MAIN;
    tracker.focus=FOCUS_PHRASE;tracker.step=10;tracker.channel=0;tracker.field=0;
    tracker.visual_selecting=tracker.selecting=0;
    f80_set_root(&tracker.app,controller_root);tracker.app.focus=controller_grid_item;
    controller_button_input(SDL_GAMEPAD_BUTTON_WEST,1);
    controller_button_input(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,1);
    assert(tracker.visual_selecting && tracker.selecting); /* Focus + Right */
    controller_button_input(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,0);
    controller_button_input(SDL_GAMEPAD_BUTTON_WEST,0);
    handle_key(27);
    controller_button_input(SDL_GAMEPAD_BUTTON_NORTH,1);
    assert(tracker.focus==FOCUS_WAVE && tracker.app.focus==controller_wave_item);
    controller_button_input(SDL_GAMEPAD_BUTTON_NORTH,0);
    controller_button_input(SDL_GAMEPAD_BUTTON_NORTH,1);
    assert(tracker.focus==FOCUS_ORDER && tracker.app.focus==controller_order_item);
    controller_button_input(SDL_GAMEPAD_BUTTON_NORTH,0);
    controller_button_input(SDL_GAMEPAD_BUTTON_NORTH,1);
    assert(tracker.app.focus==controller_grid_item);
    controller_button_input(SDL_GAMEPAD_BUTTON_NORTH,0);
    controller_button_input(SDL_GAMEPAD_BUTTON_NORTH,1);
    assert(tracker.focus==FOCUS_WAVE && tracker.app.focus==controller_wave_item);
    controller_button_input(SDL_GAMEPAD_BUTTON_NORTH,0);
    tracker.wave_frame=tracker.order_frame=tracker.phrase_frame=NULL;
    f80_set_root(&tracker.app,NULL);f80_free(controller_root);
    memset(&controller_input,0,sizeof(controller_input));
    tracker.focus=FOCUS_PHRASE;
    const char *nav_items[]={"00","01","02"};
    F80Widget *nav_root=f80_box(F80_COL);
    F80Widget *selector=f80_add(nav_root,f80_box(F80_COL));
    F80Widget *selector_sample_list=f80_add(selector,
        f80_listbox(nav_items,3,NULL,NULL));
    F80Widget *marker_list=f80_add(selector,
        f80_listbox(nav_items,3,NULL,NULL));
    F80Widget *selector_button=f80_add(selector,f80_button("Apply",NULL,NULL));
    f80_set_root(&tracker.app,nav_root);
    tracker.perspective=TT_PERSPECTIVE_INSTRUMENTS;
    tracker.focus=FOCUS_WAVE;
    tracker.instrument_selector_frame=selector;
    tracker.manage_list=selector_sample_list;
    tracker.slices_list=marker_list;
    selector_sample_list->cursor=1;
    tracker.app.focus=selector_button;
    handle_key(F80_KEY_DOWN);
    assert(selector_sample_list->cursor==1); /* arrows only affect focused list */
    assert(f80_listbox_row_pair(selector_sample_list,1)==
           F80_PAIR_LIST_INACTIVE_SELECTED);
    assert(f80_listbox_row_pair(selector_sample_list,0)==
           F80_PAIR_LIST_INACTIVE);
    tracker.app.focus=selector_sample_list;
    assert(f80_listbox_row_pair(selector_sample_list,1)==F80_PAIR_LIST_SELECTED);
    assert(f80_listbox_row_pair(selector_sample_list,0)==2);
    handle_key(F80_KEY_DOWN);
    assert(selector_sample_list->cursor==2);
    controller_action_input(TT_PAD_FOCUS,1);
    controller_action_input(TT_PAD_FOCUS,0);
    assert(tracker.app.focus==marker_list); /* Focus cycles controls in this panel. */
    f80_focus_next(&tracker.app,-1);
    assert(tracker.app.focus==selector_sample_list);
    controller_action_input(TT_PAD_TOP_RIGHT,1);
    assert(tracker.app.focus==selector_sample_list); /* R1 is menu traversal. */
    tracker.instrument_selector_frame=NULL;
    tracker.manage_list=NULL;tracker.slices_list=NULL;
    tracker.perspective=TT_PERSPECTIVE_MAIN;
    tracker.focus=FOCUS_PHRASE;
    f80_set_root(&tracker.app,NULL);
    f80_free(nav_root);
    toggle_follow(NULL,NULL);
    assert(tracker.follow==1);
    tracker.play_order=0;
    tracker.active_order_index=-1;
    tracker.active_step=-1;
    int highlight_saved_edit=tracker.edit_order;
    int highlight_saved_active=tracker.active_order_index;
    tracker.edit_order=1;tracker.active_order_index=2;tracker.follow=1;
    assert(grid_highlight_order()==2);
    tracker.follow=0;
    assert(grid_highlight_order()==1);
    tracker.edit_order=highlight_saved_edit;
    tracker.active_order_index=highlight_saved_active;tracker.follow=1;
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
    TtWave saved_wave=tracker.wave;
    int saved_zoom=tracker.zoom,saved_view_start=tracker.view_start;
    F80Rect saved_wave_rect=wave.rect;
    float pinch_pcm[1024]={0};
    tracker.wave=(TtWave){.pcm=pinch_pcm,.frames=1024,.rate=44100,
                          .instrument_type=TT_INSTRUMENT_SAMPLE};
    tracker.zoom=tracker.view_start=0;
    wave.rect=f80_rect(10,0,100,4);
    int pinch_anchor=tracker.view_start+50*visible_frames()/wave.rect.w;
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_ZOOM_WAVE_AT,
                                      .index=1,.a=60}));
    assert(tracker.zoom==1 && tracker.view_start+
           50*visible_frames()/wave.rect.w==pinch_anchor);
    tracker.wave=saved_wave;tracker.zoom=saved_zoom;
    tracker.view_start=saved_view_start;
    wave.rect=saved_wave_rect;
    wave.visible=1;
    grid.rect=f80_rect(0,0,80,30);
    tracker.pattern_lock=1;
    tracker.selecting=tracker.visual_selecting=0;
    tracker.grid_selection_invalid=0;
    tracker.edit_order=0;tracker.pattern_index=tracker.song.order[0];
    tracker.step=0;tracker.phrase_scroll=0;
    handle_mouse(10,5,2); /* Start a drag selection inside the locked order. */
    handle_mouse(10,19,2); /* Extend the same selection into the next order. */
    assert(tracker.edit_order==0 && tracker.selecting &&
           !tracker.grid_selection_invalid && !selected_cell(8,0));
    tracker.selecting=tracker.visual_selecting=0;
    tracker.grid_selection_invalid=0;tracker.pattern_lock=0;
    handle_ui_event((TtUiEvent){.type=TT_UI_SELECT_ORDER,.index=0});
    tracker.pattern_lock=1;tracker.step=15;
    handle_ui_event((TtUiEvent){.type=TT_UI_BEGIN_VISUAL_SELECTION});
    handle_ui_event((TtUiEvent){.type=TT_UI_EXTEND_VISUAL_SELECTION,
                                .index=F80_KEY_DOWN});
    assert(tracker.edit_order==0 && tracker.step==15 &&
           !tracker.grid_selection_invalid && !selected_cell(16,0));
    handle_ui_event((TtUiEvent){.type=TT_UI_END_VISUAL_SELECTION});
    tracker.grid_selection_invalid=0;tracker.pattern_lock=0;
    handle_ui_event((TtUiEvent){.type=TT_UI_SELECT_ORDER,.index=0});
    tracker.step=0;
    order_choice.scroll=0;
    tracker.order_list=&order_choice;
    tracker.follow=1;
    handle_mouse(81,1,1); /* wheel scrolls the list, not its selection */
    assert(tracker.focus==FOCUS_ORDER && !tracker.follow);
    assert(order_choice.scroll==1 && order_choice.cursor==0);
    assert(tracker.play_order==0 && tracker.active_order_index==-1);
    tracker.order_list=NULL;
    const char *wheel_items[]={"00","01","02","03","04","05","06","07"};
    F80Widget *wheel_root=f80_box(F80_COL);
    F80Widget *wheel_canvas=f80_add(wheel_root,f80_canvas(NULL,NULL));
    F80Widget *wheel_list=f80_add(wheel_root,f80_listbox(wheel_items,8,NULL,NULL));
    f80_set_root(&tracker.app,wheel_root);
    f80_layout(wheel_root,f80_rect(0,0,20,10));
    wheel_canvas->rect=f80_rect(0,0,20,10); /* list overlays the canvas area */
    wheel_list->rect=f80_rect(2,2,10,3);
    tracker.wave_canvas=wheel_canvas;
    tracker.header=&header;
    assert(f80_scroll_drag_begin(&tracker.app,3,4));
    assert(f80_scroll_drag_move(&tracker.app,3,3) && wheel_list->scroll==1);
    assert(f80_scroll_drag_move(&tracker.app,3,5) && wheel_list->scroll==2);
    f80_scroll_drag_end(&tracker.app);
    f80_mouse(&tracker.app,3,3,1);
    assert(wheel_list->cursor==5 && !wheel_list->scroll_detached);
    f80_mouse(&tracker.app,3,3,1);
    assert(wheel_list->cursor==6); /* consecutive wheel inputs remain active */
#ifdef BUTTON4_PRESSED
    assert(native_wheel_delta(BUTTON4_PRESSED)==-1);
#endif
#ifdef BUTTON5_PRESSED
    assert(native_wheel_delta(BUTTON5_PRESSED)==1);
#endif
#ifdef BUTTON5_CLICKED
    assert(native_wheel_delta(BUTTON5_CLICKED)==1);
#endif
    wheel_list->scroll=wheel_list->cursor=0;wheel_list->scroll_detached=0;
    F80Widget hidden_order_frame={0};
    hidden_order_frame.visible=0;
    tracker.order_frame=&hidden_order_frame;
    tracker.order_list=&order_choice;
    order_choice.rect=wheel_list->rect;
    order_choice.cursor=0;
    handle_mouse(3,2,1);
    assert(wheel_list->cursor==1); /* list receives one precise wheel row */
    handle_mouse(3,2,2);
    assert(wheel_list->cursor==3); /* multi-row smooth-wheel deltas stay scroll input */
    handle_mouse(3,2,-1);
    assert(wheel_list->cursor==2); /* reversing wheel direction scrolls immediately */
    assert(order_choice.cursor==0); /* hidden Orders panel cannot intercept it */
    tracker.order_list=NULL;
    tracker.order_frame=NULL;
    f80_free(wheel_root);
    f80_set_root(&tracker.app,NULL);
    tracker.wave_canvas=&wave;
    tracker.focus=FOCUS_PHRASE;
    tracker.step=4;
    tracker.follow=1;
    handle_mouse(10,19,0); /* Mouse selects a visible order without seeking playback. */
    assert(tracker.pattern_index==1 && tracker.edit_order==1 && tracker.step==0);
    assert(tracker.follow==0 && tracker.play_order==0);
    assert(handle_ui_event((TtUiEvent){TT_UI_BEGIN_POINTER_SELECTION,0,10,19}));
    tracker.selecting=0;
    tracker.phrase_scroll=32;
    handle_mouse(10,7,0); /* repeated patterns can be selected at another order */
    assert(tracker.pattern_index==0 && tracker.edit_order==2 && tracker.step==4);
    assert(tracker.phrase_scroll==32); /* Clicking another order leaves the viewport fixed. */
    tracker.selecting=1;tracker.anchor_step=4;tracker.phrase_scroll=0;
    assert(handle_ui_event((TtUiEvent){TT_UI_BEGIN_POINTER_SELECTION,0,10,5}));
    assert(tracker.edit_order==0 && tracker.selecting && tracker.anchor_step==2);
    tracker.selecting=0;
    tracker.phrase_scroll=0;
    handle_mouse(10,5,0); /* an editable cell is manual navigation */
    assert(tracker.follow==0 && tracker.step==2);
    int scroll_before_wheel=tracker.phrase_scroll;
    tracker.reveal_cursor=1;
    tracker.follow=1;
    handle_mouse(10,5,1);
    assert(tracker.follow==0 && tracker.phrase_scroll==scroll_before_wheel+1 &&
           tracker.reveal_cursor==0);
    handle_mouse(10,5,-1);
    assert(tracker.phrase_scroll==scroll_before_wheel);
    tracker.follow=1;
    touch_scroll_rows(1);
    assert(tracker.follow==0 && tracker.phrase_scroll==1);
    F80Rect saved_grid_rect=tracker.phrase_canvas->rect;
    tracker.phrase_canvas->rect=f80_rect(0,0,80,14);
    tracker.phrase_scroll=4;tracker.follow=1;
    tracker.grid_touch_last_x=10;tracker.grid_touch_last_y=5;
    f80_pointer_gesture_begin(&tracker.grid_pointer_gesture,10,5,SDL_GetTicks());
    tracker_grid_touch_move(10,4,10,4); /* Quick touch swipe scrolls the rows. */
    assert(tracker.phrase_scroll==5 && tracker.follow==0 &&
           !tracker.grid_pointer_gesture.selecting);
    tracker_grid_pointer_up();
    tracker.phrase_canvas->rect=f80_rect(0,0,24,14);
    tracker.field_scroll=0;tracker.grid_touch_mode=1;
    tracker.selecting=1;tracker.anchor_step=2;tracker.anchor_channel=1;
    tracker.step=5;tracker.channel=1;
    tracker.grid_touch_last_x=10;tracker.grid_touch_last_y=5;
    f80_pointer_gesture_begin(&tracker.grid_pointer_gesture,10,5,SDL_GetTicks());
    tracker_grid_touch_move(9,5,9,5); /* A quick horizontal swipe scrolls fields. */
    assert(tracker.field_scroll==1 && !tracker.grid_pointer_gesture.selecting &&
           tracker.selecting && tracker.anchor_step==2 && tracker.step==5 &&
           tracker.anchor_channel==1 && tracker.channel==1);
    tracker_grid_pointer_up();
    tracker.grid_touch_mode=0;
    tracker.selecting=1;tracker.anchor_step=3;tracker.anchor_channel=0;
    tracker.step=6;tracker.channel=0;
    tracker_grid_pointer_down(10,5);
    assert(tracker_grid_pointer_scroll_move(11,5)==1);
    tracker.grid_pointer_gesture.pressed_at=(uint32_t)SDL_GetTicks()-2000;
    tracker_grid_pointer_poll(); /* Once moving, a long drag cannot arm selection. */
    assert(tracker.grid_pointer_gesture.moved &&
           !tracker.grid_pointer_gesture.selecting && tracker.selecting &&
           tracker.anchor_step==3 && tracker.step==6);
    tracker_grid_pointer_up();
    tracker.selecting=0;
    handle_ui_event((TtUiEvent){.type=TT_UI_SELECT_ORDER,.index=0});
    tracker.pattern_lock=1;tracker.step=3;tracker.phrase_scroll=0;
    tracker.anchor_step=1;tracker.anchor_channel=tracker.channel;
    tracker.selecting=1;tracker.grid_selection_invalid=0;
    handle_mouse(10,14,2); /* Drag below the grid scrolls and extends selection. */
    assert(tracker.step==11 && tracker.phrase_scroll==1 && tracker.selecting);
    handle_mouse(10,2,2); /* Drag above the grid scrolls back toward its start. */
    assert(tracker.step==0 && tracker.phrase_scroll==0 && tracker.selecting);
    tracker.phrase_canvas->rect=f80_rect(0,0,24,14);
    tracker.step=5;tracker.phrase_scroll=0;tracker.field_scroll=0;
    tracker.anchor_channel=0;tracker.channel=0;
    handle_mouse(25,8,2); /* Drag past the right edge scrolls grid fields. */
    assert(tracker.field_scroll>0 && tracker.channel>0 && tracker.selecting);
    tracker.selecting=0;tracker.pattern_lock=0;
    tracker.phrase_canvas->rect=saved_grid_rect;
    tracker.follow=1;
    handle_mouse(79,10,0); /* grid scrollbar */
    assert(tracker.follow==0);

    tracker.phrase_scroll=0;
    tracker.follow=1;
    tracker.play_order=1;
    tt_play_play_row(0);
    assert(tracker.active_order_index==1);
    assert(tracker.pattern_index==0 && tracker.edit_order==0);

    TtCell relative_cells[3][2];
    int relative_roots[TT_CHANNELS];
    memcpy(relative_roots,tracker.previous_note,sizeof(relative_roots));
    for(int row=0;row<3;++row)
        for(int channel=0;channel<2;++channel)
            relative_cells[row][channel]=tracker.pattern.cell[row][channel];
    int saved_relative_mode=tracker.play_mode;
    int saved_relative_loop=tracker.loop_pattern_index;
    tracker.play_mode=PLAY_PATTERN;tracker.loop_pattern_index=tracker.pattern_index;
    tracker.pattern.cell[0][0]=(TtCell){60,1,0,-1,0,-1,0,-1};
    tracker.pattern.cell[1][0]=(TtCell){tt_note_relative(1,1),1,0,-1,0,-1,0,-1};
    tracker.pattern.cell[1][1]=(TtCell){tt_note_left_relative(5),1,0,-1,0,-1,0,-1};
    tracker.pattern.cell[2][0]=(TtCell){tt_note_relative(0,1),1,0,-1,0,-1,0,-1};
    tracker.previous_note[0]=-1;tracker.previous_note[1]=-1;
    tt_play_play_row(0);
    assert(tracker.previous_note[0]==60);
    tt_play_play_row(1);
    assert(tracker.previous_note[0]==61 && tracker.previous_note[1]==66);
    tt_play_play_row(2);
    assert(tracker.previous_note[0]==60); /* -01 follows the same-channel note. */
    for(int row=0;row<3;++row)
        for(int channel=0;channel<2;++channel)
            tracker.pattern.cell[row][channel]=relative_cells[row][channel];
    memcpy(tracker.previous_note,relative_roots,sizeof(relative_roots));
    tracker.play_mode=saved_relative_mode;tracker.loop_pattern_index=saved_relative_loop;

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
    assert(tracker.phrase_scroll==53); /* Preserve the cursor's screen row across order selection. */
    tracker.pattern.cell[0][0]=(TtCell){-1,-1,TT_EFFECT_T,0,
                                          TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.play_mode=PLAY_PATTERN;tracker.loop_pattern_index=1;
    tracker.song.table[0].tick[0].note=60;
    tracker.song.table[0].tick[0].instrument=1;
    tracker.song.table[0].tick[0].effect[0]=(TtTableEffect){'M',0x33};
    tracker.song.table[0].tick[0].effect[1]=(TtTableEffect){'U',0x80};
    tracker.song.table[0].tick[0].effect[2]=(TtTableEffect){'Z',3};
    tracker.song.table[0].tick[1].effect[0]=(TtTableEffect){'X',1};
    tracker.song.table[1].tick[0].effect[0]=(TtTableEffect){'Z',0};
    TtTable saved_loop_table=tracker.song.table[0];
    tracker.song.table[0].length=2;
    tt_play_play_row(0);
    assert(tracker.table_voice[0].active && tracker.table_voice[0].table==0);
    tt_play_table_ticks();
    assert(tracker.table_voice[0].pending && tracker.playback_ticks_per_row==3);
    tt_play_table_ticks();
    assert(tracker.table_voice[0].table==1 && tracker.table_voice[0].tick==0);
    tt_play_table_ticks();
    assert(!tracker.table_voice[0].pending &&
           tracker.playback_ticks_per_row==tracker.song.ticks_per_row);
    tracker.song.pattern[1].cell[0][5]=(TtCell){-1,-1,TT_EFFECT_X,2,
                                               TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.song.pattern[1].cell[1][5]=(TtCell){-1,-1,TT_EFFECT_Y,7,
                                               TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.song.pattern[1].cell[2][5]=(TtCell){-1,-1,TT_EFFECT_Z,0x50,
                                               TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.song.pattern_count=3;tt_pattern_init(&tracker.song.pattern[2]);
    tracker.song.pattern[2].cell[1][5]=tracker.song.pattern[1].cell[1][5];
    tracker.song.pattern[2].cell[2][5]=tracker.song.pattern[1].cell[2][5];
    tracker.pattern=tracker.song.pattern[1];
    tracker.pattern_index=tracker.loop_pattern_index=1;
    tracker.play_mode=PLAY_PATTERN;
    tt_play_play_row(0);
    assert(tracker.pending_flow==TT_PLAY_PATTERN_JUMP);
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_APPLY_PENDING_FLOW}));
    tt_play_flush();
    assert(tracker.loop_pattern_index==2 && tracker.play_step==0);
    tt_play_play_row(1);
    assert(tracker.pending_flow==TT_PLAY_ROW_JUMP);
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_APPLY_PENDING_FLOW}));
    tt_play_flush();
    assert(tracker.play_step==7);
    tt_play_play_row(2);
    assert(tracker.playback_bpm==0x50 &&
           tracker.playback_ticks_per_row==tracker.song.ticks_per_row);
    tracker.song.pattern[2].cell[3][5]=(TtCell){-1,-1,TT_EFFECT_Z,0,
                                               TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tt_play_play_row(3);
    assert(tracker.playback_bpm==tracker.song.bpm);
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_SET_BPM,.a=0}));
    tt_play_flush();
    assert(tracker.playback_bpm==tracker.song.bpm);
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_SET_TICKS_PER_ROW,.a=0}));
    tt_play_flush();
    assert(tracker.playback_ticks_per_row==tracker.song.ticks_per_row);
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_SET_EFFECT_REGISTER,
                                      .a=0,.b=0}));
    tt_play_flush();
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_SET_EFFECT_REGISTER,
                                      .a=TT_EFFECT_REGISTER_COUNT,.b=0xff}));
    tt_play_flush();
    assert(tracker.effect_registers[0]==0);
    tracker.pattern=tracker.song.pattern[2];
    tracker.pattern_index=tracker.loop_pattern_index=2;
    tracker.play_mode=PLAY_PATTERN;
    tracker.pattern.cell[4][0]=(TtCell){-1,-1,'0',1,TT_EFFECT_T,
                                        tt_effect_register_value(0),TT_EFFECT_NONE,-1};
    tracker.pattern.cell[5][0]=(TtCell){-1,-1,'0',2,TT_EFFECT_T,
                                        tt_effect_register_value(0),TT_EFFECT_NONE,-1};
    tt_play_play_row(4);
    assert(tracker.effect_registers[0]==1 && tracker.table_voice[0].active &&
           tracker.table_voice[0].table==1);
    tt_play_play_row(5);
    assert(tracker.effect_registers[0]==2 && tracker.table_voice[0].active &&
           tracker.table_voice[0].table==2);
    tracker.song.table[0].tick[0].effect[0]=(TtTableEffect){'F',3};
    tracker.song.table[0].tick[0].effect[1]=(TtTableEffect){'X',
                                         tt_effect_register_value(15)};
    tracker.table_voice[0]=(TableVoice){.active=1,.table=0,.tick=0,
                                        .next_table=-1};
    tt_play_table_ticks();
    assert(tracker.effect_registers[15]==3 && tracker.table_voice[0].table==3);
    tracker.play_mode=PLAY_SONG;
    tracker.song.order_count=3;
    tracker.song.order[0]=0;tracker.song.order[1]=1;tracker.song.order[2]=2;
    tracker.song.order_active[0]=tracker.song.order_active[2]=1;
    tracker.song.order_active[1]=0;
    tracker.play_order=0;
    tracker.song.pattern[0].cell[3][5]=(TtCell){-1,-1,TT_EFFECT_X,1,
                                               TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tt_play_play_row(3);
    assert(tracker.pending_flow==0); /* inactive target is not a Song jump */
    tracker.song.pattern[0].cell[3][5].value=2;
    tt_play_play_row(3);
    assert(tracker.pending_flow==TT_PLAY_PATTERN_JUMP && tracker.pending_order==2);
    assert(tt_play_emit((TtPlayEvent){.type=TT_PLAY_APPLY_PENDING_FLOW}));
    tt_play_flush();
    tracker.play_mode=PLAY_PATTERN;
    tracker.pattern_index=tracker.loop_pattern_index=1;
    tracker.song.pattern_count=3;tt_pattern_init(&tracker.song.pattern[2]);
    tracker.song.pattern[2].cell[1][5]=tracker.song.pattern[1].cell[1][5];
    tracker.song.pattern[2].cell[2][5]=tracker.song.pattern[1].cell[2][5];
    tracker.pattern=tracker.song.pattern[1];
    tracker.pattern.cell[3][0]=(TtCell){TT_NOTE_OFF,-1,TT_EFFECT_NONE,-1,
                                        TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.song.table[0].length=2;
    tracker.song.table[0].tick[0]=(TtTableTick){0};
    tracker.song.table[0].tick[0].effect[0]=(TtTableEffect){'Y',0};
    tracker.table_voice[0]=(TableVoice){.active=1,.table=0,.tick=0};
    tt_play_table_ticks();
    assert(tracker.table_voice[0].table_loop_active && tracker.table_voice[0].tick==0);
    tt_play_play_row(3);
    assert(!tracker.table_voice[0].active && !tracker.table_voice[0].table_loop_active);
    tt_play_table_ticks();
    assert(!tracker.table_voice[0].table_loop_active);
    tracker.pattern.cell[2][0]=(TtCell){60,1,TT_EFFECT_NONE,-1,
                                        TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    tracker.table_voice[0]=(TableVoice){.active=1,.table=0,.tick=1,
                                        .pending=1,.next_table=2};
    tt_play_play_row(2);
    assert(!tracker.table_voice[0].active && !tracker.table_voice[0].pending &&
           tracker.table_voice[0].next_table==-1);
    tracker.song.table[0]=saved_loop_table;
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
    tracker.table_grid.row=6;
    handle_table_key('0');handle_table_key('3');handle_table_key('0');
    assert(tracker.song.table[0].tick[6].effect[0].command=='0' &&
           tracker.song.table[0].tick[6].effect[0].value==0x30);
    tracker.table_grid.row=7;
    handle_table_key('m');handle_table_key('$');handle_table_key('0');
    assert(tracker.song.table[0].tick[7].effect[0].command=='M' &&
           tracker.song.table[0].tick[7].effect[0].value==
               tt_effect_register_value(0));
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
    F80Widget instrument_wave_frame={0};
    int perspective_before=tracker.perspective;
    tracker.wave_frame=&instrument_wave_frame;
    instrument_wave_frame.rect=f80_rect(0,1,80,8);
    tracker.perspective=TT_PERSPECTIVE_INSTRUMENTS;
    tracker.zoom=0;
    handle_mouse(0,1,-1); /* instrument view zooms from the wave frame edge too */
    assert(tracker.zoom==1);
    tracker.zoom=0;
    tracker.perspective=perspective_before;
    tracker.wave_frame=NULL;
    wave.rect=f80_rect(0,1,80,8);
    tracker.marker=0;
    int marker_x=wave_x_from_frame(tracker.phrase.marker[5],wave.rect.w*2)/2;
    handle_mouse(marker_x,3,0);
    assert(tracker.marker==5);
    handle_mouse(marker_x+2,3,0);
    assert(tracker.marker==5);
    tracker.wave_canvas=&wave;
    tracker.zoom=1;tracker.view_start=400;
    begin_wave_gesture(40);
    pan_wave_touch(20);
    int wave_pan_after_drag=tracker.view_start;
    pan_wave_touch(-2); /* Continuing the drag beyond the left edge keeps panning. */
    assert(wave_pan_after_drag>400 && tracker.view_start>wave_pan_after_drag);
    tracker.zoom=1;tracker.view_start=0;
    wave_touch.x=78;wave_touch.visible=visible_frames();
    wave_touch.marker_frame=tracker.phrase.marker[1];tracker.marker=1;
    int marker_before_edge_drag=tracker.phrase.marker[1];
    move_wave_marker_touch(81); /* Dragging past the right edge pans the wave. */
    assert(tracker.view_start>0);
    edit_marker(1,marker_before_edge_drag);
    tracker.view_start=0;tracker.zoom=0;
    tracker.wave.pcm=NULL; /* stack-backed smoke sample is not history-owned */
    TtEditDoc history_doc=edit_doc();
    edit_history=tt_edit_history_new(&history_doc);
    assert(edit_history);
    tracker.focus=FOCUS_PHRASE;tracker.field=2;tracker.step=0;tracker.channel=0;
    assert(strcmp(current_status()[0].label,"Focus")==0);
    TtCell picker_original=tracker.pattern.cell[0][0];
    effect_picker_open(NULL,NULL);
    assert(tracker.effect_picker_labels[17][0]=='R' &&
           !strcmp(tracker.effect_picker_labels[17],"RRR"));
    for(int i=0;i<36;++i)assert(strlen(tracker.effect_picker_labels[i])==3);
    assert(!strcmp(tracker.effect_picker_labels[18],"HPC") &&
           !strcmp(tracker.effect_picker_labels[19],"LPC") &&
           !strcmp(tracker.effect_picker_labels[20],"DRG") &&
           !strcmp(tracker.effect_picker_labels[21],"CRG") &&
           !strcmp(tracker.effect_picker_labels[22],"UDL") &&
           !strcmp(tracker.effect_picker_labels[23],"MVL") &&
           !strcmp(tracker.effect_picker_labels[33],"QRS"));
    assert(tracker.effect_picker_canvas &&
           tracker.effect_picker_dialog->child_count==2);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_EFFECT_PICKER_CURSOR,
                                       .index=17}));
    handle_key(13);
    assert(tracker.pattern.cell[0][0].effect==TT_EFFECT_R &&
           tracker.effect_picker_dialog==NULL);
    effect_picker_open(NULL,NULL); /* A second open must create a new modal. */
    handle_key(27);
    assert(tracker.pattern.cell[0][0].effect==TT_EFFECT_NONE &&
           tracker.effect_picker_dialog==NULL);
    handle_key(10); /* Grid Accept opens autocomplete on an effect-name cell. */
    assert(tracker.effect_picker_dialog &&
           tracker.app.modal==tracker.effect_picker_dialog);
    handle_key(27);
    assert(tracker.effect_picker_dialog==NULL);
    controller_action_input(TT_PAD_AFFIRMATIVE,1);
    assert(tracker.effect_picker_dialog &&
           tracker.app.modal==tracker.effect_picker_dialog);
    controller_action_input(TT_PAD_AFFIRMATIVE,0);
    handle_key(27);
    assert(tracker.effect_picker_dialog==NULL);
    edit_cell(0,0,picker_original);
    assert(tt_edit_undo(edit_history,&history_doc));
    assert(tt_edit_undo(edit_history,&history_doc));
    assert(tt_edit_undo(edit_history,&history_doc));
    TtCell byte_picker_original=tracker.pattern.cell[0][0];
    tracker.focus=FOCUS_PHRASE;tracker.step=0;tracker.channel=0;tracker.field=3;
    tracker.grid_value_editing=0;
    handle_key(13); /* Forward opens the byte picker on effect-value cells. */
    assert(tracker.byte_picker_dialog &&
           tracker.app.modal==tracker.byte_picker_dialog &&
           tracker.byte_picker_cursor==0);
    handle_key(F80_KEY_RIGHT);handle_key(F80_KEY_DOWN);
    assert(tracker.byte_picker_cursor==0x11);
    handle_key(13);
    assert(!tracker.byte_picker_dialog &&
           tracker.pattern.cell[0][0].value==0x11);
    assert(tracker.pattern.cell[0][0].effect==
           (byte_picker_original.effect==TT_EFFECT_NONE?
            TT_EFFECT_S:byte_picker_original.effect));
    assert(!tracker.grid_value_editing);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_GRID_VALUE_EDITING,.a=1}));
    handle_key(2003); /* Forward still opens while editing a byte value. */
    assert(tracker.byte_picker_dialog);
    handle_key(27);
    assert(!tracker.byte_picker_dialog && tracker.pattern.cell[0][0].value==-1);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_GRID_VALUE_EDITING,.a=0}));
    controller_action_input(TT_PAD_AFFIRMATIVE,1);
    assert(tracker.byte_picker_dialog &&
           tracker.app.modal==tracker.byte_picker_dialog);
    controller_action_input(TT_PAD_AFFIRMATIVE,0);
    handle_key(27);
    assert(!tracker.byte_picker_dialog);
    tracker.field=1;
    handle_key(13); /* The grid instrument cell uses the same byte chooser. */
    assert(tracker.byte_picker_dialog && tracker.byte_picker_cursor==0);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_BYTE_PICKER_CURSOR,
                                       .index=0x2a}));
    handle_key(13);
    assert(!tracker.byte_picker_dialog &&
           tracker.pattern.cell[0][0].instrument==0x2a);
    assert(tt_edit_undo(edit_history,&history_doc));
    assert(tracker.pattern.cell[0][0].instrument==byte_picker_original.instrument);
    tracker.field=3;
    edit_cell(0,0,byte_picker_original);
    assert(tt_edit_undo(edit_history,&history_doc));
    assert(tt_edit_undo(edit_history,&history_doc));
    assert(tt_edit_undo(edit_history,&history_doc));
    F80Widget order_input={0};
    order_input.kind=F80_LISTBOX;
    order_input.range_cursor_pair=F80_PAIR_RANGE_CURSOR;
    order_input.rect=f80_rect(0,0,12,4);order_input.item_count=tracker.song.order_count;
    order_input.range_active=1;order_input.range_first=1;order_input.range_last=2;
    order_input.cursor=1;
    assert(f80_listbox_row_pair(&order_input,1)==F80_PAIR_RANGE_CURSOR);
    assert(f80_listbox_row_pair(&order_input,2)==65);
    assert(f80_listbox_row_pair(&order_input,0)==F80_PAIR_LIST_INACTIVE);
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
    tracker.selecting=1;tracker.visual_selecting=0;
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
    F80Widget shortcut_wave_row={.visible=1},shortcut_grid_row={.visible=1},
        shortcut_phrase={.visible=1},shortcut_instruments={.visible=1},
        shortcut_order={.visible=1},shortcut_wave={.visible=1},
        shortcut_wave_frame={.visible=1},shortcut_manage={.visible=1},
        shortcut_slices={.visible=1},shortcut_order_dialog={.visible=1};
    tracker.wave_row=&shortcut_wave_row;tracker.grid_row=&shortcut_grid_row;
    tracker.phrase_frame=&shortcut_phrase;
    tracker.instrument_panels=&shortcut_instruments;
    tracker.order_frame=&shortcut_order;tracker.wave_canvas=&shortcut_wave;
    tracker.wave_frame=&shortcut_wave_frame;
    tracker.instrument_selector_frame=NULL;
    tracker.order_dialog=&shortcut_order_dialog;
    tracker.manage_dialog=&shortcut_manage;tracker.slices_dialog=&shortcut_slices;
    handle_key(22);
    handle_key(2005); /* F5 */
    assert(tracker.perspective==TT_PERSPECTIVE_MAIN &&
           !tracker.selecting && !tracker.visual_selecting);
    handle_key(22);
    handle_key(2006); /* F6 */
    assert(tracker.perspective==TT_PERSPECTIVE_INSTRUMENTS &&
           !tracker.selecting && !tracker.visual_selecting);
    handle_key(22);
    handle_key(27);
    assert(!tracker.selecting && !tracker.visual_selecting);
    handle_key(22);
    handle_key(2007); /* F7 */
    assert(tracker.perspective==TT_PERSPECTIVE_ORDERS &&
           !tracker.selecting && !tracker.visual_selecting);
    int playing_before_f8=tracker.playing;
    handle_key(2008); /* F8 toggles playback. */
    assert(tracker.playing!=playing_before_f8);
    handle_key(2008);
    assert(tracker.playing==playing_before_f8);
    tracker.order_dialog=NULL;tracker.manage_dialog=NULL;tracker.slices_dialog=NULL;
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
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_ACTIVE)==72);
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_INACTIVE)==73);
    root.window_role=F80_WINDOW_CYAN;
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_ACTIVE)==74);
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_INACTIVE)==75);
    root.window_role=F80_WINDOW_GRAY;
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_ACTIVE)==76);
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_INACTIVE)==77);
    root.window_role=F80_WINDOW_CONSOLE;
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_ACTIVE)==78);
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_INACTIVE)==79);
    root.window_role=F80_WINDOW_PURPLE;
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_ACTIVE)==80);
    assert(f80_palette_pair(&root,F80_COLOR_FRAME_INACTIVE)==81);
    F80Widget alert={.style=15,.window_role=F80_WINDOW_BLUE};
    assert(f80_palette_pair(&alert,F80_COLOR_FRAME_ACTIVE)==82);
    assert(f80_palette_pair(&alert,F80_COLOR_FRAME_INACTIVE)==83);
    F80Widget dialog_frame={.style=7,.window_role=F80_WINDOW_BLUE};
    assert(f80_palette_pair(&dialog_frame,F80_COLOR_FRAME_ACTIVE)==76);
    assert(f80_palette_pair(&dialog_frame,F80_COLOR_FRAME_INACTIVE)==77);
    root.window_role=F80_WINDOW_BLUE;
    F80Widget gap={.visible=1},wave_row={.visible=1},grid_row={.visible=1},
        phrase_frame={.visible=1};
    F80Widget details={.visible=1},status={.visible=1};
    F80Widget console={.rect={0,1,80,10}};
    header.visible=1;
    tracker.root=&root;tracker.header_gap=&gap;tracker.header=&header;
    tracker.wave_row=&wave_row;tracker.grid_row=&grid_row;
    tracker.phrase_frame=&phrase_frame;
    tracker.details=&details;tracker.status=&status;tracker.console=&console;
    current_status();assert(status.status_count==4);
    int logged=event_log_count;
    view_items[5].action(NULL,NULL);
    assert(tracker.console_visible && root.window_role==F80_WINDOW_CONSOLE);
    assert(!strcmp(view_items[5].label,"~H~ide event console"));
    assert(console.visible && !header.visible && !details.visible && !status.visible);
    assert(event_log_count>logged || event_log_head!=(logged%TT_EVENT_LOG_CAPACITY));
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
    /* Editor inputs must emit validated edits; navigation remains UI-only. */
    memset(&tracker,0,sizeof(tracker));tt_song_init(&tracker.song);
    dx7_grids_init();
    tracker.pattern=tracker.song.pattern[0];tracker.sample_count=1;
    tt_phrase_init(&tracker.phrase,0);tracker.wave.instrument_type=TT_INSTRUMENT_DX7;
    tt_dx7_init(&tracker.wave.dx7);
    assert(initial_instruments_ready()); /* DX7 instruments do not need WAV data */
    tracker.wave.instrument_type=TT_INSTRUMENT_SAMPLE;
    assert(!initial_instruments_ready()); /* sample instruments still require WAV data */
    tracker.wave.instrument_type=TT_INSTRUMENT_DX7;
    F80Widget dx7_root={.visible=1,.enabled=1};
    F80Widget dx7_instrument_panel={.visible=1,.enabled=1,.parent=&dx7_root};
    F80Widget dx7_operator_panel={.visible=1,.enabled=1,.frame=1,
                                  .parent=&dx7_instrument_panel};
    F80Widget dx7_global_panel={.visible=1,.enabled=1,.frame=1,
                                .parent=&dx7_instrument_panel};
    F80Widget dx7_canvas={.kind=F80_CANVAS,.rect={0,0,120,20},
                          .visible=1,.enabled=1,.focusable=1,
                          .parent=&dx7_operator_panel};
    tracker.wave_canvas=&dx7_canvas;tracker.app.root=&dx7_root;
    tracker.app.focus=&dx7_canvas;
    tracker.focus=FOCUS_WAVE;tracker.dx7_grid_kind=0;
    F80Widget global_canvas={.kind=F80_CANVAS,.rect={0,0,48,12},
                             .visible=1,.enabled=1,.focusable=1,
                             .parent=&dx7_global_panel};
    tracker.dx7_globals_canvas=&global_canvas;
    tracker.dx7_globals_panel=&dx7_global_panel;
    tracker.dx7_operators_panel=&dx7_operator_panel;
    F80Grid saved_globals_grid=tracker.dx7_globals_grid;
    tracker.dx7_globals_grid=(F80Grid){.field=0,.rect={0,0,48,12}};
    tracker.dx7_grid_kind=0;
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_FOCUS,
                                       .index=0,.a=0}));
    tracker.dx7_globals_grid=saved_globals_grid;
    edit_history=tt_edit_history_new(&(TtEditDoc){.song=&tracker.song,
        .active=&tracker.pattern,.active_index=&tracker.pattern_index,.samples=tracker.samples,
        .active_wave=&tracker.wave,.active_phrase=&tracker.phrase,
        .sample_count=&tracker.sample_count,.sample_index=&tracker.sample_index,
        .active_loaded_path=tracker.loaded_path,
        .active_source_ref=tracker.source_ref,.active_csv_path=tracker.csv_path});
    assert(edit_history);
    tracker.perspective=TT_PERSPECTIVE_INSTRUMENTS;
    tracker.synth_canvas=&dx7_canvas;
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_INSTRUMENT_FOCUS_RECONCILE}));
    assert(tracker.app.focus==&dx7_canvas);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GLOBAL_SELECT,.index=0}));
    assert(tracker.app.focus==&global_canvas);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_KEY,
        .index=1,.a=F80_KEY_LEFT}));
    assert(tracker.app.focus==&dx7_canvas);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GLOBAL_SELECT,.index=0}));
    send_play(TT_PLAY_AUDITION,0,0);assert(tt_audio_voice_active(0));
    handle_key('+');assert(tracker.wave.dx7.data[134]==1 && tt_audio_voice_active(0));
    undo_edit(NULL,NULL);assert(tracker.wave.dx7.data[134]==0 && tt_audio_voice_active(0));
    redo_edit(NULL,NULL);assert(tracker.wave.dx7.data[134]==1 && tt_audio_voice_active(0));
    TtEditDoc synthdoc=edit_doc();assert(tt_edit_undo(edit_history,&synthdoc));
    assert(tracker.wave.dx7.data[134]==0);
    f80_grid_layout(&tracker.dx7_operators_grid,f80_rect(0,0,24,9));
    assert(tracker.dx7_operators_grid.fields==22 &&
           tracker.dx7_operators_grid.show_h &&
           tracker.dx7_operators_grid.read_only[0]);
    assert(tracker.dx7_pitch_grid.rows==1 && tracker.dx7_pitch_grid.fields==8 &&
           tracker.dx7_pitch_grid.gutter_width==16 &&
           !strcmp(tracker.dx7_pitch_grid.corner,""));
    char operator_gutter[16];
    dx7_operator_row_text(NULL,1,-1,operator_gutter,sizeof(operator_gutter));
    assert(!strcmp(operator_gutter,"OP2"));
    char pitch_gutter[24];
    dx7_pitch_row_text(NULL,0,-1,pitch_gutter,sizeof(pitch_gutter));
    assert(!strcmp(pitch_gutter,"Pitch Envelope: "));
    int global_columns=dx7_global_columns(global_canvas.rect.w);
    tracker.dx7_globals_grid.field=0;
    tracker.app.focus=&dx7_global_panel;tracker.dx7_grid_kind=0;
    handle_key(F80_KEY_DOWN);
    assert(tracker.dx7_globals_grid.field==global_columns);
    assert(tracker.dx7_grid_kind==0);
    handle_key(F80_KEY_UP);
    assert(tracker.dx7_globals_grid.field==0);
    int global_rows=(11+global_columns-1)/global_columns;
    for(int row=1;row<global_rows;++row)handle_key(F80_KEY_DOWN);
    assert(tracker.dx7_grid_kind==0);
    handle_key(F80_KEY_DOWN); /* The last globals row leads into Pitch Envelope. */
    assert(tracker.dx7_grid_kind==2);
    handle_key(F80_KEY_UP);
    assert(tracker.dx7_grid_kind==0);
    handle_key(F80_KEY_UP);
    assert(tracker.dx7_grid_kind==0); /* The top edge stays in this pane. */
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_KEY,
        .index=1,.a=F80_KEY_RIGHT}));
    assert(tracker.dx7_grid_kind==1 && tracker.dx7_operators_grid.field==1);
    for(int i=0;i<7;++i)
        assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_KEY,
            .index=1,.a=F80_KEY_RIGHT}));
    assert(tracker.dx7_operators_grid.scroll_x>0);
    assert(tt_edit_can_redo(edit_history)); /* cursor did not touch history */
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_KEY,
        .index=1,.a=F80_KEY_HOME}));
    while(tracker.dx7_operators_grid.field>1)
        assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_KEY,
            .index=1,.a=F80_KEY_LEFT}));
    tracker.app.focus=&dx7_canvas;
    int parameter=dx7_operator_parameter(0,tracker.dx7_operators_grid.field);
    int old_value=tracker.wave.dx7.data[parameter];
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_EDITING,.a=1}));
    assert(dx7_grid_is_editing());
    handle_key(F80_KEY_UP);
    assert(tracker.wave.dx7.data[parameter]==
           clamp(old_value+10,0,tt_dx7_max(parameter)));
    handle_key(F80_KEY_RIGHT);
    assert(tracker.wave.dx7.data[parameter]==
           clamp(old_value+11,0,tt_dx7_max(parameter)));
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_EDITING,.a=0}));
    assert(!dx7_grid_is_editing());
    int old_decimal_value=tracker.wave.dx7.data[parameter];
    handle_key(13); /* Forward opens autocomplete instead of toggling edit. */
    assert(!dx7_grid_is_editing());
    assert(tracker.decimal_picker_dialog &&
           tracker.app.modal==tracker.decimal_picker_dialog);
    handle_key(2003); /* Forward opens the FM decimal grid. */
    assert(tracker.decimal_picker_dialog &&
           tracker.decimal_picker_max==tt_dx7_max(parameter));
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DECIMAL_PICKER_CURSOR,
                                       .index=0}));
    handle_key(F80_KEY_RIGHT);handle_key(F80_KEY_DOWN);
    assert(tracker.decimal_picker_cursor==11);
    handle_key(13);
    assert(!tracker.decimal_picker_dialog &&
           tracker.wave.dx7.data[parameter]==11);
    assert(!dx7_grid_is_editing());
    assert(tt_edit_undo(edit_history,&synthdoc));
    assert(tracker.wave.dx7.data[parameter]==old_decimal_value);
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_KEY,
        .index=2,.a=F80_KEY_RIGHT}));
    assert(tracker.dx7_grid_kind==2 && tracker.dx7_pitch_grid.field==1);
    tracker.app.focus=&dx7_canvas;
    int pitch_parameter=dx7_pitch_parameter(tracker.dx7_pitch_grid.field);
    int old_pitch=tracker.wave.dx7.data[pitch_parameter];
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_EDITING,.a=1}));
    assert(dx7_grid_is_editing());
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_KEY,
        .index=2,.a=F80_KEY_UP}));
    assert(tracker.wave.dx7.data[pitch_parameter]==
           clamp(old_pitch+10,0,tt_dx7_max(pitch_parameter)));
    assert(handle_ui_event((TtUiEvent){.type=TT_UI_DX7_GRID_EDITING,.a=0}));
    assert(!dx7_grid_is_editing());
    tt_play_emit((TtPlayEvent){.type=TT_PLAY_AUDITION});
    TtEditEvent new_synth={.type=TT_EDIT_DX7_APPEND};assert(emit_edit(&new_synth));
    assert(!tracker.playing && tracker.sample_count==2 && tracker.sample_index==1);
    assert(!tracker.wave.pcm && tt_dx7_valid(&tracker.wave.dx7));
    assert(tt_edit_undo(edit_history,&synthdoc) && tracker.sample_count==1);
    tt_edit_history_free(edit_history);edit_history=NULL;tt_audio_close();
    memset(&tracker,0,sizeof(tracker));tt_song_init(&tracker.song);
    tracker.pattern=tracker.song.pattern[0];tracker.sample_count=1;
    char source_error[160];
    assert(tt_load_wav("samples/sine-c4.wav",&tracker.wave,
                       source_error,sizeof(source_error)));
    tt_phrase_init(&tracker.phrase,tracker.wave.frames);
    TtPlayEvent preview_event={.type=TT_PLAY_PREVIEW_SAMPLE};
    preview_event.preview_sample.instrument=tracker.sample_index;
    assert(tt_play_emit(preview_event));tt_play_flush();
    assert(tt_audio_voice_pcm(0)==tracker.wave.pcm);
    preview_event=(TtPlayEvent){.type=TT_PLAY_PREVIEW_SAMPLE};
    snprintf(preview_event.preview_sample.path,
             sizeof(preview_event.preview_sample.path),"samples/sine-c4.wav");
    assert(tt_play_emit(preview_event));tt_play_flush();
    assert(tt_audio_voice_pcm(0) && tt_audio_voice_pcm(0)!=tracker.wave.pcm);
    send_play(TT_PLAY_STOP,0,0);
    assert(!tt_audio_voice_active(0));
    snprintf(tracker.loaded_path,sizeof(tracker.loaded_path),"%s","samples/sine-c4.wav");
    snprintf(tracker.source_ref,sizeof(tracker.source_ref),"%s","./shared.wav");
    snprintf(tracker.song_path,sizeof(tracker.song_path),"%s","default.ttt");
    F80Widget *source_root=f80_box(F80_COL);
    f80_set_root(&tracker.app,source_root);
    F80Widget source_list={.cursor=0};tracker.manage_list=&source_list;
    TtEditDoc source_doc=edit_doc();
    edit_history=tt_edit_history_new(&source_doc);assert(edit_history);
    manage_source(NULL,NULL);
    assert(tracker.app.modal==tracker.source_dialog &&
           !strcmp(tracker.source_input->input,"./shared.wav"));
    source_internal(NULL,NULL);
    assert(!strcmp(tracker.source_input->input,"!samples/01.wav"));
    source_apply(NULL,NULL);
    assert(!tracker.app.modal && !tracker.source_ref[0]);
    manage_source(NULL,NULL);
    char source_absolute[512];assert(realpath("samples/sine-c4.wav",source_absolute));
    source_browsed(source_absolute);
    assert(!strcmp(tracker.source_input->input,"./samples/sine-c4.wav"));
    source_apply(NULL,NULL);
    assert(!tracker.app.modal && !strcmp(tracker.source_ref,"./samples/sine-c4.wav"));
    assert(tt_edit_undo(edit_history,&source_doc) && !tracker.source_ref[0]);
    assert(tt_edit_redo(edit_history,&source_doc) &&
           !strcmp(tracker.source_ref,"./samples/sine-c4.wav"));
    TtSongSample archive_sample={0};
    snprintf(archive_sample.name,sizeof(archive_sample.name),"Test WAV");
    snprintf(archive_sample.source_name,sizeof(archive_sample.source_name),"sine-c4.wav");
    archive_sample.frames=tracker.wave.frames;archive_sample.rate=tracker.wave.rate;
    archive_sample.phrase=tracker.phrase;
    char archive_path[]="/tmp/tt-source-XXXXXX.zip";
    int archive_fd=mkstemps(archive_path,4);assert(archive_fd>=0);close(archive_fd);
    const char *archive_sources[TT_SONG_SAMPLES]={"samples/sine-c4.wav"};
    const TtWave *archive_waves[TT_SONG_SAMPLES]={&tracker.wave};
    assert(tt_package_save(archive_path,&tracker.song,&archive_sample,1,
                           archive_sources,archive_waves,source_error,sizeof(source_error)));
    manage_source(NULL,NULL);
    char archive_wav[512];
    assert(snprintf(archive_wav,sizeof(archive_wav),"%s!samples/01.wav",
                    archive_path)<(int)sizeof(archive_wav));
    source_browsed(archive_path);
    assert(tracker.app.modal==tracker.source_dialog);
    source_browsed(archive_wav);
    assert(tracker.app.modal==tracker.source_dialog &&
           strstr(tracker.source_input->input,"!samples/01.wav"));
    source_apply(NULL,NULL);
    assert(!tracker.app.modal && strstr(tracker.source_ref,"!samples/01.wav") &&
           tracker.wave.pcm);
    tracker.path_action=PATH_WAV;
    file_chosen(archive_wav,NULL);
    assert(!tracker.app.modal && tracker.sample_count==2 &&
           tracker.sample_index==1 && tracker.wave.pcm &&
           !strcmp(tracker.wave.source_name,"01.wav") && !tracker.source_ref[0]);
    unlink(archive_path);tt_package_cleanup_all();
    tt_edit_history_free(edit_history);edit_history=NULL;
    tracker.song.pattern_count=3;
    for(int p=0;p<3;++p) {
        tt_pattern_init(&tracker.song.pattern[p]);
        snprintf(tracker.song.name[p],TT_SONG_NAME,"Pattern %d",p);
    }
    tracker.song.order_count=2;tracker.song.order[0]=0;tracker.song.order[1]=2;
    tracker.pattern=tracker.song.pattern[0];tracker.pattern_index=0;
    TtStored empty_samples[TT_HISTORY_MAX]={0};
    TtWave empty_wave={0};TtPhrase empty_phrase={0};
    int empty_count=0,empty_index=0;
    char empty_path[512]={0},empty_ref[512]={0},empty_csv[256]={0},empty_song_path[512]={0};
    TtEditDoc delete_doc=edit_doc();
    delete_doc.samples=empty_samples;delete_doc.active_wave=&empty_wave;
    delete_doc.active_phrase=&empty_phrase;delete_doc.sample_count=&empty_count;
    delete_doc.sample_index=&empty_index;delete_doc.active_loaded_path=empty_path;
    delete_doc.active_source_ref=empty_ref;delete_doc.active_csv_path=empty_csv;
    delete_doc.song_path=empty_song_path;
    edit_history=tt_edit_history_new(&delete_doc);assert(edit_history);
    TtEditEvent delete_pattern={.type=TT_EDIT_PATTERN_DELETE};
    delete_pattern.pattern.index=1;
    assert(tt_edit_apply_recorded(edit_history,&delete_doc,&delete_pattern) &&
           tracker.song.pattern_count==2 && tracker.song.order[1]==1);
    assert(tt_edit_undo(edit_history,&delete_doc) && tracker.song.pattern_count==3 &&
           tracker.song.order[1]==2);
    assert(tt_edit_redo(edit_history,&delete_doc) && tracker.song.pattern_count==2 &&
           tracker.song.order[1]==1);
    delete_pattern.pattern.index=0;
    assert(!tt_edit_apply_recorded(edit_history,&delete_doc,&delete_pattern) &&
           tracker.song.pattern_count==2 && tracker.song.order[1]==1);
    tt_edit_history_free(edit_history);edit_history=NULL;
    f80_free(source_root);
    puts("default song, ordered grid, playback follow and DX7 editor events passed");
    return 0;
}
