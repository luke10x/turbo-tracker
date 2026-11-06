#define WEB_CURSES_IMPLEMENTATION
#include <curses.h>
#include "f80.h"
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, f80_web_clear_inputs, (), {
    Module.f80InputRects = [];
    Module.f80GridRect = null;
    Module.f80EditableGridRect = null;
    Module.f80WaveRect = null;
    Module.f80OrderRect = null;
});
EM_JS(void, f80_web_add_input, (int x, int y, int width, int height), {
    if (Module.f80InputRects) Module.f80InputRects.push({x, y, width, height});
});
#endif

static int limit(int value, int lo, int hi) { return value < lo ? lo : value > hi ? hi : value; }
static int hot_label(const char *source,char *out,int cap,int *hot);
static const F80Widget *paint_widget;
#ifndef __EMSCRIPTEN__
typedef struct { int x,y,pair; unsigned code; } F80RawGlyph;
static F80RawGlyph *raw_glyphs;
static size_t raw_count,raw_capacity;
static size_t previous_raw_count;
static void raw_glyph(int x,int y,int pair,unsigned code) {
    if(raw_count==raw_capacity) {
        size_t capacity=raw_capacity?raw_capacity*2:256;
        F80RawGlyph *next=(F80RawGlyph *)realloc(raw_glyphs,capacity*sizeof(*next));
        if(!next)return;
        raw_glyphs=next; raw_capacity=capacity;
    }
    raw_glyphs[raw_count++]=(F80RawGlyph){x,y,pair,code};
}
static void flush_raw_glyphs(const F80App *app) {
    int rows,cols; getmaxyx(stdscr,rows,cols);
    for(size_t i=0;i<raw_count;++i) {
        F80RawGlyph g=raw_glyphs[i];
        if(g.x<0 || g.y<0 || g.x>=cols || g.y>=rows)continue;
        int covered=0;
        for(int j=0;j<app->modal_depth;++j) {
            F80Rect r=app->modal_stack[j]->rect;
            if(f80_contains(f80_rect(r.x,r.y,r.w+2,r.h+1),g.x,g.y)) { covered=1; break; }
        }
        if(covered)continue;
        short fg=COLOR_WHITE,bg=COLOR_BLUE;
        pair_content((short)g.pair,&fg,&bg);
        char glyph[5]={(char)(0xf0u|(g.code>>18)),(char)(0x80u|((g.code>>12)&63u)),
                       (char)(0x80u|((g.code>>6)&63u)),(char)(0x80u|(g.code&63u)),0};
        printf("\033[%d;%dH\033[1;%d;%dm%s",g.y+1,g.x+1,30+fg,40+bg,glyph);
    }
    if(raw_count) { fputs("\033[0m",stdout); fflush(stdout); }
}
#endif

/* These pairs are installed by f80_colors. Slot zero means no remapping. */
static const int role_pairs[F80_WINDOW_ROLE_COUNT][F80_PALETTE_SLOTS] = {
    {0},
    {0},
    /* cyan */
    {0,24,24,27,26,30,24,24,28,29,25,24,0,29,27,24,24,0,31,27,26,30,24,0},
    /* gray */
    {0,32,32,35,34,38,32,32,36,37,33,32,0,37,35,32,32,0,39,35,34,38,32,0},
    /* console */
    {0,40,40,43,42,46,40,40,44,45,41,40,0,45,43,40,40,0,47,43,42,46,40,0},
    /* purple */
    {0,48,48,51,50,54,48,48,52,53,49,48,0,53,51,48,48,0,55,51,50,54,48,0}
};

F80Widget *f80_window(F80WindowRole role, F80Direction direction) {
    F80Widget *w=f80_box(direction);
    if(w) { w->window_role=role>F80_WINDOW_INHERIT && role<F80_WINDOW_ROLE_COUNT?role:F80_WINDOW_BLUE; w->fill=1; }
    return w;
}
void f80_palette_set(F80Palette *palette,int slot,int pair) {
    if(palette && slot>0 && slot<F80_PALETTE_SLOTS && pair>=0)palette->pair[slot]=pair;
}
int f80_palette_pair(const F80Widget *widget,int slot) {
    if(slot<=0 || slot>=F80_PALETTE_SLOTS)return slot;
    F80WindowRole role=F80_WINDOW_BLUE;
    for(const F80Widget *w=widget;w;w=w->parent) {
        if(w->palette.pair[slot])return w->palette.pair[slot];
        if(w->window_role!=F80_WINDOW_INHERIT) { role=w->window_role; break; }
    }
    if(role<=F80_WINDOW_INHERIT || role>=F80_WINDOW_ROLE_COUNT)role=F80_WINDOW_BLUE;
    if(widget && widget->app && widget->app->palettes[role].pair[slot])
        return widget->app->palettes[role].pair[slot];
    /* The floating-window shadow is solid; button half-blocks use the
       window background so their shaded edges remain visible. */
    if(slot==F80_COLOR_DIALOG_SHADOW)return 17;
    if(slot==F80_COLOR_ALERT_SHADOW)return 23;
    if(slot==F80_COLOR_SHADOW) {
        static const int button_shadow[]={0,12,56,57,58,59};
        return button_shadow[role];
    }
    if(slot==F80_COLOR_BUTTON_SHADOW_BOTTOM) {
        static const int inverted_shadow[]={0,60,61,16,7,62};
        return inverted_shadow[role];
    }
    if(slot==F80_COLOR_ALERT_SHADOW_BOTTOM)return 63;
    if(slot==F80_COLOR_FORM_FIELD || slot==F80_COLOR_FORM_FOCUS)return 3;
    if(slot==F80_COLOR_FORM_HOT)return 64;
    if(slot==F80_COLOR_FORM_INPUT)return 2;
    if(slot==F80_COLOR_INPUT_OVERFLOW) {
        if(widget && widget->style==F80_COLOR_FORM_INPUT)return 8;
        if(widget && widget->style==F80_COLOR_FORM_FIELD)return 28;
        return role_pairs[role][F80_COLOR_POSITIVE]?
               role_pairs[role][F80_COLOR_POSITIVE]:F80_COLOR_POSITIVE;
    }
    return role_pairs[role][slot]?role_pairs[role][slot]:slot;
}

F80Size f80_fit(void) { return (F80Size){ F80_FIT, 0, 0, 1000000 }; }
F80Size f80_grow(void) { return (F80Size){ F80_GROW, 0, 0, 1000000 }; }
F80Size f80_fixed(int n) { return (F80Size){ F80_FIXED, (float)n, 0, 1000000 }; }
F80Size f80_percent(float f) { return (F80Size){ F80_PERCENT, f, 0, 1000000 }; }
F80Rect f80_rect(int x, int y, int w, int h) { return (F80Rect){ x, y, w > 0 ? w : 0, h > 0 ? h : 0 }; }
F80Rect f80_inset(F80Rect r, int x, int y) { return f80_rect(r.x + x, r.y + y, r.w - 2*x, r.h - 2*y); }
int f80_contains(F80Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x+r.w && y < r.y+r.h; }
F80Rect f80_content_rect(const F80Widget *w) {
    if(!w)return f80_rect(0,0,0,0);
    F80Rect r=w->rect;
    if(w->kind==F80_LISTBOX || w->kind==F80_TABLE) {
        int gutter=limit(w->gutter_width,0,r.w>0?r.w-1:0);
        r.x+=gutter; r.w-=gutter;
    }
    return r;
}

F80Widget *f80_widget(F80WidgetKind kind) {
    F80Widget *w = (F80Widget *)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->kind = kind; w->visible = w->enabled = 1;
    w->width = f80_grow(); w->height = f80_grow();
    w->align = F80_STRETCH;
    return w;
}
F80Widget *f80_box(F80Direction direction) { F80Widget *w = f80_widget(F80_BOX); if (w) w->direction = direction; return w; }
F80Widget *f80_spacer(void) { return f80_widget(F80_SPACER); }
F80Widget *f80_label(const char *text) { F80Widget *w = f80_widget(F80_LABEL); if (w) { w->text = text; w->height = f80_fit(); } return w; }
F80Widget *f80_button(const char *text, F80Action action, void *user) {
    F80Widget *w = f80_widget(F80_BUTTON);
    if (w) { w->text = text; w->action = action; w->user = user; w->width = w->height = f80_fit(); w->focusable = 1; }
    return w;
}
F80Widget *f80_listbox(const char **items, int count, F80Action action, void *user) {
    F80Widget *w = f80_widget(F80_LISTBOX);
    if (w) { w->items = items; w->item_count = count; w->action = action; w->user = user; w->focusable = 1; }
    return w;
}
F80Widget *f80_radio_group(const char **items,int count,int selected,F80Action action,void *user) {
    F80Widget *w=f80_widget(F80_RADIO_GROUP);
    if(w) {
        w->items=items; w->item_count=count>0?count:0;
        w->selected=w->item_count?limit(selected,0,w->item_count-1):-1;
        w->cursor=w->selected>=0?w->selected:0;
        w->action=action; w->user=user; w->focusable=1;
    }
    return w;
}
F80Widget *f80_check_group(const char **items,int count,F80Action action,void *user) {
    F80Widget *w=f80_widget(F80_CHECK_GROUP);
    if(!w)return NULL;
    w->items=items; w->item_count=count>0?count:0;
    w->action=action; w->user=user; w->focusable=1;
    if(w->item_count) {
        w->checked=(unsigned char *)calloc((size_t)w->item_count,1);
        if(!w->checked) { free(w); return NULL; }
    }
    return w;
}
int f80_checked(const F80Widget *w,int index) {
    return w && w->kind==F80_CHECK_GROUP && w->checked &&
           index>=0 && index<w->item_count && w->checked[index]!=0;
}
void f80_set_checked(F80Widget *w,int index,int checked) {
    if(w && w->kind==F80_CHECK_GROUP && w->checked &&
       index>=0 && index<w->item_count)w->checked[index]=checked!=0;
}
F80Widget *f80_progress(float value, int indeterminate) {
    F80Widget *w = f80_widget(F80_PROGRESS);
    if (w) { w->value = value; w->indeterminate = indeterminate; w->height = f80_fixed(1); }
    return w;
}
F80Widget *f80_table(const char **rows, int count) {
    F80Widget *w = f80_widget(F80_TABLE);
    if (w) { w->items = rows; w->item_count = count; w->focusable = 1; w->table_sort_col=-1; }
    return w;
}
void f80_table_columns(F80Widget *w, const int *starts, int count) {
    if(!w || w->kind!=F80_TABLE || !starts)return;
    w->table_col_count=limit(count,0,16);
    for(int i=0;i<w->table_col_count;++i)w->table_starts[i]=starts[i];
    w->table_col=0;
}
void f80_table_grid(F80Widget *w, const F80TableField *fields, int count,
                    const char *const *const *rows) {
    if(!w || w->kind!=F80_TABLE)return;
    w->table_fields=fields; w->table_rows=rows;
    w->table_col_count=limit(count,0,16);
    w->table_col=w->table_col_scroll=0;
}
const char *f80_table_cell(const F80Widget *w,int row,int column) {
    if(!w || !w->table_rows || row<0 || row>=w->item_count ||
       column<0 || column>=w->table_col_count)return "";
    const char *value=w->table_rows[row][column];
    return value?value:"";
}
static int numeric_value(const char *text,double *value) {
    char buffer[128]; size_t at=0;
    while(*text && at<sizeof(buffer)-1) {
        if(*text!=',')buffer[at++]=*text;
        ++text;
    }
    buffer[at]='\0';
    char *end; *value=strtod(buffer,&end);
    while(isspace((unsigned char)*end))++end;
    return end!=buffer && !*end;
}
static int compare_cells(const char *a,const char *b) {
    double av,bv;
    if(numeric_value(a,&av) && numeric_value(b,&bv))return av<bv?-1:av>bv?1:0;
    while(*a && *b) {
        int ca=tolower((unsigned char)*a++),cb=tolower((unsigned char)*b++);
        if(ca!=cb)return ca<cb?-1:1;
    }
    return *a?1:*b?-1:0;
}
void f80_table_sort(F80Widget *w,int column) {
    if(!w || !w->table_rows || column<0 || column>=w->table_col_count)return;
    if(w->table_sort_col==column)w->table_sort_desc=!w->table_sort_desc;
    else { w->table_sort_col=column; w->table_sort_desc=0; }
    const char *const **sorted=(const char *const **)malloc((size_t)w->item_count*sizeof(*sorted));
    if(!sorted)return;
    for(int i=0;i<w->item_count;++i)sorted[i]=w->table_rows[i];
    for(int i=1;i<w->item_count;++i) {
        const char *const *row=sorted[i]; int j=i;
        while(j>0) {
            int cmp=compare_cells(sorted[j-1][column],row[column]);
            if(w->table_sort_desc)cmp=-cmp;
            if(cmp<=0)break;
            sorted[j]=sorted[j-1]; --j;
        }
        sorted[j]=row;
    }
    free(w->table_sorted); w->table_sorted=sorted; w->table_rows=sorted;
    w->cursor=w->scroll=0;
}
F80Widget *f80_bars(const char **labels, int count) {
    F80Widget *w = f80_widget(F80_BARS);
    if (w) { w->items = labels; w->item_count = count; w->direction=F80_COL; }
    return w;
}
void f80_bars_values(F80Widget *w,const int *values) { if(w && w->kind==F80_BARS)w->bar_values=values; }
F80Widget *f80_linechart(void) { F80Widget *w=f80_widget(F80_LINECHART); if(w)w->chart_legend=1; return w; }
F80Widget *f80_canvas(F80Paint paint, void *user) { F80Widget *w = f80_widget(F80_CANVAS); if (w) { w->paint = paint; w->user = user; } return w; }
F80Widget *f80_input(void) { F80Widget *w = f80_widget(F80_INPUT); if (w) { w->height = f80_fixed(1); w->focusable = 1; w->style = F80_COLOR_FORM_INPUT; } return w; }
F80Widget *f80_menubar(const F80Menu *menus, int count) {
    F80Widget *w=f80_widget(F80_MENUBAR);
    if(w) { w->menus=menus; w->menu_count=count; w->height=f80_fixed(1); }
    return w;
}
F80Widget *f80_statusbar(const F80StatusItem *items, int count) {
    F80Widget *w=f80_widget(F80_STATUSBAR);
    if(w) { w->status_items=items; w->status_count=count; w->height=f80_fixed(1); }
    return w;
}
F80Widget *f80_dialog(const char *title,const char *const *lines,const int *pairs,int count,int alert) {
    F80Widget *w=f80_widget(F80_DIALOG);
    if(w) { w->title=title; w->dialog_lines=lines; w->dialog_pairs=pairs;
        w->dialog_count=count; w->dialog_min_width=30; w->style=alert?15:7;
        w->window_role=F80_WINDOW_GRAY;
        if(alert)f80_palette_set(&w->palette,F80_COLOR_ALERT,15); }
    return w;
}
static int menu_enabled(const F80MenuItem *item) { return item->label && item->enabled; }
static int hot_label(const char *source,char *out,int cap,int *hot) {
    int n=0; *hot=-1;
    if(!source || cap<1) { if(cap>0)out[0]='\0'; return 0; }
    for(int i=0;source[i] && n<cap-1;++i) {
        if(source[i]=='~' && source[i+1] && source[i+2]=='~') {
            *hot=n; out[n++]=source[i+1]; i+=2; continue;
        }
        if(source[i]=='~')continue;
        out[n++]=source[i];
    }
    out[n]='\0'; return n;
}
void f80_menu_open(F80Widget *w, int index) {
    if(!w || w->kind!=F80_MENUBAR || !w->menu_count)return;
    w->menu_index=(index+w->menu_count)%w->menu_count;
    w->menu_active=1; w->menu_item=0;
    const F80Menu *m=&w->menus[w->menu_index];
    while(w->menu_item<m->count && !menu_enabled(&m->items[w->menu_item]))++w->menu_item;
}
void f80_menu_close(F80Widget *w) { if(w)w->menu_active=0; }

static void attach(F80Widget *w, F80App *app) { if (!w) return; w->app = app; for (size_t i=0; i<w->child_count; ++i) attach(w->children[i], app); }
F80Widget *f80_add(F80Widget *p, F80Widget *c) {
    if (!p || !c) return NULL;
    if (p->child_count == p->child_capacity) {
        size_t cap = p->child_capacity ? p->child_capacity * 2 : 4;
        F80Widget **children = (F80Widget **)realloc(p->children, cap * sizeof(*children));
        if (!children) return NULL;
        p->children = children; p->child_capacity = cap;
    }
    p->children[p->child_count++] = c; c->parent = p; attach(c, p->app); return c;
}
void f80_sized(F80Widget *w, F80Size width, F80Size height) { if (w) { w->width = width; w->height = height; } }
void f80_set_root(F80App *app, F80Widget *root) { app->root = root; attach(root, app); app->focus = NULL; f80_focus_next(app, 1); }
void f80_show_modal(F80App *app, F80Widget *modal) {
    if(!app || !modal || app->modal_depth>=8)return;
    app->modal_focus[app->modal_depth]=app->focus;
    app->modal_stack[app->modal_depth++]=modal;
    app->saved_focus=app->modal_focus[0];
    app->modal=modal; attach(modal,app); app->focus=NULL; f80_focus_next(app,1);
}
void f80_hide_modal(F80App *app) {
    if(!app || app->modal_depth<=0)return;
    int top=--app->modal_depth;
    f80_free(app->modal_stack[top]); app->modal_stack[top]=NULL;
    app->modal=top?app->modal_stack[top-1]:NULL;
    app->focus=app->modal_focus[top]; app->modal_focus[top]=NULL;
    if(!top)app->saved_focus=NULL;
    if(!app->focus)f80_focus_next(app,1);
}
void f80_free(F80Widget *w) { if (!w) return; for (size_t i=0;i<w->child_count;++i) f80_free(w->children[i]); free(w->children); free(w->table_sorted); free(w->checked); free(w); }

void f80_fit_size(F80Widget *w, int *width, int *height) {
    int fw=10, fh=1;
    if (!w) { *width=*height=0; return; }
    if (w->kind == F80_SPACER) { fw=fh=0; }
    else if (w->kind == F80_LABEL) { fw=(int)strlen(w->text ? w->text : ""); fh=1; }
    else if (w->kind == F80_BUTTON) { char label[256]; int hot; fw=hot_label(w->text,label,sizeof(label),&hot)+3; if (fw<9) fw=9; fh=2; }
    else if (w->kind == F80_LISTBOX) { fw=4; fh=w->item_count > 0 ? w->item_count : 1; for(int i=0;i<w->item_count;++i) { int n=(int)strlen(w->items[i])+2; if(n>fw)fw=n; } }
    else if (w->kind == F80_RADIO_GROUP || w->kind == F80_CHECK_GROUP) {
        fw=6; fh=w->item_count>0?w->item_count:1;
        for(int i=0;i<w->item_count;++i) {
            char label[256]; int hot;
            int n=hot_label(w->items && w->items[i]?w->items[i]:"",label,sizeof(label),&hot)+5;
            if(n>fw)fw=n;
        }
    }
    else if (w->kind == F80_INPUT) { fw=10; fh=1; }
    else if (w->kind == F80_DIALOG) {
        fw=w->dialog_min_width; if(w->title && (int)strlen(w->title)+4>fw)fw=(int)strlen(w->title)+4;
        for(int i=0;i<w->dialog_count;++i)if(w->dialog_lines[i] && (int)strlen(w->dialog_lines[i])>fw)fw=(int)strlen(w->dialog_lines[i]);
        int buttons=0, input=0;
        for(size_t i=0;i<w->child_count;++i) {
            F80Widget *c=w->children[i]; int cw,ch; f80_fit_size(c,&cw,&ch);
            if(c->kind==F80_BUTTON) { buttons+=cw+(buttons?2:0); }
            else if(c->kind==F80_INPUT)input=2;
        }
        if(buttons>fw)fw=buttons;
        fw+=4; fh=w->dialog_count+2+(buttons?3:0)+input;
    }
    else if (w->kind == F80_BOX) {
        fw=fh=0; int count=0;
        for(size_t i=0;i<w->child_count;++i) {
            F80Widget *c=w->children[i]; if(!c->visible)continue;
            int cw,ch; f80_fit_size(c,&cw,&ch);
            if(c->width.kind==F80_FIXED)cw=(int)c->width.value;
            if(c->height.kind==F80_FIXED)ch=(int)c->height.value;
            if(w->direction==F80_ROW) { fw+=cw; if(ch>fh)fh=ch; }
            else { if(cw>fw)fw=cw; fh+=ch; }
            ++count;
        }
        if(count>1) { if(w->direction==F80_ROW) fw+=w->gap*(count-1); else fh+=w->gap*(count-1); }
        fw+=2*w->padding_x+(w->frame?2:0);
        fh+=2*w->padding_y+(w->frame?2:0);
        if(w->frame && w->title && (int)strlen(w->title)+4>fw)fw=(int)strlen(w->title)+4;
    }
    *width=fw; *height=fh;
}

static F80Size axis_spec(F80Widget *w, int axis) { return axis==0 ? w->width : w->height; }
static int fit_axis(F80Widget *w, int axis) { int x,y; f80_fit_size(w,&x,&y); return axis==0?x:y; }
void f80_layout(F80Widget *w, F80Rect rect) {
    if(!w)return;
    w->rect=rect;
    if(w->kind==F80_DIALOG) {
        int total=0, count=0;
        for(size_t i=0;i<w->child_count;++i)if(w->children[i]->kind==F80_BUTTON) {
            int cw,ch; f80_fit_size(w->children[i],&cw,&ch); total+=cw; ++count;
        }
        total+=count>1?2*(count-1):0;
        int x=rect.x+(rect.w-total)/2;
        for(size_t i=0;i<w->child_count;++i) {
            F80Widget *c=w->children[i];
            if(c->kind==F80_BUTTON) { int cw,ch; f80_fit_size(c,&cw,&ch);
                f80_layout(c,f80_rect(x,rect.y+rect.h-3,cw,2)); x+=cw+2; }
            else if(c->kind==F80_INPUT)f80_layout(c,f80_rect(rect.x+2,rect.y+1+w->dialog_count,rect.w-4,1));
        }
        return;
    }
    if(w->kind!=F80_BOX || !w->child_count)return;
    F80Rect inner=f80_inset(rect,w->padding_x+(w->frame?1:0),w->padding_y+(w->frame?1:0));
    int row=w->direction==F80_ROW, main_axis=row?0:1, cross_axis=row?1:0;
    int main_total=row?inner.w:inner.h, cross_total=row?inner.h:inner.w;
    size_t n=0; for(size_t i=0;i<w->child_count;++i)if(w->children[i]->visible)++n;
    if(!n)return;
    int *sizes=(int *)calloc(n,sizeof(int)); F80Widget **kids=(F80Widget **)calloc(n,sizeof(*kids));
    if(!sizes || !kids) { free(sizes); free(kids); return; }
    size_t j=0; int used=w->gap*((int)n-1), grows=0;
    for(size_t i=0;i<w->child_count;++i)if(w->children[i]->visible) {
        F80Widget *c=kids[j]=w->children[i]; F80Size s=axis_spec(c,main_axis);
        if(s.kind==F80_FIXED)sizes[j]=(int)s.value;
        else if(s.kind==F80_PERCENT)sizes[j]=(int)(main_total*s.value);
        else if(s.kind==F80_FIT)sizes[j]=limit(fit_axis(c,main_axis),s.min,s.max);
        else ++grows;
        used+=sizes[j]; ++j;
    }
    int remaining=main_total-used; if(remaining<0)remaining=0;
    int grow_index=0;
    for(size_t i=0;i<n;++i)if(axis_spec(kids[i],main_axis).kind==F80_GROW) {
        F80Size s=axis_spec(kids[i],main_axis);
        sizes[i]=limit(remaining/grows+(grow_index<remaining%grows),s.min,s.max);
        ++grow_index;
    }
    int over=w->gap*((int)n-1)-main_total;
    for(size_t i=0;i<n;++i)over+=sizes[i];
    while(over>0) {
        int changed=0;
        for(size_t i=0;i<n && over>0;++i)if(axis_spec(kids[i],main_axis).kind!=F80_FIXED && sizes[i]>0) { --sizes[i]; --over; changed=1; }
        if(!changed)break;
    }
    int leftover=main_total-w->gap*((int)n-1);
    for(size_t i=0;i<n;++i)leftover-=sizes[i];
    int start=0, spacing=w->gap;
    if(leftover>0 && grows==0) {
        if(w->justify==F80_JUSTIFY_CENTER)start=leftover/2;
        else if(w->justify==F80_JUSTIFY_END)start=leftover;
        else if(w->justify==F80_SPACE_BETWEEN && n>1)spacing+=leftover/((int)n-1);
    }
    int pos=(row?inner.x:inner.y)+start;
    for(size_t i=0;i<n;++i) {
        F80Widget *c=kids[i]; F80Size s=axis_spec(c,cross_axis); int cs;
        if(s.kind==F80_FIXED)cs=(int)s.value;
        else if(s.kind==F80_PERCENT)cs=(int)(cross_total*s.value);
        else if(s.kind==F80_FIT)cs=fit_axis(c,cross_axis);
        else cs=w->align==F80_STRETCH?cross_total:fit_axis(c,cross_axis);
        cs=limit(cs,0,cross_total);
        int off=w->align==F80_CENTER?(cross_total-cs)/2:w->align==F80_END?cross_total-cs:0;
        f80_layout(c,row?f80_rect(pos,inner.y+off,sizes[i],cs):f80_rect(inner.x+off,pos,cs,sizes[i]));
        pos+=sizes[i]+spacing;
    }
    free(sizes); free(kids);
}

void f80_colors(void) {
    start_color(); use_default_colors();
    init_pair(1,COLOR_CYAN,COLOR_BLUE);   /* panel */
    init_pair(2,COLOR_WHITE,COLOR_BLUE);  /* text */
    init_pair(3,COLOR_BLACK,COLOR_CYAN);  /* selection */
    init_pair(4,COLOR_YELLOW,COLOR_BLUE); /* highlight */
    init_pair(5,COLOR_YELLOW,COLOR_GREEN);/* button */
    init_pair(6,COLOR_BLACK,COLOR_CYAN);  /* status */
    init_pair(7,COLOR_BLACK,COLOR_WHITE); /* dialog */
    init_pair(8,COLOR_GREEN,COLOR_BLUE);
    init_pair(9,COLOR_RED,COLOR_BLUE);
    init_pair(10,COLOR_CYAN,COLOR_BLUE);
    init_pair(11,COLOR_BLACK,COLOR_WHITE);/* menu */
    init_pair(12,COLOR_BLACK,COLOR_BLUE); /* shadow */
    init_pair(13,COLOR_RED,COLOR_WHITE);  /* menu hot key */
    init_pair(14,COLOR_BLACK,COLOR_GREEN);/* active menu */
    init_pair(15,COLOR_WHITE,COLOR_RED);  /* alert */
    init_pair(16,COLOR_WHITE,COLOR_BLACK);/* status key */
    init_pair(17,COLOR_BLACK,COLOR_BLACK);/* dialog shadow */
    init_pair(18,COLOR_WHITE,COLOR_GREEN);/* focused button */
    init_pair(19,COLOR_RED,COLOR_GREEN);  /* selected menu hot key */
    init_pair(20,COLOR_MAGENTA,COLOR_BLUE);/* chart series */
    init_pair(21,COLOR_CYAN,COLOR_GREEN);  /* button hot key */
    init_pair(22,COLOR_WHITE,COLOR_WHITE);/* dialog frame */
    init_pair(23,COLOR_BLACK,COLOR_RED);  /* alert button shadow */
    /* Each role uses a compact semantic ramp: base, muted, accent,
       selection, positive, negative, button, and focused button. */
    const short backgrounds[]={COLOR_CYAN,COLOR_WHITE,COLOR_BLACK,COLOR_MAGENTA};
    const short foregrounds[]={COLOR_BLACK,COLOR_BLACK,COLOR_WHITE,COLOR_WHITE};
    const short button_backgrounds[]={COLOR_BLUE,COLOR_GREEN,COLOR_CYAN,COLOR_YELLOW};
    const short button_foregrounds[]={COLOR_WHITE,COLOR_WHITE,COLOR_BLACK,COLOR_BLACK};
    const short focused_foregrounds[]={COLOR_YELLOW,COLOR_YELLOW,COLOR_BLUE,COLOR_BLUE};
    for(int role=0;role<4;++role) {
        int base=24+role*8; short bg=backgrounds[role],fg=foregrounds[role];
        init_pair((short)base,fg,bg);
        init_pair((short)(base+1),role==0?COLOR_BLUE:COLOR_CYAN,bg);
        init_pair((short)(base+2),COLOR_YELLOW,bg);
        init_pair((short)(base+3),bg,fg);
        init_pair((short)(base+4),COLOR_GREEN,bg);
        init_pair((short)(base+5),COLOR_RED,bg);
        init_pair((short)(base+6),button_foregrounds[role],button_backgrounds[role]);
        init_pair((short)(base+7),focused_foregrounds[role],button_backgrounds[role]);
    }
    init_pair(56,COLOR_BLACK,COLOR_CYAN);    /* cyan button half-shadow */
    init_pair(57,COLOR_BLACK,COLOR_WHITE);   /* gray button half-shadow */
    init_pair(58,COLOR_WHITE,COLOR_BLACK);   /* visible on black console */
    init_pair(59,COLOR_BLACK,COLOR_MAGENTA); /* purple button half-shadow */
    init_pair(60,COLOR_BLUE,COLOR_BLACK);    /* blue inverted bottom shadow */
    init_pair(61,COLOR_CYAN,COLOR_BLACK);    /* cyan inverted bottom shadow */
    init_pair(62,COLOR_MAGENTA,COLOR_BLACK); /* purple inverted bottom shadow */
    init_pair(63,COLOR_RED,COLOR_BLACK);     /* alert inverted bottom shadow */
    init_pair(64,COLOR_YELLOW,COLOR_CYAN);   /* form hot letter */
    init_pair(65,COLOR_BLACK,COLOR_YELLOW);  /* tracker block selection */
#ifdef __EMSCRIPTEN__
    init_pair(66,67,COLOR_BLUE);              /* muted blue neighboring pattern */
#else
    init_pair(66,COLORS>67?67:COLOR_CYAN,COLOR_BLUE);
#endif
}
void f80_put(int y, int x, const char *text, int width, int pair, int bold) {
    if(!text || width<=0 || y<0 || x<0)return;
    int rows,cols; getmaxyx(stdscr,rows,cols);
    if(y>=rows || x>=cols)return;
    width=limit(width,0,cols-x);
    width=limit(width,0,1900);
    char buffer[8192]; size_t bytes=0; int cells=0;
    while(text[bytes] && cells<width) {
        unsigned char c=(unsigned char)text[bytes];
        size_t step=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
        if(bytes+step>=sizeof(buffer)-1901)break;
        int valid=1; for(size_t j=1;j<step;++j)if(!text[bytes+j] || ((unsigned char)text[bytes+j]&0xc0)!=0x80) { valid=0; break; }
        bytes+=valid?step:1; ++cells;
    }
    memcpy(buffer,text,bytes);
#ifdef __EMSCRIPTEN__
    memset(buffer+bytes,' ',(size_t)(width-cells));
    buffer[bytes+width-cells]='\0';
#else
    /* ncurses elides trailing ASCII spaces and exposes the terminal's own
       background. Braille blank occupies a cell without drawing a mark. */
    for(int i=cells;i<width;++i) {
        memcpy(buffer+bytes,"\342\240\200",3);
        bytes+=3;
    }
    buffer[bytes]='\0';
#endif
    int attr=COLOR_PAIR(paint_widget?f80_palette_pair(paint_widget,pair):pair)|(bold?A_BOLD:0);
    wattron(stdscr,attr); mvwprintw(stdscr,y,x,"%s",buffer); wattroff(stdscr,attr);
}
void f80_fill(F80Rect r, int pair) { for(int y=r.y;y<r.y+r.h;++y) f80_put(y,r.x,"",r.w,pair,0); }
void f80_frame(F80Rect r, const char *title, int pair) {
    if(r.w<2 || r.h<2)return;
    char line[2048]; int n=limit(r.w-2,0,600); size_t at=0;
    memcpy(line+at,"╔",3); at+=3;
    for(int i=0;i<n;++i) { memcpy(line+at,"═",3); at+=3; }
    memcpy(line+at,"╗",3); at+=3; line[at]='\0';
    f80_put(r.y,r.x,line,n+2,pair,1);
    memcpy(line,"╚",3); memcpy(line+at-3,"╝",3);
    f80_put(r.y+r.h-1,r.x,line,n+2,pair,1);
    for(int y=r.y+1;y<r.y+r.h-1;++y) { f80_put(y,r.x,"║",1,pair,1); f80_put(y,r.x+r.w-1,"║",1,pair,1); }
    if(title && r.w>6) { int len=(int)strlen(title); if(len>r.w-6)len=r.w-6; char t[256]; if(len>248)len=248; snprintf(t,sizeof(t),"╣ %.*s ╠",len,title); f80_put(r.y,r.x+(r.w-len-4)/2,t,len+4,pair,1); }
}
static void f80_single_frame(F80Rect r,int pair) {
    if(r.w<2 || r.h<2)return;
    f80_put(r.y,r.x,"┌",1,pair,0);
    f80_put(r.y,r.x+r.w-1,"┐",1,pair,0);
    f80_put(r.y+r.h-1,r.x,"└",1,pair,0);
    f80_put(r.y+r.h-1,r.x+r.w-1,"┘",1,pair,0);
    for(int x=r.x+1;x<r.x+r.w-1;++x) {
        f80_put(r.y,x,"─",1,pair,0);
        f80_put(r.y+r.h-1,x,"─",1,pair,0);
    }
    for(int y=r.y+1;y<r.y+r.h-1;++y) {
        f80_put(y,r.x,"│",1,pair,0);
        f80_put(y,r.x+r.w-1,"│",1,pair,0);
    }
}
F80Raster f80_raster(F80Rect rect) {
    F80Raster raster={0}; raster.rect=rect;
    if(rect.w<=0 || rect.h<=0)return raster;
    size_t count=(size_t)rect.w*(size_t)rect.h;
    raster.dots=(unsigned char *)calloc(count,1);
    raster.colors=(unsigned char *)calloc(count,1);
    raster.priority=(unsigned char *)calloc(count,1);
    if(!raster.dots || !raster.colors || !raster.priority)f80_raster_free(&raster);
    return raster;
}
void f80_raster_free(F80Raster *raster) {
    if(!raster)return;
    free(raster->dots); free(raster->colors); free(raster->priority);
    raster->dots=raster->colors=raster->priority=NULL;
}
void f80_raster_pixel(F80Raster *raster,int x,int y,int pair,int priority) {
    static const unsigned char bits[4][2]={{1,8},{2,16},{4,32},{64,128}};
    if(!raster || !raster->dots || x<0 || y<0 || x>=raster->rect.w*2 || y>=raster->rect.h*4)return;
    size_t at=(size_t)(y/4)*(size_t)raster->rect.w+(size_t)(x/2);
    raster->dots[at]|=bits[y%4][x%2];
    if(priority>=raster->priority[at]) { raster->colors[at]=(unsigned char)pair; raster->priority[at]=(unsigned char)priority; }
}
void f80_raster_line(F80Raster *r,int x0,int y0,int x1,int y1,int pair,int priority) {
    int dx=abs(x1-x0),dy=-abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx+dy;
    for(;;) {
        f80_raster_pixel(r,x0,y0,pair,priority);
        if(x0==x1 && y0==y1)break;
        int twice=2*err;
        if(twice>=dy) { err+=dy; x0+=sx; }
        if(twice<=dx) { err+=dx; y0+=sy; }
    }
}
void f80_raster_outline(F80Raster *r,int x,int y,int width,int height,int pair,int priority) {
    if(width<1 || height<1)return;
    f80_raster_line(r,x,y,x+width-1,y,pair,priority);
    f80_raster_line(r,x+width-1,y,x+width-1,y+height-1,pair,priority);
    f80_raster_line(r,x+width-1,y+height-1,x,y+height-1,pair,priority);
    f80_raster_line(r,x,y+height-1,x,y,pair,priority);
}
static int raster_norm_x(const F80Raster *r,float x) {
    int max=r->rect.w*2-1;
    return limit((int)lroundf((x+1.0f)*0.5f*max),0,max);
}
static int raster_norm_y(const F80Raster *r,float y) {
    int max=r->rect.h*4-1;
    return limit((int)lroundf((1.0f-y)*0.5f*max),0,max);
}
void f80_raster_point_norm(F80Raster *r,float x,float y,int pair,int priority) {
    if(!r || !r->dots || !isfinite(x) || !isfinite(y))return;
    f80_raster_pixel(r,raster_norm_x(r,x),raster_norm_y(r,y),pair,priority);
}
void f80_raster_line_norm(F80Raster *r,float x0,float y0,float x1,float y1,int pair,int priority) {
    if(!r || !r->dots || !isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))return;
    f80_raster_line(r,raster_norm_x(r,x0),raster_norm_y(r,y0),
                    raster_norm_x(r,x1),raster_norm_y(r,y1),pair,priority);
}
void f80_raster_rect_norm(F80Raster *r,float left,float bottom,float right,float top,int pair,int priority) {
    f80_raster_line_norm(r,left,bottom,right,bottom,pair,priority);
    f80_raster_line_norm(r,right,bottom,right,top,pair,priority);
    f80_raster_line_norm(r,right,top,left,top,pair,priority);
    f80_raster_line_norm(r,left,top,left,bottom,pair,priority);
}
void f80_raster_ellipse_norm(F80Raster *r,float cx,float cy,float rx,float ry,int pair,int priority) {
    if(!r || !r->dots || !isfinite(cx) || !isfinite(cy) || !isfinite(rx) || !isfinite(ry) || rx<0 || ry<0)return;
    const int segments=64;
    float previous_x=cx+rx,previous_y=cy;
    for(int i=1;i<=segments;++i) {
        double angle=6.283185307179586*(double)i/segments;
        float x=cx+rx*(float)cos(angle),y=cy+ry*(float)sin(angle);
        f80_raster_line_norm(r,previous_x,previous_y,x,y,pair,priority);
        previous_x=x; previous_y=y;
    }
}
void f80_raster_draw(F80Raster *raster) {
    if(!raster || !raster->dots)return;
    F80Rect r=raster->rect;
    for(int y=0;y<r.h;++y)for(int x=0;x<r.w;++x) {
        size_t at=(size_t)y*(size_t)r.w+(size_t)x;
        if(!raster->dots[at])continue;
        unsigned code=0x2800u+raster->dots[at];
        char glyph[4]={(char)(0xe0|(code>>12)),(char)(0x80|((code>>6)&63)),(char)(0x80|(code&63)),0};
        f80_put(r.y+y,r.x+x,glyph,1,raster->colors[at]?raster->colors[at]:2,1);
    }
}
unsigned f80_octant_codepoint(unsigned char dots) {
    /* Braille's middle-row bits differ from Unicode octant row order. */
    unsigned mask=(dots&1u)|((dots&8u)>>2)|((dots&2u)<<1)|((dots&16u)>>1)|
                  ((dots&4u)<<2)|(dots&32u)|(dots&64u)|(dots&128u);
    /* The 230 U+1CD00..U+1CDE5 octants skip patterns already encoded as
       block elements elsewhere in Unicode 16. The skipped masks are sorted. */
    static const struct { unsigned char mask; unsigned codepoint; } special[]={
        {0x00,0x0020},{0x01,0x1CEA8},{0x02,0x1CEAB},{0x03,0x1FB82},
        {0x05,0x2598},{0x0A,0x259D},{0x0F,0x2580},{0x14,0x1FBE6},
        {0x28,0x1FBE7},{0x3F,0x1FB85},{0x40,0x1CEA3},{0x50,0x2596},
        {0x55,0x258C},{0x5A,0x259E},{0x5F,0x259B},{0x80,0x1CEA0},
        {0xA0,0x2597},{0xA5,0x259A},{0xAA,0x2590},{0xAF,0x259C},
        {0xC0,0x2582},{0xF0,0x2584},{0xF5,0x2599},{0xFA,0x259F},
        {0xFC,0x2586},{0xFF,0x2588}
    };
    unsigned skipped=0;
    for(size_t i=0;i<sizeof(special)/sizeof(*special);++i) {
        if(mask==special[i].mask)return special[i].codepoint;
        if(mask<special[i].mask)break;
        ++skipped;
    }
    return 0x1CD00u+mask-skipped;
}
void f80_raster_draw_octants(F80Raster *raster) {
    if(!raster || !raster->dots)return;
    F80Rect r=raster->rect;
    for(int y=0;y<r.h;++y)for(int x=0;x<r.w;++x) {
        size_t at=(size_t)y*(size_t)r.w+(size_t)x;
        unsigned char dots=raster->dots[at];
        if(!dots)continue;
        unsigned code=f80_octant_codepoint(dots);
#ifndef __EMSCRIPTEN__
        if(code>0xffffu) {
            int pair=raster->colors[at]?raster->colors[at]:2;
            f80_put(r.y+y,r.x+x,"",1,pair,0);
            raw_glyph(r.x+x,r.y+y,
                      paint_widget?f80_palette_pair(paint_widget,pair):pair,code);
            continue;
        }
#endif
        char glyph[5]={(char)(0xf0u|(code>>18)),(char)(0x80u|((code>>12)&63u)),
                       (char)(0x80u|((code>>6)&63u)),(char)(0x80u|(code&63u)),0};
        if(code<0x10000u) {
            glyph[0]=(char)(0xe0u|(code>>12));
            glyph[1]=(char)(0x80u|((code>>6)&63u));
            glyph[2]=(char)(0x80u|(code&63u)); glyph[3]=0;
        }
        f80_put(r.y+y,r.x+x,glyph,1,
                raster->colors[at]?raster->colors[at]:2,1);
    }
}
void f80_raster_draw_quadrants(F80Raster *raster) {
    static const char *glyphs[16]={
        " ","▘","▝","▀","▖","▌","▞","▛",
        "▗","▚","▐","▜","▄","▙","▟","█"
    };
    if(!raster || !raster->dots)return;
    F80Rect r=raster->rect;
    for(int y=0;y<r.h;++y)for(int x=0;x<r.w;++x) {
        size_t at=(size_t)y*(size_t)r.w+(size_t)x;
        unsigned char dots=raster->dots[at];
        if(!dots)continue;
        unsigned mask=((dots&3)?1u:0u)|((dots&24)?2u:0u)|
                      ((dots&68)?4u:0u)|((dots&160)?8u:0u);
        f80_put(r.y+y,r.x+x,glyphs[mask],1,
                raster->colors[at]?raster->colors[at]:2,1);
    }
}

static void draw_menu_bar(F80Widget *w) {
    F80Rect r=w->rect; f80_put(r.y,r.x,"",r.w,11,0);
    int x=r.x+1;
    for(int i=0;i<w->menu_count;++i) {
        char title[128]; int hot;
        int len=hot_label(w->menus[i].title,title,sizeof(title),&hot);
        f80_put(r.y,x,"",len+2,w->menu_active&&i==w->menu_index?14:11,0);
        f80_put(r.y,x+1,title,len,w->menu_active&&i==w->menu_index?14:11,0);
        if(hot>=0)f80_put(r.y,x+1+hot,title+hot,1,w->menu_active&&i==w->menu_index?19:13,1);
        x+=len+3;
    }
    if(r.w>28 && w->app && w->app->user) {
        char right[32];
        snprintf(right,sizeof(right),"page %d/2",*(int *)w->app->user+1);
        f80_put(r.y,r.x+r.w-(int)strlen(right)-2,right,(int)strlen(right),11,0);
    }
}
static F80Rect menu_drop_rect(F80Widget *w) {
    F80Rect r=w->rect; int x=r.x+1;
    for(int i=0;i<w->menu_index;++i) {
        char title[128]; int hot; x+=hot_label(w->menus[i].title,title,sizeof(title),&hot)+3;
    }
    const F80Menu *m=&w->menus[w->menu_index]; int width=12;
    for(int i=0;i<m->count;++i) {
        const F80MenuItem *it=&m->items[i];
        char label[128]; int hot;
        int n=hot_label(it->label,label,sizeof(label),&hot)+(int)strlen(it->shortcut?it->shortcut:"")+5;
        if(n>width)width=n;
    }
    int rows,cols; getmaxyx(stdscr,rows,cols);
    if(width>cols)width=cols;
    if(x+width>cols)x=cols-width;
    int h=m->count+2; if(h>rows-r.y-1)h=rows-r.y-1;
    return f80_rect(x,r.y+1,width,h);
}
static void draw_menu_drop(F80Widget *w) {
    if(!w || !w->menu_active)return;
    F80Rect r=menu_drop_rect(w); const F80Menu *m=&w->menus[w->menu_index];
    if(r.w<3 || r.h<3)return;
    f80_fill(f80_rect(r.x+r.w,r.y+1,2,r.h),17);
    f80_fill(f80_rect(r.x+2,r.y+r.h,r.w,1),17);
    f80_fill(r,11); f80_single_frame(r,11);
    for(int i=0;i<m->count && i<r.h-2;++i) {
        const F80MenuItem *it=&m->items[i]; int y=r.y+i+1;
        if(!it->label) {
            f80_put(y,r.x,"├",1,11,0);
            for(int x=r.x+1;x<r.x+r.w-1;++x)f80_put(y,x,"─",1,11,0);
            f80_put(y,r.x+r.w-1,"┤",1,11,0);
            continue;
        }
        char label[128]; int hot;
        int len=hot_label(it->label,label,sizeof(label),&hot);
        int pair=i==w->menu_item?14:11;
        f80_put(y,r.x+1,"",r.w-2,pair,0);
        f80_put(y,r.x+2,label,r.w-4,pair,!it->enabled);
        if(it->shortcut)f80_put(y,r.x+r.w-(int)strlen(it->shortcut)-2,it->shortcut,(int)strlen(it->shortcut),pair,0);
        if(it->enabled && hot>=0 && hot<len)f80_put(y,r.x+2+hot,label+hot,1,i==w->menu_item?19:13,1);
    }
}
static void draw_status(F80Widget *w) {
    F80Rect r=w->rect; f80_put(r.y,r.x,"",r.w,F80_COLOR_STATUS,0); int x=r.x;
    for(int i=0;i<w->status_count && x<r.x+r.w;++i) {
        const F80StatusItem *it=&w->status_items[i]; int key=(int)strlen(it->key), label=(int)strlen(it->label);
        f80_put(r.y,x,it->key,key,16,1); x+=key;
        f80_put(r.y,x,it->label,label,6,0); x+=label+1;
    }
}

static int has_focus(F80Widget *w) { if(!w)return 0; if(w->app && w->app->focus==w)return 1; for(size_t i=0;i<w->child_count;++i)if(has_focus(w->children[i]))return 1; return 0; }
static void draw_bars(F80Widget *w) {
    static const int defaults[]={310,180,95,140,60,120};
    static const int pairs[]={4,8,9,10,20,2};
    static const char *vblocks[]={" ","▁","▂","▃","▄","▅","▆","▇"};
    static const char *hblocks[]={" ","▏","▎","▍","▌","▋","▊","▉"};
    F80Rect r=w->rect; int count=w->item_count<6?w->item_count:6;
    if(count<=0 || r.w<3 || r.h<2)return;
    int max=1;
    for(int i=0;i<count;++i) {
        int value=w->bar_values?w->bar_values[i]:defaults[i];
        if(value>max)max=value;
    }
    if(w->direction==F80_ROW) {
        int label_w=0; for(int i=0;i<count;++i) {
            int n=(int)strlen(w->items[i]); if(n>label_w)label_w=n;
        }
        if(label_w>12)label_w=12;
        int value_w=3, bar_w=r.w-label_w-1-value_w-1; if(bar_w<2)return;
        for(int i=0;i<count && i<r.h;++i) {
            int value=w->bar_values?w->bar_values[i]:defaults[i];
            int units=(int)lround((double)value/max*bar_w*8), full=units/8, part=units%8;
            int len=(int)strlen(w->items[i]);
            f80_put(r.y+i,r.x+label_w-(len<label_w?len:label_w),w->items[i],len<label_w?len:label_w,4,1);
            for(int j=0;j<full && j<bar_w;++j)f80_put(r.y+i,r.x+label_w+1+j,"█",1,pairs[i],1);
            if(part && full<bar_w)f80_put(r.y+i,r.x+label_w+1+full,hblocks[part],1,pairs[i],1);
            char number[16]; snprintf(number,sizeof(number),"%d",value);
            f80_put(r.y+i,r.x+label_w+bar_w+2,number,value_w,2,1);
        }
    } else if(r.h>2) {
        int gap=1, bw=(r.w-gap*(count-1))/count;
        if(bw<1)bw=1; if(bw>10)bw=10;
        int total=bw*count+gap*(count-1);
        int x=r.x+(r.w-total)/2, bar_h=r.h-2, base=r.y+r.h-2;
        for(int i=0;i<count;++i) {
            int value=w->bar_values?w->bar_values[i]:defaults[i];
            int units=(int)lround((double)value/max*bar_h*8), full=units/8, part=units%8;
            for(int y=0;y<full && y<bar_h;++y)for(int k=0;k<bw;++k)
                f80_put(base-y,x+k,"█",1,pairs[i],1);
            if(part && full<bar_h)for(int k=0;k<bw;++k)f80_put(base-full,x+k,vblocks[part],1,pairs[i],1);
            char number[16]; snprintf(number,sizeof(number),"%d",value);
            int vy=base-(full+(part?1:0)); if(vy<r.y)vy=r.y;
            int len=(int)strlen(number);
            if(len>bw) { if(bw>1)number[bw-1]='-'; number[bw]='\0'; len=bw; }
            f80_put(vy,x+(bw-len)/2,number,len,2,1);
            f80_put(base+1,x,w->items[i],bw,4,1);
            x+=bw+gap;
        }
    }
}
static void draw_linechart(F80Widget *w) {
    F80Rect r=w->rect; if(r.w<8 || r.h<3)return;
    F80Raster plot=f80_raster(r); if(!plot.dots)return;
    int px0=12, pw=r.w*2-px0, ph=r.h*4;
    f80_raster_line(&plot,px0-1,0,px0-1,ph-1,10,0);
    f80_raster_line(&plot,px0-1,ph-1,r.w*2-1,ph-1,10,0);
    double phase=(w->app?w->app->animation_tick:0)*0.06;
    for(int series=0;series<3;++series) {
        int pair=series==0?4:series==1?8:9, priority=series==0?2:series==1?1:3;
        int oldx=0,oldy=0;
        for(int i=0;i<80;++i) {
            double value=series==0?sin(phase+i*0.16):
                         series==1?cos(phase+i*0.16)*0.8:
                         w->chart_walk?w->chart_walk[i]:sin(i*0.14)*0.5;
            int x=px0+(int)lround((double)i*(pw-1)/79.0);
            int y=(int)lround((ph-2)-(value+1.5)/3.0*(ph-2));
            y=limit(y,0,ph-1);
            if(i)f80_raster_line(&plot,oldx,oldy,x,y,pair,priority);
            else f80_raster_pixel(&plot,x,y,pair,priority);
            oldx=x; oldy=y;
        }
    }
    if(w->chart_raster_mode==1)f80_raster_draw_octants(&plot);
    else if(w->chart_raster_mode==2)f80_raster_draw_quadrants(&plot);
    else f80_raster_draw(&plot);
    f80_raster_free(&plot);
    f80_put(r.y,r.x," 1.50",5,1,0);
    f80_put(r.y+r.h/2,r.x," 0.00",5,1,0);
    f80_put(r.y+r.h-1,r.x,"-1.50",5,1,0);
    if(w->chart_legend && r.w>12) {
        f80_put(r.y,r.x+r.w-6,"■ sin",5,4,1);
        f80_put(r.y+1,r.x+r.w-6,"■ cos",5,8,1);
        f80_put(r.y+2,r.x+r.w-7,"■ walk",6,9,1);
    }
}
static const char *ascii_offset(const char *text,int offset) {
    if(!text)return "";
    int len=(int)strlen(text);
    return text+(offset<len?offset:len);
}
static int table_has_groups(const F80Widget *w) {
    for(int i=0;i<w->table_col_count;++i)if(w->table_fields[i].group)return 1;
    return 0;
}
static int same_group(const char *a,const char *b) {
    return a==b || (a && b && strcmp(a,b)==0);
}
static int table_field_width(const F80Widget *w,int column) {
    const F80TableField *field=&w->table_fields[column];
    if(field->width>0)return field->width;
    int width=(int)strlen(field->title)+2;
    for(int row=0;row<w->item_count;++row) {
        int n=(int)strlen(f80_table_cell(w,row,column))+2;
        if(n>width)width=n;
    }
    return limit(width,4,40);
}
static void table_visible(F80Widget *w,int width) {
    if(w->table_col<w->table_col_scroll)w->table_col_scroll=w->table_col;
    for(;;) {
        int x=0,n=0;
        for(int i=w->table_col_scroll;i<w->table_col_count;++i) {
            const char *group=w->table_fields[i].group;
            const char *previous=i>0?w->table_fields[i-1].group:NULL;
            int separator=n>0 && (!group || !same_group(group,previous));
            int cw=table_field_width(w,i);
            if(n>0 && x+separator+cw>width)break;
            if(n==0 && cw>width)cw=width;
            x+=separator;
            w->table_visible_index[n]=i; w->table_visible_x[n]=x;
            w->table_visible_width[n]=cw; x+=cw; ++n;
        }
        w->table_visible_count=n;
        int selected=0;
        for(int j=0;j<n;++j)if(w->table_visible_index[j]==w->table_col)selected=1;
        if(selected || w->table_col_scroll>=w->table_col || n==0)break;
        ++w->table_col_scroll;
    }
    int used=0;
    for(int j=0;j<w->table_visible_count;++j)used=w->table_visible_x[j]+w->table_visible_width[j];
    int extra=width-used, flex=0;
    for(int j=0;j<w->table_visible_count;++j)
        if(w->table_fields[w->table_visible_index[j]].width==0)++flex;
    if(!flex && w->table_visible_count)flex=1;
    int shift=0;
    for(int j=0;j<w->table_visible_count;++j) {
        int grow=w->table_fields[w->table_visible_index[j]].width==0 ||
                 (flex==1 && j==w->table_visible_count-1);
        w->table_visible_x[j]+=shift;
        if(grow && extra>0) {
            int add=(extra+flex-1)/flex; w->table_visible_width[j]+=add;
            shift+=add; extra-=add; --flex;
        }
    }
}
static void table_cell_text(int y,int x,int width,const char *text,int align,int pair,int bold) {
    if(width<=0)return;
    f80_put(y,x,"",width,pair,bold);
    int len=(int)strlen(text); if(len>width-2)len=width-2;
    if(len<0)len=0;
    int off=align==1?width-len-1:align==2?(width-len)/2:1;
    if(len)f80_put(y,x+off,text,len,pair,bold);
}
static void draw_gutter(F80Widget *w,int header) {
    int width=f80_content_rect(w).x-w->rect.x;
    if(width<=0)return;
    char number[32];
    for(int y=0;y<w->rect.h;++y) {
        int row=w->scroll+y-header;
        if(y>=header && row>=0 && row<w->item_count)
            snprintf(number,sizeof(number),"%*d ",width>1?width-1:1,row+1);
        else number[0]='\0';
        f80_put(w->rect.y+y,w->rect.x,number,width,F80_COLOR_ACCENT,0);
    }
}
static void input_view(F80Widget *w,int width,int *left,int *text_width,int *right) {
    int length=(int)strlen(w->input);
    w->input_pos=limit(w->input_pos,0,length);
    int scroll=limit(w->input_scroll,0,length);
    if(width<=0) {
        *left=*right=*text_width=0;
        w->input_scroll=scroll;
        return;
    }
    if(width<4) {
        if(w->input_pos<scroll)scroll=w->input_pos;
        if(w->input_pos>=scroll+width)scroll=w->input_pos-width+1;
        *left=*right=0; *text_width=width;
    } else for(;;) {
        int show_left=scroll>0;
        int available=width-show_left;
        int show_right=length>scroll+available;
        int visible=available-show_right;
        if(w->input_pos<scroll) { scroll=w->input_pos; continue; }
        if(w->input_pos>=scroll+visible) {
            scroll=w->input_pos-visible+1; continue;
        }
        *left=show_left; *right=show_right; *text_width=visible;
        break;
    }
    w->input_scroll=scroll;
}
static void draw_table_grid(F80Widget *w) {
    F80Rect r=f80_content_rect(w); int grouped=table_has_groups(w), header=grouped?2:1;
    if(r.h<=header || r.w<3)return;
    int body=r.h-header, scrollbar=w->item_count>body, width=r.w-scrollbar;
    if(w->cursor<w->scroll)w->scroll=w->cursor;
    if(w->cursor>=w->scroll+body)w->scroll=w->cursor-body+1;
    draw_gutter(w,header);
    table_visible(w,width);
    for(int y=0;y<header;++y)f80_put(r.y+y,r.x,"",width,y==0?4:10,1);
    for(int j=0;j<w->table_visible_count;++j) {
        int col=w->table_visible_index[j], x=w->table_visible_x[j], cw=w->table_visible_width[j];
        const F80TableField *field=&w->table_fields[col];
        char heading[128]; snprintf(heading,sizeof(heading),"%s%s",field->title,
             w->table_sort_col==col?(w->table_sort_desc?"▼":"▲"):"");
        table_cell_text(r.y+header-1,r.x+x,cw,heading,field->align,field->group?10:4,1);
        if(grouped && field->group && (j==0 || !same_group(field->group,w->table_fields[w->table_visible_index[j-1]].group))) {
            int span=cw;
            for(int k=j+1;k<w->table_visible_count;++k) {
                if(!same_group(w->table_fields[w->table_visible_index[k]].group,field->group))break;
                span=w->table_visible_x[k]+w->table_visible_width[k]-x;
            }
            for(int k=0;k<span;++k)f80_put(r.y,r.x+x+k,"─",1,1,0);
            char group_title[128]; snprintf(group_title,sizeof(group_title)," %s ",field->group);
            int gn=(int)strlen(group_title); if(gn>span)gn=span;
            if(gn>0)f80_put(r.y,r.x+x+(span-gn)/2,group_title,gn,4,1);
        }
    }
    if(w->table_col_scroll>0)f80_put(r.y+header-1,r.x,"◄",1,8,1);
    if(w->table_visible_count && w->table_visible_index[w->table_visible_count-1]<w->table_col_count-1)
        f80_put(r.y+header-1,r.x+width-1,"►",1,8,1);
    if(w->cursor<w->scroll)w->scroll=w->cursor;
    if(w->cursor>=w->scroll+body)w->scroll=w->cursor-body+1;
    for(int i=0;i<body && w->scroll+i<w->item_count;++i) {
        int row=w->scroll+i, y=r.y+header+i;
        for(int j=0;j<w->table_visible_count;++j) {
            int col=w->table_visible_index[j], x=w->table_visible_x[j], cw=w->table_visible_width[j];
            const char *value=f80_table_cell(w,row,col);
            int pair=w->table_fields[col].pair?w->table_fields[col].pair:2;
            if(strcmp(w->table_fields[col].title,"Side")==0)pair=strcmp(value,"BUY")==0?8:9;
            else if(pair==8 || pair==9)pair=value[0]=='-'?9:8;
            if(row==w->cursor && col==w->table_col)pair=3;
            table_cell_text(y,r.x+x,cw,value,w->table_fields[col].align,pair,
                            row==w->cursor && w->app && w->app->focus==w);
        }
    }
    for(int j=0;j<w->table_visible_count;++j) {
        int col=w->table_visible_index[j], x=w->table_visible_x[j];
        const char *group=w->table_fields[col].group;
        const char *previous=col>0?w->table_fields[col-1].group:NULL;
        if(x<=0 || (group && same_group(group,previous)))continue;
        int lines=header+(w->item_count<body?w->item_count:body);
        for(int y=0;y<lines;++y)f80_put(r.y+y,r.x+x-1,"│",1,1,0);
    }
    if(scrollbar && r.h>=3) {
        int track=r.h-2, thumb=track*body/w->item_count;
        if(thumb<1)thumb=1;
        int at=(track-thumb)*w->scroll/(w->item_count-body);
        f80_put(r.y,r.x+r.w-1,"▲",1,10,1);
        for(int i=0;i<track;++i)f80_put(r.y+1+i,r.x+r.w-1,i>=at&&i<at+thumb?"█":"▒",1,10,0);
        f80_put(r.y+r.h-1,r.x+r.w-1,"▼",1,10,1);
    }
}
void f80_draw(F80Widget *w) {
    if(!w || !w->visible || w->rect.w<=0 || w->rect.h<=0)return;
    const F80Widget *previous_paint=paint_widget;
    paint_widget=w;
    F80Rect r=w->rect;
    switch(w->kind) {
    case F80_SPACER: break;
    case F80_BOX:
        if(w->fill || w->frame)f80_fill(r,w->style==15?15:w->style==7?7:1);
        if(w->frame) {
            int active=has_focus(w);
            f80_frame(r,w->title,w->style==15?15:w->style==7?7:active?2:1);
            if(w->title && w->style!=15 && w->style!=7 && r.w>6) {
                int len=(int)strlen(w->title); if(len>r.w-6)len=r.w-6;
                f80_put(r.y,r.x+(r.w-len-4)/2+2,w->title,len,active?4:2,1);
            }
        }
        break;
    case F80_LABEL: f80_put(r.y,r.x,w->text?w->text:"",r.w,w->style?w->style:4,0); break;
    case F80_DIALOG:
        f80_fill(r,w->style);
        f80_frame(r,w->title,w->style==15?15:22);
        for(int i=0;i<w->dialog_count && i<r.h-4;++i) {
            const char *line=w->dialog_lines[i]?w->dialog_lines[i]:"";
            int len=(int)strlen(line), x=r.x+2;
            if(w->dialog_align==0 && len<r.w-4)x+=(r.w-4-len)/2;
            f80_put(r.y+1+i,x,line,r.w-4-(x-r.x-2),w->dialog_pairs&&w->dialog_pairs[i]?w->dialog_pairs[i]:w->style,0);
        }
        break;
    case F80_BUTTON:
        {
        int pressed=w->app && w->app->tick<w->pressed_until;
        int bx=r.x+(pressed?1:0);
        char label[256]; int hot;
        int len=hot_label(w->text,label,sizeof(label),&hot), face=r.w-1;
        f80_put(r.y,bx,"",face,w->app&&w->app->focus==w?18:5,1);
        if(len) {
            if(len>face)len=face;
            f80_put(r.y,bx+(face-len)/2,label,len,w->app&&w->app->focus==w?18:5,1);
            if(hot>=0 && hot<len)f80_put(r.y,bx+(face-len)/2+hot,label+hot,1,21,1);
        }
        if(!pressed) {
            int side_shadow=F80_COLOR_SHADOW;
            int bottom_shadow=F80_COLOR_BUTTON_SHADOW_BOTTOM;
            for(F80Widget *parent=w->parent;parent;parent=parent->parent)
                if(parent->kind==F80_DIALOG && parent->style==15) {
                    side_shadow=F80_COLOR_ALERT_SHADOW;
                    bottom_shadow=F80_COLOR_ALERT_SHADOW_BOTTOM;
                    break;
                }
            if(r.w>1)f80_put(r.y,r.x+r.w-1,"▄",1,side_shadow,0);
            if(r.h>1)for(int i=1;i<r.w;++i)
                f80_put(r.y+1,r.x+i,"▄",1,bottom_shadow,0);
        }
        }
        break;
    case F80_LISTBOX:
    case F80_TABLE: {
        if(w->kind==F80_TABLE && w->table_fields) { draw_table_grid(w); break; }
        int header=w->kind==F80_TABLE?(w->table_group_header?2:1):0;
        r=f80_content_rect(w);
        int body=r.h-header; if(body<1)body=1;
        int sb=w->item_count>body, width=r.w-(sb?1:0);
        if(w->kind==F80_TABLE && w->table_col_count>0) {
            int start=w->table_starts[w->table_col];
            int end=w->table_col+1<w->table_col_count?w->table_starts[w->table_col+1]:start+12;
            if(start<w->table_hscroll)w->table_hscroll=start;
            if(end>w->table_hscroll+width)w->table_hscroll=end-width;
            if(w->table_hscroll<0)w->table_hscroll=0;
        }
        if(w->kind==F80_TABLE) {
            if(w->table_group_header) {
                f80_put(r.y,r.x,ascii_offset(w->table_group_header,w->table_hscroll),width,4,1);
                f80_put(r.y+1,r.x,ascii_offset(w->title,w->table_hscroll),width,10,1);
            } else f80_put(r.y,r.x,ascii_offset(w->title,w->table_hscroll),width,4,1);
            if(w->table_hscroll>0)f80_put(r.y+header-1,r.x,"◄",1,8,1);
            if(w->table_col_count && w->table_starts[w->table_col_count-1]+12>w->table_hscroll+width)
                f80_put(r.y+header-1,r.x+width-1,"►",1,8,1);
        }
        if(!w->scroll_detached) {
            if(w->cursor<w->scroll)w->scroll=w->cursor;
            if(w->cursor>=w->scroll+body)w->scroll=w->cursor-body+1;
        }
        w->scroll=limit(w->scroll,0,w->item_count>body?w->item_count-body:0);
        draw_gutter(w,header);
        for(int i=0;i<body && w->scroll+i<w->item_count;++i) {
            int selected=w->scroll+i==w->cursor;
            int y=r.y+i+header;
            const char *line=w->items[w->scroll+i];
            f80_put(y,r.x,w->kind==F80_TABLE?ascii_offset(line,w->table_hscroll):line,width,w->kind==F80_LISTBOX&&selected?3:2,
                    selected && w->app && w->app->focus==w);
            if(w->kind==F80_TABLE && selected) {
                int start=w->table_col_count?w->table_starts[w->table_col]:0;
                int end=w->table_col+1<w->table_col_count?w->table_starts[w->table_col+1]:start+12;
                int x0=start-w->table_hscroll, cw=end-start;
                if(x0<0) { cw+=x0; start-=x0; x0=0; }
                if(cw>width-x0)cw=width-x0;
                if(cw>0 && x0<width) {
                    char cell[256]; int n=(int)strlen(line), copy=n>start?n-start:0;
                    if(copy>cw)copy=cw; if(copy>255)copy=255;
                    if(copy>0)memcpy(cell,line+start,(size_t)copy);
                    cell[copy]='\0'; f80_put(y,r.x+x0,cell,cw,3,w->app && w->app->focus==w);
                }
            }
        }
        if(w->item_count>body && r.w>1 && r.h>=3) {
            int track=r.h-2, thumb=track*body/w->item_count;
            if(thumb<1)thumb=1;
            int at=(track-thumb)*w->scroll/(w->item_count-body);
            f80_put(r.y,r.x+r.w-1,"▲",1,10,1);
            for(int i=0;i<track;++i)f80_put(r.y+1+i,r.x+r.w-1,i>=at&&i<at+thumb?"█":"▒",1,10,0);
            f80_put(r.y+r.h-1,r.x+r.w-1,"▼",1,10,1);
        }
        break;
    }
    case F80_PROGRESS: {
        int filled=limit((int)(w->value*r.w+0.5f),0,r.w);
        int block=limit(r.w/5,3,r.w), pos=(w->app?w->app->tick:0)%(r.w+block)-block;
        char percent[16]; snprintf(percent,sizeof(percent),"%d%%",limit((int)(w->value*100+0.5f),0,100));
        int text_x=(r.w-(int)strlen(percent))/2;
        for(int i=0;i<r.w;++i) {
            if(w->indeterminate)f80_put(r.y,r.x+i,i>=pos&&i<pos+block?"█":"░",1,1,1);
            else {
                int in_text=i>=text_x && i<text_x+(int)strlen(percent);
                char ch[2]={in_text?percent[i-text_x]:'\0','\0'};
                f80_put(r.y,r.x+i,in_text?ch:i<filled?" ":"░",1,i<filled?3:10,in_text);
            }
        }
        break;
    }
    case F80_BARS: draw_bars(w); break;
    case F80_LINECHART: draw_linechart(w); break;
    case F80_CANVAS: if(w->paint)w->paint(w,w->user); break;
    case F80_RADIO_GROUP:
    case F80_CHECK_GROUP:
        f80_fill(r,F80_COLOR_FORM_FIELD);
        if(w->cursor<w->scroll)w->scroll=w->cursor;
        if(w->cursor>=w->scroll+r.h)w->scroll=w->cursor-r.h+1;
        for(int row=0;row<r.h && w->scroll+row<w->item_count;++row) {
            int i=w->scroll+row;
            int focused=w->app && w->app->focus==w && i==w->cursor;
            int pair=focused?F80_COLOR_FORM_FOCUS:F80_COLOR_FORM_FIELD;
            char label[256],marker[4]; int hot;
            int len=hot_label(w->items && w->items[i]?w->items[i]:"",label,sizeof(label),&hot);
            snprintf(marker,sizeof(marker),"%s",w->kind==F80_RADIO_GROUP?
                     (i==w->selected?"(*)":"( )"):(f80_checked(w,i)?"[x]":"[ ]"));
            f80_put(r.y+row,r.x,focused?">":" ",1,pair,focused);
            if(r.w>1)f80_put(r.y+row,r.x+1,marker,r.w-1<3?r.w-1:3,pair,focused);
            if(r.w>5) {
                int width=r.w-5;
                f80_put(r.y+row,r.x+5,label,width,pair,focused);
                if(hot>=0 && hot<len && hot<width)
                    f80_put(r.y+row,r.x+5+hot,label+hot,1,F80_COLOR_FORM_HOT,1);
            }
        }
        break;
    case F80_INPUT: {
#ifdef __EMSCRIPTEN__
        f80_web_add_input(r.x,r.y,r.w,r.h);
#endif
        int left,text_width,right;
        input_view(w,r.w,&left,&text_width,&right);
        f80_fill(r,w->style?w->style:F80_COLOR_TEXT);
        f80_put(r.y,r.x+left,w->input+w->input_scroll,text_width,
                w->style?w->style:F80_COLOR_TEXT,1);
        if(left)f80_put(r.y,r.x,"◀",1,F80_COLOR_INPUT_OVERFLOW,1);
        if(right)f80_put(r.y,r.x+r.w-1,"▶",1,F80_COLOR_INPUT_OVERFLOW,1);
        if(w->app && w->app->focus==w) {
            int pos=w->input_pos-w->input_scroll+left;
            if(pos>=left && pos<r.w-right) {
                char ch[2]={w->input[w->input_pos]?w->input[w->input_pos]:' ',0};
                f80_put(r.y,r.x+pos,ch,1,7,0);
            }
        }
        break;
    }
    case F80_MENUBAR: draw_menu_bar(w); break;
    case F80_STATUSBAR: draw_status(w); break;
    }
    for(size_t i=0;i<w->child_count;++i)f80_draw(w->children[i]);
    paint_widget=previous_paint;
}
void f80_draw_screen(F80App *app) {
#ifdef __EMSCRIPTEN__
    f80_web_clear_inputs();
#endif
#ifndef __EMSCRIPTEN__
    raw_count=0;
#endif
    erase(); f80_draw(app->root);
    for(int i=0;i<app->modal_depth;++i) {
#ifdef __EMSCRIPTEN__
        f80_web_clear_inputs();
#endif
        F80Rect r=app->modal_stack[i]->rect;
        paint_widget=app->modal_stack[i];
        f80_fill(f80_rect(r.x+2,r.y+r.h,r.w,1),17);
        f80_fill(f80_rect(r.x+r.w,r.y+1,2,r.h),17);
        f80_draw(app->modal_stack[i]);
    }
    if(app->menu && !app->modal) { paint_widget=app->menu; draw_menu_drop(app->menu); }
    paint_widget=NULL;
#ifndef __EMSCRIPTEN__
    /* ncurses does not know about the supplementary-plane glyphs emitted
       below. Redraw its cell model to clear pixels that moved or disappeared. */
    if(raw_count || previous_raw_count)redrawwin(stdscr);
#endif
    refresh(); ++app->tick;
#ifndef __EMSCRIPTEN__
    flush_raw_glyphs(app);
    previous_raw_count=raw_count;
#endif
}

static void collect(F80Widget *w, F80Widget **out, int *count, int cap) {
    if(!w || !w->visible || !w->enabled)return;
    if(w->focusable && *count<cap)out[(*count)++]=w;
    for(size_t i=0;i<w->child_count;++i)collect(w->children[i],out,count,cap);
}
void f80_focus_next(F80App *app, int step) {
    F80Widget *items[256]; int n=0; collect(app->modal?app->modal:app->root,items,&n,256); if(!n)return;
    int i=-1; for(int j=0;j<n;++j)if(items[j]==app->focus)i=j;
    app->focus=items[(i+step+n)%n];
}
static int form_activate(F80Widget *w,int index) {
    if(index<0 || index>=w->item_count)return 0;
    w->cursor=index;
    if(w->kind==F80_RADIO_GROUP) {
        if(w->selected==index)return 1;
        w->selected=index;
    } else f80_set_checked(w,index,!f80_checked(w,index));
    if(w->action)w->action(w,w->user);
    return 1;
}
static int widget_key(F80Widget *w, int key) {
    if(!w)return 0;
    if(w->kind==F80_BUTTON && (key==10 || key==13 || key==' ')) { if(w->app)w->pressed_until=w->app->tick+2; if(w->action)w->action(w,w->user); return 1; }
    if(w->kind==F80_RADIO_GROUP || w->kind==F80_CHECK_GROUP) {
        if(!w->item_count)return 0;
        if(key==F80_KEY_UP || key==F80_KEY_DOWN || key==F80_KEY_HOME || key==F80_KEY_END) {
            int next=key==F80_KEY_HOME?0:key==F80_KEY_END?w->item_count-1:
                     limit(w->cursor+(key==F80_KEY_UP?-1:1),0,w->item_count-1);
            w->cursor=next;
            if(w->kind==F80_RADIO_GROUP)return form_activate(w,next);
            return 1;
        }
        if(key==' ' || key==10 || key==13)return form_activate(w,w->cursor);
        if(key>=32 && key<127)for(int i=0;i<w->item_count;++i) {
            char label[256]; int hot;
            hot_label(w->items && w->items[i]?w->items[i]:"",label,sizeof(label),&hot);
            if(hot>=0 && tolower((unsigned char)label[hot])==tolower((unsigned char)key))
                return form_activate(w,i);
        }
        return 0;
    }
    if(w->kind==F80_LISTBOX || w->kind==F80_TABLE) {
        int header=w->kind==F80_TABLE?(w->table_fields?table_has_groups(w)+1:(w->table_group_header?2:1)):0;
        int old=w->cursor, page=w->rect.h>header?w->rect.h-header:1;
        if(w->kind==F80_TABLE && w->table_col_count>0 && (key==F80_KEY_LEFT || key==F80_KEY_RIGHT)) {
            w->table_col=limit(w->table_col+(key==F80_KEY_LEFT?-1:1),0,w->table_col_count-1);
            if(w->action)w->action(w,w->user);
            return 1;
        }
        if(w->kind==F80_TABLE && w->table_fields && (key=='s' || key=='S')) {
            f80_table_sort(w,w->table_col);
            if(w->action)w->action(w,w->user);
            return 1;
        }
        if(key==F80_KEY_UP)w->cursor--; else if(key==F80_KEY_DOWN)w->cursor++;
        else if(key==F80_KEY_PPAGE)w->cursor-=page; else if(key==F80_KEY_NPAGE)w->cursor+=page;
        else if(key==F80_KEY_HOME) { if(w->kind==F80_TABLE && w->table_fields)w->table_col=0; else w->cursor=0; }
        else if(key==F80_KEY_END) { if(w->kind==F80_TABLE && w->table_fields)w->table_col=w->table_col_count-1; else w->cursor=w->item_count-1; }
        else if(key==10 || key==13) { if(w->activate)w->activate(w,w->user); else if(w->action)w->action(w,w->user); return 1; }
        else return 0;
        w->cursor=limit(w->cursor,0,w->item_count>0?w->item_count-1:0);
        if(old!=w->cursor && w->action)w->action(w,w->user);
        return 1;
    }
    if(w->kind==F80_INPUT) {
        size_t n=strlen(w->input);
        if(key==F80_KEY_BACKSPACE || key==127 || key==8) { if(w->input_pos>0) { memmove(w->input+w->input_pos-1,w->input+w->input_pos,n-(size_t)w->input_pos+1); --w->input_pos; } }
        else if(key==F80_KEY_DELETE) { if((size_t)w->input_pos<n)memmove(w->input+w->input_pos,w->input+w->input_pos+1,n-(size_t)w->input_pos); }
        else if(key==F80_KEY_LEFT)w->input_pos=limit(w->input_pos-1,0,(int)n);
        else if(key==F80_KEY_RIGHT)w->input_pos=limit(w->input_pos+1,0,(int)n);
        else if(key==F80_KEY_HOME)w->input_pos=0;
        else if(key==F80_KEY_END)w->input_pos=(int)n;
        else if(key>=32 && key<127 && n<sizeof(w->input)-1) { memmove(w->input+w->input_pos+1,w->input+w->input_pos,n-(size_t)w->input_pos+1); w->input[w->input_pos++]=(char)key; }
        else return 0;
        return 1;
    }
    return 0;
}
static int menu_key(F80Widget *w,int key) {
    if(!w || !w->menu_active)return 0;
    const F80Menu *m=&w->menus[w->menu_index];
    if(m->count<=0) { f80_menu_close(w); return 1; }
    if(key==27) { f80_menu_close(w); return 1; }
    if(key==F80_KEY_LEFT || key==F80_KEY_RIGHT) { f80_menu_open(w,w->menu_index+(key==F80_KEY_LEFT?-1:1)); return 1; }
    if(key==F80_KEY_UP || key==F80_KEY_DOWN) {
        int step=key==F80_KEY_UP?-1:1;
        for(int i=0;i<m->count;++i) {
            int at=(w->menu_item+step*(i+1)+m->count*(i+1))%m->count;
            if(menu_enabled(&m->items[at])) { w->menu_item=at; break; }
        }
        return 1;
    }
    if(key==10 || key==13) {
        const F80MenuItem *it=&m->items[w->menu_item]; f80_menu_close(w);
        if(menu_enabled(it) && it->action)it->action(w,NULL);
        return 1;
    }
    if(key>=32 && key<127) {
        for(int i=0;i<m->count;++i) {
            const F80MenuItem *it=&m->items[i];
            char label[128]; int hot;
            hot_label(it->label,label,sizeof(label),&hot);
            if(menu_enabled(it) && hot>=0 && (label[hot]|32)==(key|32)) {
                f80_menu_close(w); if(it->action)it->action(w,NULL); return 1;
            }
        }
        for(int i=0;i<w->menu_count;++i) {
            char label[128]; int hot;
            hot_label(w->menus[i].title,label,sizeof(label),&hot);
            if(hot>=0 && (label[hot]|32)==(key|32)) { f80_menu_open(w,i); return 1; }
        }
    }
    return 1;
}
int f80_key(F80App *app, int key) {
    if(app->menu && app->menu->menu_active && !app->modal)return menu_key(app->menu,key);
    if(app->modal && app->modal->kind==F80_DIALOG) {
        F80Widget *d=app->modal;
        if(key==F80_KEY_LEFT || key==F80_KEY_UP || key==F80_KEY_RIGHT || key==F80_KEY_DOWN) {
            if(app->focus && app->focus->kind==F80_INPUT && (key==F80_KEY_LEFT || key==F80_KEY_RIGHT))return widget_key(app->focus,key);
            f80_focus_next(app,(key==F80_KEY_LEFT || key==F80_KEY_UP)?-1:1); return 1;
        }
        if((key==10 || key==13) && app->focus && app->focus->kind==F80_INPUT) {
            int button=0;
            for(size_t i=0;i<d->child_count;++i)if(d->children[i]->kind==F80_BUTTON) {
                if(button++==d->dialog_default)return widget_key(d->children[i],key);
            }
        }
        if(key>=32 && key<127 && (!app->focus || app->focus->kind!=F80_INPUT)) {
            for(size_t i=0;i<d->child_count;++i)if(d->children[i]->kind==F80_BUTTON) {
                char label[256]; int hot;
                hot_label(d->children[i]->text,label,sizeof(label),&hot);
                if(hot>=0 && (label[hot]|32)==(key|32))return widget_key(d->children[i],13);
            }
        }
    }
    if(key==9) { f80_focus_next(app,1); return 1; }
    if(key==F80_KEY_BTAB) { f80_focus_next(app,-1); return 1; }
    return widget_key(app->focus,key);
}
static F80Widget *hit(F80Widget *w,int x,int y) {
    if(!w || !w->visible || !f80_contains(w->rect,x,y))return NULL;
    for(size_t i=w->child_count;i>0;--i) { F80Widget *c=hit(w->children[i-1],x,y); if(c)return c; }
    return w;
}
int f80_mouse(F80App *app,int x,int y,int wheel) {
    F80Widget *menu=app->menu;
    if(menu && !app->modal && !wheel) {
        if(y==menu->rect.y) {
            int at=menu->rect.x+1;
            for(int i=0;i<menu->menu_count;++i) {
                char title[128]; int hot;
                int width=hot_label(menu->menus[i].title,title,sizeof(title),&hot)+2;
                if(x>=at && x<at+width) {
                    if(menu->menu_active && menu->menu_index==i)f80_menu_close(menu);
                    else f80_menu_open(menu,i);
                    return 1;
                }
                at+=width+1;
            }
            f80_menu_close(menu); return 1;
        }
        if(menu->menu_active) {
            F80Rect drop=menu_drop_rect(menu);
            if(f80_contains(drop,x,y)) {
                int index=y-drop.y-1;
                if(index>=0 && index<menu->menus[menu->menu_index].count) {
                    const F80MenuItem *it=&menu->menus[menu->menu_index].items[index];
                    f80_menu_close(menu); if(menu_enabled(it) && it->action)it->action(menu,NULL);
                }
            } else f80_menu_close(menu);
            return 1;
        }
    }
    F80Widget *w=hit(app->modal?app->modal:app->root,x,y); if(!w)return 0;
    if(w->kind==F80_STATUSBAR && !wheel) {
        int at=w->rect.x;
        for(int i=0;i<w->status_count;++i) {
            const F80StatusItem *it=&w->status_items[i]; int width=(int)strlen(it->key)+(int)strlen(it->label)+1;
            if(x>=at && x<at+width) { if(it->action)it->action(w,NULL); return 1; }
            at+=width;
        }
    }
    if(w->focusable)app->focus=w;
    if(w->kind==F80_RADIO_GROUP || w->kind==F80_CHECK_GROUP) {
        if(wheel) {
            int next=limit(w->cursor+wheel,0,w->item_count>0?w->item_count-1:0);
            if(w->kind==F80_RADIO_GROUP)return form_activate(w,next);
            w->cursor=next; return 1;
        }
        form_activate(w,w->scroll+y-w->rect.y);
        return 1;
    }
    if(w->kind==F80_LISTBOX || w->kind==F80_TABLE) {
        if(wheel)w->cursor=limit(w->cursor+wheel*3,0,w->item_count>0?w->item_count-1:0);
        else {
            int old=w->cursor;
            int header=w->kind==F80_TABLE?(w->table_fields?table_has_groups(w)+1:(w->table_group_header?2:1)):0;
            int body=w->rect.h-header;
            if(w->item_count>body && x==w->rect.x+w->rect.w-1) {
                int delta=y==w->rect.y?-1:y==w->rect.y+w->rect.h-1?1:
                    y<w->rect.y+w->rect.h/2?-body:body;
                w->cursor=limit(w->cursor+delta,0,w->item_count-1);
                if(w->action)w->action(w,w->user);
                return 1;
            }
            if(w->kind==F80_TABLE && w->table_fields) {
                int offset=x-f80_content_rect(w).x;
                for(int i=0;i<w->table_visible_count;++i)
                    if(offset>=w->table_visible_x[i] && offset<w->table_visible_x[i]+w->table_visible_width[i]) {
                        w->table_col=w->table_visible_index[i]; break;
                    }
                if(y<w->rect.y+header) {
                    if(y==w->rect.y && table_has_groups(w)) {
                        const char *group=w->table_fields[w->table_col].group;
                        if(group)while(w->table_col>0 && same_group(w->table_fields[w->table_col-1].group,group))--w->table_col;
                    }
                    if(w->sort)w->sort(w,w->user);
                    else f80_table_sort(w,w->table_col);
                    if(w->action)w->action(w,w->user);
                    return 1;
                }
            }
            if(w->kind==F80_TABLE && w->table_col_count>0) {
                if(!w->table_fields) {
                    int offset=x-f80_content_rect(w).x+w->table_hscroll;
                    for(int i=0;i<w->table_col_count;++i)if(offset>=w->table_starts[i])w->table_col=i;
                }
            }
            if(y<w->rect.y+header)return 1;
            w->cursor=limit(w->scroll+y-w->rect.y-header,0,w->item_count>0?w->item_count-1:0);
            if(old==w->cursor && w->activate) { w->activate(w,w->user); return 1; }
        }
        if(w->action)w->action(w,w->user);
        return 1;
    }
    if(w->kind==F80_BUTTON && !wheel) { if(w->app)w->pressed_until=w->app->tick+2; if(w->action)w->action(w,w->user); return 1; }
    if(w->kind==F80_INPUT && !wheel) {
        int left,text_width,right;
        input_view(w,w->rect.w,&left,&text_width,&right);
        if(left && x==w->rect.x)
            w->input_pos=limit(w->input_pos-(w->rect.w>3?w->rect.w-3:1),0,(int)strlen(w->input));
        else if(right && x==w->rect.x+w->rect.w-1)
            w->input_pos=limit(w->input_pos+(w->rect.w>3?w->rect.w-3:1),0,(int)strlen(w->input));
        else w->input_pos=limit(w->input_scroll+x-w->rect.x-left,0,(int)strlen(w->input));
        return 1;
    }
    return w->focusable;
}
