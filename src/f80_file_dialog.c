#define _XOPEN_SOURCE 700
#include "f80_file_dialog.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static char recent_files[F80_FILE_DIALOG_RECENT][F80_FILE_DIALOG_PATH_MAX];
static int recent_file_count;
static char recent_storage_path[F80_FILE_DIALOG_PATH_MAX];

static void save_recent_files(void) {
    if(!recent_storage_path[0])return;
    char temporary[F80_FILE_DIALOG_PATH_MAX+16];
    if(snprintf(temporary,sizeof(temporary),"%s.tmpXXXXXX",recent_storage_path)>=
       (int)sizeof(temporary))return;
    int fd=mkstemp(temporary);
    if(fd<0)return;
    FILE *file=fdopen(fd,"w");
    if(!file) { close(fd);unlink(temporary);return; }
    int ok=1;
    for(int i=0;i<recent_file_count;++i)
        if(fprintf(file,"%s\n",recent_files[i])<0)ok=0;
    if(fclose(file))ok=0;
    if(ok && !rename(temporary,recent_storage_path))return;
    unlink(temporary);
}

void f80_file_dialog_recent_storage(const char *path) {
    recent_file_count=0;
    recent_storage_path[0]='\0';
    if(!path || !path[0] || strlen(path)>=sizeof(recent_storage_path))return;
    snprintf(recent_storage_path,sizeof(recent_storage_path),"%s",path);
    FILE *file=fopen(path,"r");
    if(!file)return;
    char line[F80_FILE_DIALOG_PATH_MAX+2];
    while(recent_file_count<F80_FILE_DIALOG_RECENT && fgets(line,sizeof(line),file)) {
        size_t n=strlen(line);
        if(n && line[n-1]=='\n')line[--n]='\0';
        if(!n || n>=F80_FILE_DIALOG_PATH_MAX || strpbrk(line,"\r\n"))continue;
        int duplicate=0;
        for(int i=0;i<recent_file_count;++i)
            if(!strcmp(line,recent_files[i]))duplicate=1;
        if(!duplicate)snprintf(recent_files[recent_file_count++],
                               F80_FILE_DIALOG_PATH_MAX,"%s",line);
    }
    fclose(file);
}

void f80_file_dialog_recent_add(const char *path) {
    if(!path || !path[0] || strlen(path)>=F80_FILE_DIALOG_PATH_MAX ||
       strpbrk(path,"\r\n"))return;
    int existing=-1;
    for(int i=0;i<recent_file_count;++i)
        if(!strcmp(path,recent_files[i])) { existing=i;break; }
    if(existing==0)return;
    int last=existing>=0?existing:
             recent_file_count<F80_FILE_DIALOG_RECENT?
             recent_file_count++:F80_FILE_DIALOG_RECENT-1;
    for(int i=last;i>0;--i) {
        snprintf(recent_files[i],sizeof(recent_files[i]),"%s",recent_files[i-1]);
    }
    snprintf(recent_files[0],sizeof(recent_files[0]),"%s",path);
    save_recent_files();
}

void f80_file_dialog_set_preview(F80FileDialog *state,
                                 F80FilePreviewChanged preview_changed) {
    if(state)state->preview_changed=preview_changed;
}

static int smaller(int a, int b) { return a < b ? a : b; }
static int larger(int a, int b) { return a > b ? a : b; }

static int entry_compare(const void *left, const void *right) {
    const F80FileEntry *a=left, *b=right;
    if(a->directory!=b->directory)return b->directory-a->directory;
    return strcasecmp(a->name,b->name);
}

static int full_path(char *out, size_t capacity, const char *directory,
                     const char *name) {
    int written=name[0]=='/'?snprintf(out,capacity,"%s",name):
        snprintf(out,capacity,"%s/%s",directory,name);
    return written>=0 && (size_t)written<capacity;
}

static int local_browse_list(void *user,const char *directory,
                             F80FileEntry *entries,int capacity,int *count,
                             char *error,size_t error_size) {
    (void)user;
    DIR *dir=opendir(directory);
    *count=0;
    if(!dir) {
        snprintf(error,error_size,"Cannot open directory: %s",strerror(errno));
        return 0;
    }
    struct dirent *item;
    while((item=readdir(dir)) && *count<capacity) {
        if(!strcmp(item->d_name,".") || !strcmp(item->d_name,".."))continue;
        char path[F80_FILE_DIALOG_PATH_MAX];
        struct stat info;
        if(!full_path(path,sizeof(path),directory,item->d_name) || stat(path,&info))continue;
        if(!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode))continue;
        F80FileEntry *entry=&entries[(*count)++];
        snprintf(entry->name,sizeof(entry->name),"%s",item->d_name);
        entry->directory=S_ISDIR(info.st_mode);
        entry->size=entry->directory?0:(unsigned long long)info.st_size;
    }
    closedir(dir);
    error[0]='\0';
    return 1;
}

static int local_browse_join(void *user,const char *directory,const char *name,
                             char *path,size_t path_size) {
    (void)user;
    return full_path(path,path_size,directory,name);
}

static int local_browse_parent(void *user,const char *directory,
                               char *path,size_t path_size) {
    (void)user;
    if(!directory[0] || !strcmp(directory,"/"))return 0;
    int written=snprintf(path,path_size,"%s",directory);
    if(written<0 || (size_t)written>=path_size)return 0;
    char *slash=strrchr(path,'/');
    if(!slash)return 0;
    if(slash==path)slash[1]='\0';
    else *slash='\0';
    return 1;
}

static int local_browse_stat(void *user,const char *path,int *directory,
                             unsigned long long *size) {
    (void)user;
    struct stat info;
    if(stat(path,&info))return 0;
    if(directory)*directory=S_ISDIR(info.st_mode);
    if(size)*size=S_ISREG(info.st_mode)?(unsigned long long)info.st_size:0;
    return S_ISDIR(info.st_mode) || S_ISREG(info.st_mode);
}

static const F80FileBrowseApi local_browse_api={NULL,local_browse_list,
    local_browse_join,local_browse_parent,local_browse_stat};

const F80FileBrowseApi *f80_file_dialog_local_browse_api(void) {
    return &local_browse_api;
}

static int matches_filter(const char *name, const char *filter) {
    if(!filter[0])return 1;
    const char *part=filter;
    while(*part) {
        while(*part==',' || *part==' ' || *part==';')++part;
        if(!*part)break;
        const char *end=part;
        while(*end && *end!=',' && *end!=';')++end;
        while(end>part && end[-1]==' ')--end;
        size_t length=(size_t)(end-part), name_length=strlen(name);
        if(length && name_length>=length &&
           !strncasecmp(name+name_length-length,part,length))return 1;
        part=end;
        while(*part && *part!=',' && *part!=';')++part;
    }
    return 0;
}

static int recent_in_filter(const F80FileDialog *state,const char *path) {
    const char *name=strrchr(path,'/');
    return matches_filter(name?name+1:path,state->filter);
}

static void set_filename(F80FileDialog *state, const char *name) {
    if(!state->input)return;
    snprintf(state->input->input,sizeof(state->input->input),"%s",name);
    state->input->input_pos=(int)strlen(state->input->input);
    state->input->input_scroll=0;
    state->overwrite_path[0]='\0';
}

static void refresh_files(F80FileDialog *state) {
    state->count=0;
    const F80FileBrowseApi *browse=state->browse?state->browse:&local_browse_api;
    char parent_path[F80_FILE_DIALOG_PATH_MAX];
    int has_parent=browse->parent && browse->parent(browse->user,
        state->directory,parent_path,sizeof(parent_path));
    if(has_parent) {
            F80FileEntry *parent=&state->entries[state->count++];
            snprintf(parent->name,sizeof(parent->name),"..");
            parent->directory=1;parent->size=0;
    }
    int first=state->count,listed=0;
    char error[160]="";
    int capacity=F80_FILE_DIALOG_ITEMS-state->count;
    if(!browse->list || !browse->list(browse->user,state->directory,
            state->entries+state->count,capacity,&listed,error,sizeof(error))) {
        snprintf(state->status,sizeof(state->status),"%s",
                 error[0]?error:"Cannot browse this location");
        listed=0;
    }
    if(listed>capacity)listed=capacity;
    state->count+=listed;
    int kept=first;
    for(int i=first;i<state->count;++i) {
        F80FileEntry entry=state->entries[i];
        if(entry.directory || matches_filter(entry.name,state->filter))
            state->entries[kept++]=entry;
    }
    state->count=kept;
    if(error[0])snprintf(state->status,sizeof(state->status),"%s",error);
    else snprintf(state->status,sizeof(state->status),
                 "Tab: file name / files / buttons   Enter: %s   Esc: cancel",
                 state->mode==F80_FILE_SAVE?"save":"open");
    if(state->count>first)
        qsort(state->entries+first,(size_t)(state->count-first),
              sizeof(state->entries[0]),entry_compare);
    for(int i=0;i<state->count;++i) {
        snprintf(state->display[i],sizeof(state->display[i]),"%s%s",
                 state->entries[i].name,state->entries[i].directory?"/":"");
        state->labels_for_list[i]=state->display[i];
    }
    if(state->list) {
        state->list->items=state->labels_for_list;
        state->list->item_count=state->count;
        state->list->cursor=state->list->scroll=0;
    }
}

static void draw_labels(F80Widget *widget, void *user) {
    F80FileDialog *state=user;
    F80Rect r=widget->rect;
    f80_put(r.y,r.x,state->mode==F80_FILE_SAVE?"Save file as":"Open file",r.w,
            F80_COLOR_HIGHLIGHT,1);
    if(r.h>4)f80_put(r.y+4,r.x,"Files",r.w,F80_COLOR_HIGHLIGHT,1);
}

static void draw_list_background(F80Widget *widget, void *user) {
    (void)user;
    f80_fill(widget->rect,F80_COLOR_PANEL);
}

static void draw_preview(F80Widget *widget, void *user) {
    F80FileDialog *state=user;
    F80Rect r=widget->rect;
    f80_fill(r,F80_COLOR_PANEL);
    if(r.w<3)return;
    for(int y=r.y;y<r.y+r.h;++y)f80_put(y,r.x,"│",1,F80_COLOR_TEXT,0);
    if(state->count<=0 || !state->list)return;
    int index=state->list->cursor;
    if(index<0 || index>=state->count)return;
    const F80FileEntry *entry=&state->entries[index];
    f80_put(r.y+1,r.x+2,entry->name,r.w-3,F80_COLOR_HIGHLIGHT,1);
    if(r.h>3) {
        char detail[80];
        if(entry->directory)snprintf(detail,sizeof(detail),"Directory");
        else snprintf(detail,sizeof(detail),"%llu bytes",entry->size);
        f80_put(r.y+3,r.x+2,detail,r.w-3,F80_COLOR_TEXT,0);
    }
}

static void draw_footer(F80Widget *widget, void *user) {
    F80FileDialog *state=user;
    F80Rect r=widget->rect;
    f80_fill(r,F80_COLOR_PANEL);
    f80_put(r.y,r.x,state->directory,r.w,F80_COLOR_TEXT,0);
    if(r.h>1)f80_put(r.y+1,r.x,state->status,r.w,F80_COLOR_ACCENT,0);
}

int f80_file_dialog_active(const F80FileDialog *state) {
    return state && state->dialog && state->app && state->app->modal==state->dialog;
}

void f80_file_dialog_close(F80FileDialog *state) {
    if(!f80_file_dialog_active(state))return;
    if(state->preview_changed)state->preview_changed(NULL,state->user);
    F80App *app=state->app;
    f80_autocomplete_close(&state->autocomplete);
    state->dialog=state->labels=state->list_background=state->list=NULL;
    state->preview=state->footer=state->input=state->dropdown=NULL;
    state->accept=state->cancel=state->help=NULL;
    f80_hide_modal(app);
    state->autocomplete.panel=NULL;
}

static void cancel_action(F80Widget *button, void *user) {
    (void)button;f80_file_dialog_close(user);
}

static void help_action(F80Widget *button, void *user) {
    (void)button;
    F80FileDialog *state=user;
    snprintf(state->status,sizeof(state->status),
             "▼: recent files; Enter fills the name; edit it or press Open/Save");
}

static void dropdown_action(F80Widget *button, void *user) {
    (void)button;
    F80FileDialog *state=user;
    if(!f80_file_dialog_active(state))return;
    if(state->autocomplete.active) { f80_autocomplete_close(&state->autocomplete);return; }
    state->recent_count=0;
    for(int i=0;i<recent_file_count;++i)
        if(recent_in_filter(state,recent_files[i]))
            state->recent_items[state->recent_count++]=recent_files[i];
    f80_autocomplete_set(&state->autocomplete,state->recent_items,state->recent_count,1);
    f80_autocomplete_open(&state->autocomplete);
    f80_autocomplete_layout(&state->autocomplete,state->input->rect,
                             f80_inset(state->dialog->rect,2,1),10);
    state->app->focus=state->input;
}

static void recent_chosen(int index, void *user) {
    F80FileDialog *state=user;
    if(index<0 || index>=state->recent_count)return;
    const char *path=state->recent_items[index];
    const F80FileBrowseApi *browse=state->browse?state->browse:&local_browse_api;
    int is_directory=0;
    if(browse->stat && browse->stat(browse->user,path,&is_directory,NULL) &&
       is_directory) {
        if(strlen(path)<sizeof(state->directory)) {
            snprintf(state->directory,sizeof(state->directory),"%s",path);
            refresh_files(state);
            set_filename(state,"");
        }
    } else {
        char parent[F80_FILE_DIALOG_PATH_MAX];int parent_is_directory=0;
        int navigated=browse->parent && browse->stat &&
           browse->parent(browse->user,path,parent,sizeof(parent)) &&
           browse->stat(browse->user,parent,&parent_is_directory,NULL) &&
           parent_is_directory;
        if(navigated) {
            snprintf(state->directory,sizeof(state->directory),"%s",parent);
            refresh_files(state);
        }
        const char *bang=strchr(path,'!');
        const char *slash=strrchr(path,'/');
        const char *name=!navigated?path:
                         bang && (!slash || slash<bang)?bang+1:
                         slash?slash+1:path;
        set_filename(state,name);
    }
    snprintf(state->status,sizeof(state->status),
             "Recent location opened; edit the name or press %s",
             state->mode==F80_FILE_SAVE?"Save":"Open");
    state->app->focus=state->input;
}

static int recent_matches(const char *path,const char *query) {
    if(!query[0])return 1;
    const char *name=strrchr(path,'/');
    if(name)path=name+1;
    for(const char *at=path;*at;++at)
        if(!strncasecmp(at,query,strlen(query)))return 1;
    return 0;
}

static void filter_recent(F80FileDialog *state) {
    state->recent_count=0;
    for(int i=0;i<recent_file_count;++i)
        if(recent_in_filter(state,recent_files[i]) &&
           recent_matches(recent_files[i],state->input->input))
            state->recent_items[state->recent_count++]=recent_files[i];
    f80_autocomplete_set(&state->autocomplete,state->recent_items,state->recent_count,1);
}

static void enter_directory(F80FileDialog *state, const char *name) {
    char path[F80_FILE_DIALOG_PATH_MAX];int directory=0;
    const F80FileBrowseApi *browse=state->browse?state->browse:&local_browse_api;
    int path_ok=!strcmp(name,"..")?
        (browse->parent && browse->parent(browse->user,state->directory,
                                          path,sizeof(path))):
        (browse->join && browse->join(browse->user,state->directory,name,
                                      path,sizeof(path)));
    if(!path_ok || !browse->stat ||
       !browse->stat(browse->user,path,&directory,NULL) || !directory) {
        snprintf(state->status,sizeof(state->status),"Cannot open directory");return;
    }
    if(state->preview_changed)state->preview_changed(NULL,state->user);
    snprintf(state->directory,sizeof(state->directory),"%s",path);
    set_filename(state,"");
    refresh_files(state);
    state->app->focus=state->list;
}

static void accept_action(F80Widget *button, void *user) {
    (void)button;
    F80FileDialog *state=user;
    if(!f80_file_dialog_active(state))return;
    const char *name=state->input->input;
    if(!name[0]) {
        snprintf(state->status,sizeof(state->status),"Enter a file name or select a file");return;
    }
    char path[F80_FILE_DIALOG_PATH_MAX];
    const F80FileBrowseApi *browse=state->browse?state->browse:&local_browse_api;
    if(!browse->join || !browse->stat ||
       !browse->join(browse->user,state->directory,name,path,sizeof(path))) {
        snprintf(state->status,sizeof(state->status),"Path is too long");return;
    }
    int is_directory=0;unsigned long long file_size=0;
    int exists=browse->stat(browse->user,path,&is_directory,&file_size);
    if(exists && is_directory) { enter_directory(state,name);return; }
    if(state->mode==F80_FILE_OPEN && !exists) {
        snprintf(state->status,sizeof(state->status),"File not found: %.120s",name);return;
    }
    if(state->mode==F80_FILE_SAVE) {
        if(exists && strcmp(state->overwrite_path,path)) {
            snprintf(state->overwrite_path,sizeof(state->overwrite_path),"%s",path);
            snprintf(state->status,sizeof(state->status),
                     "File exists. Press Save again to replace it.");
            return;
        }
    }
    F80FileChosen chosen=state->chosen;
    void *chosen_user=state->user;
    f80_file_dialog_recent_add(path);
    f80_file_dialog_close(state);
    if(chosen)chosen(path,chosen_user);
}

static void list_changed(F80Widget *list, void *user) {
    F80FileDialog *state=user;
    if(list->cursor>=0 && list->cursor<state->count &&
       !state->entries[list->cursor].directory) {
        set_filename(state,state->entries[list->cursor].name);
        if(state->preview_changed) {
            char path[F80_FILE_DIALOG_PATH_MAX];
            const F80FileBrowseApi *browse=state->browse?state->browse:&local_browse_api;
            if(browse->join && browse->join(browse->user,state->directory,
                    state->entries[list->cursor].name,path,sizeof(path)))
                state->preview_changed(path,state->user);
        }
    } else if(state->preview_changed)state->preview_changed(NULL,state->user);
}

static void list_activated(F80Widget *list, void *user) {
    F80FileDialog *state=user;
    if(list->cursor<0 || list->cursor>=state->count)return;
    const F80FileEntry *entry=&state->entries[list->cursor];
    if(entry->directory)enter_directory(state,entry->name);
    else {
        if(strcmp(state->input->input,entry->name))set_filename(state,entry->name);
        accept_action(NULL,state);
    }
}

int f80_file_dialog_show_browse(F80FileDialog *state,F80App *app,F80FileMode mode,
                                const char *title,const char *initial_path,
                                const char *extensions,
                                const F80FileBrowseApi *browse,
                                F80FileChosen chosen,void *user) {
    if(!state || !app || app->modal_depth>=8 || f80_file_dialog_active(state))return 0;
    memset(state,0,sizeof(*state));
    state->app=app;state->mode=mode;
    state->browse=browse?browse:&local_browse_api;
    if(!state->browse->list || !state->browse->join || !state->browse->stat)
        return 0;
    state->chosen=chosen;state->user=user;
    snprintf(state->title,sizeof(state->title),"%s",title?title:
             mode==F80_FILE_SAVE?"Save File As":"Open File");
    snprintf(state->filter,sizeof(state->filter),"%s",extensions?extensions:"");
    char current[F80_FILE_DIALOG_PATH_MAX];
    if(!getcwd(current,sizeof(current)))return 0;
    const char *name="";
    snprintf(state->directory,sizeof(state->directory),"%s",current);
    if(initial_path && initial_path[0]) {
        int directory=0;
        if(state->browse->stat(state->browse->user,initial_path,&directory,NULL) && directory) {
            if(strlen(initial_path)>=sizeof(state->directory))return 0;
            snprintf(state->directory,sizeof(state->directory),"%s",initial_path);
        } else {
            char parent[F80_FILE_DIALOG_PATH_MAX];
            if(state->browse->parent && state->browse->parent(state->browse->user,initial_path,
                                     parent,sizeof(parent)))
                snprintf(state->directory,sizeof(state->directory),"%s",parent);
            const char *slash=strrchr(initial_path,'/');
            name=slash?slash+1:initial_path;
        }
    }
    if(!state->browse->stat(state->browse->user,state->directory,&(int){0},NULL))
        snprintf(state->directory,sizeof(state->directory),"%s",current);
    state->dialog=f80_dialog(state->title,NULL,NULL,0,0);
    if(!state->dialog)return 0;
    state->dialog->dialog_min_width=40;
    state->labels=f80_add(state->dialog,f80_canvas(draw_labels,state));
    state->list_background=f80_add(state->dialog,f80_canvas(draw_list_background,state));
    state->list_background->window_role=F80_WINDOW_CYAN;
    state->preview=f80_add(state->dialog,f80_canvas(draw_preview,state));
    state->preview->window_role=F80_WINDOW_CYAN;
    state->footer=f80_add(state->dialog,f80_canvas(draw_footer,state));
    state->footer->window_role=F80_WINDOW_BLUE;
    state->list=f80_add(state->dialog,f80_listbox(state->labels_for_list,0,list_changed,state));
    state->list->window_role=F80_WINDOW_CYAN;
    f80_palette_set(&state->list->palette,F80_COLOR_SELECTION,26);
    state->list->activate=list_activated;
    state->input=f80_add(state->dialog,f80_input());
    set_filename(state,name);
    state->dropdown=f80_add(state->dialog,f80_button("▼",dropdown_action,state));
    state->dropdown->button_shadow=0;
    state->accept=f80_add(state->dialog,f80_button(mode==F80_FILE_SAVE?"~S~ave":"~O~pen",accept_action,state));
    state->cancel=f80_add(state->dialog,f80_button("~C~ancel",cancel_action,state));
    state->help=f80_add(state->dialog,f80_button("~H~elp",help_action,state));
    if(!f80_autocomplete_attach(&state->autocomplete,state->dialog,recent_chosen,state)) {
        f80_free(state->dialog);state->dialog=NULL;return 0;
    }
    refresh_files(state);
    f80_show_modal(app,state->dialog);
    app->focus=state->input;
    return 1;
}

int f80_file_dialog_show(F80FileDialog *state,F80App *app,F80FileMode mode,
                         const char *title,const char *initial_path,
                         const char *extensions,F80FileChosen chosen,void *user) {
    return f80_file_dialog_show_browse(state,app,mode,title,initial_path,
        extensions,NULL,chosen,user);
}

void f80_file_dialog_layout(F80FileDialog *state, int columns, int rows) {
    if(!f80_file_dialog_active(state))return;
    int width=smaller(78,columns-2),height=smaller(26,rows-2);
    if(width<24 || height<11)return;
    F80Rect box=f80_rect((columns-width)/2,(rows-height)/2,width,height);
    f80_layout(state->dialog,box);
    int compact=height<17;
    int right=width>=52?16:12;
    int list_width=width-right-5;
    int list_top=compact?6:7;
    int list_height=compact?height-9:height-12;
    f80_layout(state->labels,f80_rect(box.x+3,box.y+(compact?1:2),list_width,5));
    f80_layout(state->list_background,f80_rect(box.x+3,box.y+list_top,list_width,list_height));
    f80_layout(state->list,f80_rect(box.x+3,box.y+list_top,larger(4,list_width/2),list_height));
    f80_layout(state->preview,f80_rect(box.x+3+larger(4,list_width/2),box.y+list_top,
                                        list_width-larger(4,list_width/2),list_height));
    f80_layout(state->footer,f80_rect(box.x+2,box.y+height-3,width-4,2));
    f80_layout(state->input,f80_rect(box.x+3,box.y+(compact?3:4),list_width-4,1));
    f80_layout(state->dropdown,f80_rect(box.x+list_width,box.y+(compact?3:4),3,1));
    int button_x=box.x+width-right+1,button_width=right-3;
    f80_layout(state->accept,f80_rect(button_x,box.y+(compact?3:4),button_width,2));
    f80_layout(state->cancel,f80_rect(button_x,box.y+(compact?height-5:height-10),button_width,2));
    f80_layout(state->help,f80_rect(button_x,box.y+height-6,button_width,2));
    state->help->visible=!compact;
    f80_autocomplete_layout(&state->autocomplete,state->input->rect,
                             f80_inset(box,2,1),10);
}

int f80_file_dialog_key(F80FileDialog *state, int key) {
    if(!f80_file_dialog_active(state))return 0;
    if(state->autocomplete.active) {
        if(f80_autocomplete_key(&state->autocomplete,key))return 1;
        if(state->autocomplete.active && state->app->focus==state->input &&
           ((key>=32 && key<127) || key==F80_KEY_BACKSPACE || key==F80_KEY_DELETE)) {
            int handled=f80_key(state->app,key);
            filter_recent(state);
            return handled;
        }
    }
    if(key==27) { f80_file_dialog_close(state);return 1; }
    if(key==9 || key==F80_KEY_BTAB) {
        F80Widget *cycle[]={state->input,state->dropdown,state->list,
                            state->accept,state->cancel,state->help};
        int count=state->help->visible?6:5;
        int index=0;
        for(int i=0;i<count;++i)if(state->app->focus==cycle[i])index=i;
        state->app->focus=cycle[(index+(key==9?1:count-1))%count];
        return 1;
    }
    if(state->app->focus==state->list) {
        if(key==F80_KEY_LEFT) { state->app->focus=state->input;return 1; }
        if(key==F80_KEY_RIGHT) { state->app->focus=state->accept;return 1; }
        if(key==10 || key==13) { list_activated(state->list,state);return 1; }
        if(key==F80_KEY_UP || key==F80_KEY_DOWN || key==F80_KEY_HOME ||
           key==F80_KEY_END || key==F80_KEY_PPAGE || key==F80_KEY_NPAGE) {
            int cursor=state->list->cursor;
            int page=larger(1,state->list->rect.h);
            if(key==F80_KEY_UP)--cursor;
            else if(key==F80_KEY_DOWN)++cursor;
            else if(key==F80_KEY_HOME)cursor=0;
            else if(key==F80_KEY_END)cursor=state->count-1;
            else if(key==F80_KEY_PPAGE)cursor-=page;
            else cursor+=page;
            if(cursor<0)cursor=0;
            if(cursor>=state->count)cursor=state->count?state->count-1:0;
            state->list->cursor=cursor;
            list_changed(state->list,state);
            return 1;
        }
    }
    if(state->app->focus==state->input && (key==10 || key==13)) {
        accept_action(NULL,state);return 1;
    }
    if(state->app->focus==state->input &&
       ((key>=32 && key<127) || key==F80_KEY_BACKSPACE || key==F80_KEY_DELETE)) {
        state->overwrite_path[0]='\0';
    }
    return f80_key(state->app,key);
}

int f80_file_dialog_mouse(F80FileDialog *state, int x, int y, int wheel) {
    if(!f80_file_dialog_active(state))return 0;
    if(state->autocomplete.active) {
        if(!wheel && f80_contains(state->dropdown->rect,x,y)) {
            f80_autocomplete_close(&state->autocomplete);return 1;
        }
        if(f80_autocomplete_mouse(&state->autocomplete,x,y,wheel))return 1;
    }
    return f80_mouse(state->app,x,y,wheel);
}
