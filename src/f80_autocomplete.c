#include "f80_autocomplete.h"

#include <string.h>

static int clamp(int value, int low, int high) {
    return value<low?low:value>high?high:value;
}

static int rows(const F80Autocomplete *state) {
    return (state->count+state->columns-1)/state->columns;
}

static F80Rect content_rect(const F80Autocomplete *state) {
    F80Rect rect=state->panel->rect;
    if(rect.w>=3 && rect.h>=3)
        return f80_inset(rect,1,1);
    return rect;
}

static int visible_rows(const F80Autocomplete *state) {
    return content_rect(state).h;
}

static int in_dialog(const F80Autocomplete *state) {
    for(const F80Widget *widget=state->panel->parent;widget;widget=widget->parent)
        if(widget->kind==F80_DIALOG)return 1;
    return 0;
}

static void reveal(F80Autocomplete *state) {
    if(!state->panel || state->cursor<0)return;
    int row=state->cursor/state->columns;
    int visible=visible_rows(state);
    if(row<state->scroll_row)state->scroll_row=row;
    if(row>=state->scroll_row+visible)
        state->scroll_row=row-visible+1;
    state->scroll_row=clamp(state->scroll_row,0,
                            rows(state)>visible?rows(state)-visible:0);
}

static void paint(F80Widget *panel, void *user) {
    F80Autocomplete *state=user;
    F80Rect rect=panel->rect;
    f80_fill(rect,F80_COLOR_PANEL);
    if(rect.w>=3 && rect.h>=3)
        f80_frame(rect,NULL,F80_COLOR_HIGHLIGHT);
    rect=content_rect(state);
    int scrolling=rows(state)>rect.h && rect.w>1;
    int list_width=rect.w-(scrolling?1:0);
    if(!state->count) {
        f80_put(rect.y,rect.x,"(no suggestions)",list_width,F80_COLOR_TEXT,0);
        return;
    }
    int width=list_width/state->columns;
    for(int row=0;row<rect.h;++row)for(int column=0;column<state->columns;++column) {
        int index=(state->scroll_row+row)*state->columns+column;
        if(index>=state->count)continue;
        int x=rect.x+column*width;
        int cells=column==state->columns-1?rect.x+list_width-x:width;
        f80_put(rect.y+row,x,state->items[index],cells,
                index==state->cursor?F80_COLOR_SELECTION:F80_COLOR_TEXT,0);
    }
    if(scrolling) {
        int track=rect.h-2;
        int thumb=track>0?track*rect.h/rows(state):0;
        if(track>0 && thumb<1)thumb=1;
        int at=track>thumb?(track-thumb)*state->scroll_row/(rows(state)-rect.h):0;
        int x=rect.x+rect.w-1;
        f80_put(rect.y,x,"▲",1,F80_COLOR_HIGHLIGHT,1);
        for(int i=0;i<track;++i)
            f80_put(rect.y+1+i,x,i>=at && i<at+thumb?"█":"▒",1,F80_COLOR_HIGHLIGHT,0);
        if(rect.h>1)f80_put(rect.y+rect.h-1,x,"▼",1,F80_COLOR_HIGHLIGHT,1);
    }
}

int f80_autocomplete_attach(F80Autocomplete *state, F80Widget *parent,
                            F80AutocompleteChosen chosen, void *user) {
    if(!state || !parent)return 0;
    memset(state,0,sizeof(*state));
    state->columns=1;state->cursor=-1;state->chosen=chosen;state->user=user;
    state->panel=f80_add(parent,f80_canvas(paint,state));
    if(!state->panel)return 0;
    state->panel->window_role=F80_WINDOW_PURPLE;
    state->panel->visible=0;
    return 1;
}

void f80_autocomplete_set(F80Autocomplete *state, const char *const *items,
                          int count, int columns) {
    if(!state)return;
    state->items=items;
    state->count=items && count>0?count:0;
    state->columns=columns>0?columns:1;
    if(state->cursor>=state->count)state->cursor=state->count-1;
    if(state->cursor<0 && state->count)state->cursor=0;
    if(!state->count)state->cursor=-1;
    reveal(state);
}

void f80_autocomplete_open(F80Autocomplete *state) {
    if(!state || !state->panel)return;
    state->active=state->panel->visible=1;
    state->cursor=state->count?0:-1;
    state->scroll_row=0;
}

void f80_autocomplete_close(F80Autocomplete *state) {
    if(!state || !state->panel)return;
    state->active=state->panel->visible=0;
}

void f80_autocomplete_layout(F80Autocomplete *state, F80Rect anchor,
                             F80Rect bounds, int max_rows) {
    if(!state || !state->active || !state->panel)return;
    int count_rows=rows(state);
    if(!count_rows)count_rows=1;
    int below=bounds.y+bounds.h-(anchor.y+anchor.h);
    int above=anchor.y-bounds.y;
    if(below<0)below=0;
    if(above<0)above=0;
    int wanted=clamp(count_rows+2,3,max_rows>2?max_rows:10);
    int use_below=in_dialog(state);
    int preferred=use_below?below:above;
    int alternate=use_below?above:below;
    if(preferred<wanted && (alternate>=wanted || alternate>preferred))
        use_below=!use_below;
    int available=use_below?below:above;
    int height=wanted;
    if(height>available)height=available;
    if(height<1)height=1;
    int desired=anchor.w+2;
    for(int i=0;i<state->count;++i) {
        int length=(int)strlen(state->items[i])+4;
        if(length>desired)desired=length;
    }
    int width=clamp(desired,1,bounds.w);
    int x=anchor.x;
    if(x+width>bounds.x+bounds.w)x=bounds.x+bounds.w-width;
    if(x<bounds.x)x=bounds.x;
    int y=use_below?anchor.y+anchor.h:anchor.y-height;
    if(y<bounds.y)y=bounds.y;
    if(y+height>bounds.y+bounds.h)y=bounds.y+bounds.h-height;
    f80_layout(state->panel,f80_rect(x,y,width,height));
    state->max_rows=max_rows;
    reveal(state);
}

static void accept(F80Autocomplete *state) {
    int index=state->cursor;
    f80_autocomplete_close(state);
    if(index>=0 && state->chosen)state->chosen(index,state->user);
}

int f80_autocomplete_key(F80Autocomplete *state, int key) {
    if(!state || !state->active)return 0;
    if(key==27) { f80_autocomplete_close(state);return 1; }
    if(key==10 || key==13) { accept(state);return 1; }
    if(key==9 || key==F80_KEY_BTAB) { f80_autocomplete_close(state);return 0; }
    if(!state->count)return 0;
    int next=state->cursor;
    if(key==F80_KEY_UP)next-=state->columns;
    else if(key==F80_KEY_DOWN)next+=state->columns;
    else if(key==F80_KEY_LEFT)next--;
    else if(key==F80_KEY_RIGHT)next++;
    else if(key==F80_KEY_HOME)next=0;
    else if(key==F80_KEY_END)next=state->count-1;
    else if(key==F80_KEY_PPAGE)next-=visible_rows(state)*state->columns;
    else if(key==F80_KEY_NPAGE)next+=visible_rows(state)*state->columns;
    else return 0;
    state->cursor=clamp(next,0,state->count-1);
    reveal(state);
    return 1;
}

int f80_autocomplete_mouse(F80Autocomplete *state, int x, int y, int wheel) {
    if(!state || !state->active || !state->panel)return 0;
    F80Rect rect=state->panel->rect;
    if(!f80_contains(rect,x,y)) {
        if(!wheel)f80_autocomplete_close(state);
        return 0;
    }
    if(wheel) {
        int visible=visible_rows(state);
        state->scroll_row=clamp(state->scroll_row+wheel,0,
                                rows(state)>visible?rows(state)-visible:0);
        if(state->count) {
            int selected_row=state->cursor/state->columns;
            if(selected_row<state->scroll_row)
                state->cursor=state->scroll_row*state->columns;
            else if(selected_row>=state->scroll_row+visible)
                state->cursor=(state->scroll_row+visible-1)*state->columns;
            if(state->cursor>=state->count)state->cursor=state->count-1;
        }
        return 1;
    }
    rect=content_rect(state);
    if(!f80_contains(rect,x,y))return 1;
    if(rows(state)>rect.h && rect.w>1 && x==rect.x+rect.w-1) {
        state->scroll_row=clamp(state->scroll_row+(y==rect.y?-1:y==rect.y+rect.h-1?1:0),
                                0,rows(state)-rect.h);
        return 1;
    }
    int list_width=rect.w-((rows(state)>rect.h && rect.w>1)?1:0);
    int width=list_width/state->columns;
    int column=width>0?clamp((x-rect.x)/width,0,state->columns-1):0;
    int index=(state->scroll_row+y-rect.y)*state->columns+column;
    if(index>=0 && index<state->count) {
        state->cursor=index;
        accept(state);
    }
    return 1;
}
