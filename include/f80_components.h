#ifndef F80_COMPONENTS_H
#define F80_COMPONENTS_H

#include "f80.h"

enum { F80_GRID_MAX_FIELDS=16 };
typedef struct {
    F80Rect rect;
    int rows,fields,width[F80_GRID_MAX_FIELDS];
    const char *heading[F80_GRID_MAX_FIELDS];
    int row,field,scroll_x,scroll_y;
    int show_h,show_v;
    int selecting,anchor_row,anchor_field;
} F80Grid;
typedef void (*F80GridText)(void *user,int row,int field,char *out,size_t size);

void f80_grid_init(F80Grid *grid,int rows,int fields,const int *width,
                   const char *const *heading);
void f80_grid_layout(F80Grid *grid,F80Rect rect);
void f80_grid_resize(F80Grid *grid,int rows);
int f80_grid_content_width(const F80Grid *grid);
void f80_grid_scroll(F80Grid *grid,int columns,int rows);
int f80_grid_key(F80Grid *grid,int key);
int f80_grid_mouse(F80Grid *grid,int x,int y,int wheel);
void f80_grid_hold(F80Grid *grid);
void f80_grid_drag(F80Grid *grid,int x,int y);
int f80_grid_selected(const F80Grid *grid,int row,int field);
void f80_grid_bounds(const F80Grid *grid,int *top,int *left,int *bottom,int *right);
void f80_grid_draw(F80Grid *grid,F80GridText text,void *user);

typedef struct { F80Rect rect; int scroll,content_height,gap_x,gap_y; } F80Wrap;
void f80_wrap_init(F80Wrap *wrap,int gap_x,int gap_y);
void f80_wrap_layout(F80Wrap *wrap,F80Rect rect,F80Widget *const *items,int count);
void f80_wrap_scroll(F80Wrap *wrap,int rows,F80Widget *const *items,int count);
int f80_wrap_mouse(F80Wrap *wrap,int x,int y,int wheel,
                   F80Widget *const *items,int count);
void f80_wrap_draw_scrollbar(const F80Wrap *wrap);

#endif
