#include "f80_components.h"
#include <stdio.h>
#include <string.h>
enum { GRID_GUTTER_WIDTH=4 };

static int bound(int value,int low,int high) {
    return value<low?low:value>high?high:value;
}
static int grid_gutter_width(const F80Grid *g) {
    return g->gutter_width>0?g->gutter_width:GRID_GUTTER_WIDTH;
}
static int grid_body_width(const F80Grid *g) {
    int width=g->rect.w-grid_gutter_width(g)-g->show_v;
    return width>0?width:1;
}
static int grid_body_height(const F80Grid *g) {
    int height=g->rect.h-1-g->show_h;
    return height>0?height:1;
}
static int grid_field_start(const F80Grid *g,int field) {
    int x=0;
    for(int i=0;i<field;++i)x+=g->width[i]+1;
    return x;
}
int f80_grid_content_width(const F80Grid *g) {
    int width=0;
    for(int i=0;i<g->fields;++i)width+=g->width[i]+1;
    return width>0?width-1:0;
}
static void grid_scrollbars(F80Grid *g) {
    g->show_h=g->show_v=0;
    /* Each scrollbar consumes a cell on the other axis. Recheck until stable. */
    for(int i=0;i<2;++i) {
        if(f80_grid_content_width(g)>grid_body_width(g))g->show_h=1;
        if(g->rows>grid_body_height(g))g->show_v=1;
    }
}
static int grid_max_x(const F80Grid *g) {
    int value=f80_grid_content_width(g)-grid_body_width(g);
    return value>0?value:0;
}
static int grid_max_y(const F80Grid *g) {
    int value=g->rows-grid_body_height(g);
    return value>0?value:0;
}
void f80_grid_init(F80Grid *g,int rows,int fields,const int *width,
                   const char *const *heading) {
    memset(g,0,sizeof(*g));
    g->rows=rows>0?rows:0;
    g->fields=bound(fields,0,F80_GRID_MAX_FIELDS);
    g->corner="TCK";
    g->focused=1;
    for(int i=0;i<g->fields;++i) {
        g->width[i]=width[i]>0?width[i]:1;
        g->heading[i]=heading?heading[i]:"";
    }
}
void f80_grid_corner(F80Grid *g,const char *label) {
    if(g)g->corner=label?label:"";
}
void f80_grid_gutter(F80Grid *g,int width) {
    if(g)g->gutter_width=width>0?width:GRID_GUTTER_WIDTH;
}
void f80_grid_row_text(F80Grid *g,F80GridText row_text) {
    if(g)g->row_text=row_text;
}
void f80_grid_read_only(F80Grid *g,int field,int read_only) {
    if(g && field>=0 && field<g->fields)
        g->read_only[field]=!!read_only;
}
void f80_grid_focus(F80Grid *g,int focused) {
    if(g)g->focused=!!focused;
}
void f80_grid_enable_edit(F80Grid *g,int enabled) {
    if(g) { g->edit_enabled=!!enabled;if(!enabled)g->editing=0; }
}
void f80_grid_set_editing(F80Grid *g,int editing) {
    if(g)g->editing=g->edit_enabled && !!editing;
}
void f80_grid_blink(F80Grid *g,int visible) {
    if(g)g->blink_on=!!visible;
}
int f80_grid_is_editing(const F80Grid *g) { return g && g->editing; }
void f80_grid_layout(F80Grid *g,F80Rect rect) {
    g->rect=rect;
    grid_scrollbars(g);
    g->scroll_x=bound(g->scroll_x,0,grid_max_x(g));
    g->scroll_y=bound(g->scroll_y,0,grid_max_y(g));
}
void f80_grid_resize(F80Grid *g,int rows) {
    g->rows=rows>0?rows:0;
    grid_scrollbars(g);
    g->row=bound(g->row,0,g->rows?g->rows-1:0);
    g->scroll_y=bound(g->scroll_y,0,grid_max_y(g));
}
void f80_grid_scroll(F80Grid *g,int columns,int rows) {
    g->scroll_x=bound(g->scroll_x+columns,0,grid_max_x(g));
    g->scroll_y=bound(g->scroll_y+rows,0,grid_max_y(g));
}
static void grid_reveal(F80Grid *g) {
    int left=grid_field_start(g,g->field),right=left+g->width[g->field];
    int visible=grid_body_width(g);
    if(left<g->scroll_x)g->scroll_x=left;
    if(right>g->scroll_x+visible)g->scroll_x=right-visible;
    if(g->row<g->scroll_y)g->scroll_y=g->row;
    if(g->row>=g->scroll_y+grid_body_height(g))
        g->scroll_y=g->row-grid_body_height(g)+1;
    g->scroll_x=bound(g->scroll_x,0,grid_max_x(g));
    g->scroll_y=bound(g->scroll_y,0,grid_max_y(g));
}
int f80_grid_key(F80Grid *g,int key) {
    if(!g->rows || !g->fields)return 0;
    if(g->edit_enabled && (key==10 || key==13)) {
        g->editing=!g->editing;return F80_GRID_KEY_HANDLED;
    }
    if(g->editing && (key==F80_KEY_UP || key==F80_KEY_DOWN ||
                      key==F80_KEY_LEFT || key==F80_KEY_RIGHT))
        return F80_GRID_KEY_EDIT_VALUE;
    int shift=key==F80_KEY_SHIFT_UP || key==F80_KEY_SHIFT_DOWN ||
              key==F80_KEY_SHIFT_LEFT || key==F80_KEY_SHIFT_RIGHT;
    if(shift && !g->selecting) {
        g->anchor_row=g->row;g->anchor_field=g->field;g->selecting=1;
    } else if(!shift && (key==F80_KEY_UP || key==F80_KEY_DOWN ||
              key==F80_KEY_LEFT || key==F80_KEY_RIGHT))g->selecting=0;
    int page=grid_body_height(g);
    switch(key) {
    case F80_KEY_UP:case F80_KEY_SHIFT_UP:g->row--;break;
    case F80_KEY_DOWN:case F80_KEY_SHIFT_DOWN:g->row++;break;
    case F80_KEY_LEFT:case F80_KEY_SHIFT_LEFT:g->field--;break;
    case F80_KEY_RIGHT:case F80_KEY_SHIFT_RIGHT:g->field++;break;
    case F80_KEY_PPAGE:g->row-=page;break;
    case F80_KEY_NPAGE:g->row+=page;break;
    case F80_KEY_HOME:g->row=0;break;
    case F80_KEY_END:g->row=g->rows-1;break;
    default:return 0;
    }
    g->row=bound(g->row,0,g->rows-1);
    g->field=bound(g->field,0,g->fields-1);
    grid_reveal(g);
    return 1;
}
static int grid_field_at(const F80Grid *g,int content_x) {
    int result=0;
    for(int field=0;field<g->fields;++field) {
        if(content_x<grid_field_start(g,field)+g->width[field]+1) {
            result=field;break;
        }
        result=field;
    }
    return result;
}
int f80_grid_mouse(F80Grid *g,int x,int y,int wheel) {
    F80Rect r=g->rect;
    if(x<r.x || y<r.y || x>=r.x+r.w || y>=r.y+r.h)return 0;
    if(wheel) { f80_grid_scroll(g,0,wheel*3);return 1; }
    if(g->show_h && g->show_v && x==r.x+r.w-1 && y==r.y+r.h-1)return 1;
    int data_x=r.x+grid_gutter_width(g),data_width=grid_body_width(g);
    if(g->show_h && y==r.y+r.h-1 && x>=data_x && x<data_x+data_width) {
        if(x==data_x)f80_grid_scroll(g,-1,0);
        else if(x==data_x+data_width-1)f80_grid_scroll(g,1,0);
        else if(data_width>3) {
            int maximum=grid_max_x(g);
            g->scroll_x=bound((x-data_x-1)*maximum/(data_width-3),0,maximum);
        }
        return 1;
    }
    if(g->show_v && x==r.x+r.w-1 && y<r.y+r.h-g->show_h) {
        int height=r.h-g->show_h;
        if(y==r.y)f80_grid_scroll(g,0,-1);
        else if(y==r.y+height-1)f80_grid_scroll(g,0,1);
        else if(height>3) {
            int maximum=grid_max_y(g);
            g->scroll_y=bound((y-r.y-1)*maximum/(height-3),0,maximum);
        }
        return 1;
    }
    if(y>r.y && g->rows && g->fields) {
        g->row=bound(g->scroll_y+y-r.y-1,0,g->rows-1);
        if(x>=data_x && x<data_x+data_width)
            g->field=grid_field_at(g,g->scroll_x+x-data_x);
        g->selecting=0;
    }
    return 1;
}
void f80_grid_hold(F80Grid *g) {
    g->anchor_row=g->row;g->anchor_field=g->field;g->selecting=1;
}
void f80_grid_drag(F80Grid *g,int x,int y) {
    if(!g->selecting || !g->rows || !g->fields)return;
    F80Rect r=g->rect;
    g->row=bound(g->scroll_y+y-r.y-1,0,g->rows-1);
    int data_x=r.x+grid_gutter_width(g);
    if(x>=data_x)g->field=grid_field_at(g,g->scroll_x+x-data_x);
    grid_reveal(g);
}
int f80_grid_selected(const F80Grid *g,int row,int field) {
    if(!g->selecting)return 0;
    int r0=g->anchor_row<g->row?g->anchor_row:g->row;
    int r1=g->anchor_row>g->row?g->anchor_row:g->row;
    int c0=g->anchor_field<g->field?g->anchor_field:g->field;
    int c1=g->anchor_field>g->field?g->anchor_field:g->field;
    return row>=r0 && row<=r1 && field>=c0 && field<=c1;
}
void f80_grid_bounds(const F80Grid *g,int *top,int *left,int *bottom,int *right) {
    int ar=g->selecting?g->anchor_row:g->row;
    int af=g->selecting?g->anchor_field:g->field;
    *top=ar<g->row?ar:g->row;
    *bottom=ar>g->row?ar:g->row;
    *left=af<g->field?af:g->field;
    *right=af>g->field?af:g->field;
}
static void grid_put_clipped(const F80Grid *g,int y,int start,const char *text,
                             int width,int pair,int bold) {
    int x=start-g->scroll_x,visible=grid_body_width(g);
    if(x<0) {
        int skipped=-x;
        if(skipped>=width)return;
        text+=skipped;width-=skipped;x=0;
    }
    if(x>=visible || width<=0)return;
    if(width>visible-x)width=visible-x;
    f80_put(y,g->rect.x+grid_gutter_width(g)+x,text,width,pair,bold);
}
static void grid_put_gutter(const F80Grid *g,int y,const char *text,
                            int pair,int bold) {
    int gutter=grid_gutter_width(g);
    int width=g->rect.w<gutter?g->rect.w:gutter;
    if(width>0)f80_put(y,g->rect.x,text,width,pair,bold);
}
void f80_grid_draw(F80Grid *g,F80GridText text,void *user) {
    F80Rect r=g->rect;
    if(r.w<1 || r.h<2)return;
    f80_fill(r,1);
    grid_put_gutter(g,r.y,g->corner?g->corner:"TCK",8,1);
    for(int field=0;field<g->fields;++field)
        grid_put_clipped(g,r.y,grid_field_start(g,field),g->heading[field],
                         g->width[field],8,1);
    int body=grid_body_height(g);
    for(int line=0;line<body && g->scroll_y+line<g->rows;++line) {
        int row=g->scroll_y+line,y=r.y+1+line;
        char gutter[64];
        if(g->row_text)g->row_text(user,row,-1,gutter,sizeof(gutter));
        else snprintf(gutter,sizeof(gutter),"%02X",row);
        grid_put_gutter(g,y,gutter,row==g->row?4:8,row==g->row);
        for(int field=0;field<g->fields;++field) {
            char value[256];value[0]=0;
            if(text)text(user,row,field,value,sizeof(value));
            int pair=g->editing && row==g->row && field==g->field?
                         (g->blink_on?7:3):
                     f80_grid_selected(g,row,field)?65:
                     row==g->row && field==g->field?
                         (g->read_only[field]?10:(g->focused?3:8)):
                     g->read_only[field]?10:2;
            grid_put_clipped(g,y,grid_field_start(g,field),value,
                             g->width[field],pair,row==g->row);
        }
    }
    int max_x=grid_max_x(g),data_x=r.x+grid_gutter_width(g);
    int data_width=grid_body_width(g),track=data_width-2;
    if(g->show_h) {
    f80_put(r.y+r.h-1,data_x,"◄",1,10,1);
    if(track>0) {
        int thumb=max_x?track*grid_body_width(g)/f80_grid_content_width(g):track;
        if(thumb<1)thumb=1;
        int at=max_x?(track-thumb)*g->scroll_x/max_x:0;
        for(int i=0;i<track;++i)
            f80_put(r.y+r.h-1,data_x+1+i,i>=at && i<at+thumb?"█":"▒",1,10,0);
    }
    f80_put(r.y+r.h-1,data_x+data_width-1,"►",1,10,1);
    }
    int max_y=grid_max_y(g),vertical=r.h-g->show_h-2;
    if(g->show_v) {
    f80_put(r.y,r.x+r.w-1,"▲",1,10,1);
    if(vertical>0) {
        int thumb=max_y?vertical*body/g->rows:vertical;
        if(thumb<1)thumb=1;
        int at=max_y?(vertical-thumb)*g->scroll_y/max_y:0;
        for(int i=0;i<vertical;++i)
            f80_put(r.y+1+i,r.x+r.w-1,i>=at && i<at+thumb?"█":"▒",1,10,0);
    }
    f80_put(r.y+r.h-g->show_h-1,r.x+r.w-1,"▼",1,10,1);
    }
}

void f80_wrap_init(F80Wrap *wrap,int gap_x,int gap_y) {
    memset(wrap,0,sizeof(*wrap));wrap->gap_x=gap_x;wrap->gap_y=gap_y;
}
void f80_wrap_layout(F80Wrap *wrap,F80Rect rect,F80Widget *const *items,int count) {
    wrap->rect=rect;
    int usable=rect.w>1?rect.w-1:1,x=0,y=0,line_height=0;
    for(int i=0;i<count;++i) {
        int width,height;f80_fit_size(items[i],&width,&height);
        if(width>usable)width=usable;
        if(x && x+width>usable) { x=0;y+=line_height+wrap->gap_y;line_height=0; }
        if(height>line_height)line_height=height;
        x+=width+wrap->gap_x;
    }
    wrap->content_height=y+line_height;
    int maximum=wrap->content_height-rect.h;
    wrap->scroll=bound(wrap->scroll,0,maximum>0?maximum:0);
    x=y=line_height=0;
    for(int i=0;i<count;++i) {
        int width,height;f80_fit_size(items[i],&width,&height);
        if(width>usable)width=usable;
        if(x && x+width>usable) { x=0;y+=line_height+wrap->gap_y;line_height=0; }
        if(height>line_height)line_height=height;
        int top=y-wrap->scroll;
        items[i]->visible=top>=0 && top+height<=rect.h;
        f80_layout(items[i],f80_rect(rect.x+x,rect.y+top,width,height));
        x+=width+wrap->gap_x;
    }
}
void f80_wrap_scroll(F80Wrap *wrap,int rows,F80Widget *const *items,int count) {
    int maximum=wrap->content_height-wrap->rect.h;
    wrap->scroll=bound(wrap->scroll+rows,0,maximum>0?maximum:0);
    f80_wrap_layout(wrap,wrap->rect,items,count);
}
int f80_wrap_mouse(F80Wrap *wrap,int x,int y,int wheel,
                   F80Widget *const *items,int count) {
    F80Rect r=wrap->rect;
    if(x<r.x || x>=r.x+r.w || y<r.y || y>=r.y+r.h)return 0;
    if(wheel)f80_wrap_scroll(wrap,wheel*3,items,count);
    else if(x==r.x+r.w-1 && wrap->content_height>r.h) {
        int maximum=wrap->content_height-r.h;
        wrap->scroll=bound(y==r.y?wrap->scroll-1:
                           y==r.y+r.h-1?wrap->scroll+1:
                           (y-r.y)*maximum/(r.h-1),0,maximum);
        f80_wrap_layout(wrap,r,items,count);
    } else return 0;
    return 1;
}
void f80_wrap_draw_scrollbar(const F80Wrap *wrap) {
    F80Rect r=wrap->rect;
    if(r.w<2 || r.h<2 || wrap->content_height<=r.h)return;
    int maximum=wrap->content_height-r.h,thumb=r.h*r.h/wrap->content_height;
    if(thumb<1)thumb=1;
    int at=(r.h-thumb)*wrap->scroll/maximum;
    for(int y=0;y<r.h;++y)
        f80_put(r.y+y,r.x+r.w-1,y>=at && y<at+thumb?"█":"▒",1,10,0);
}
