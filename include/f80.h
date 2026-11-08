#ifndef F80_H
#define F80_H

/* F80: a C widget tree and Clay-style layout model based on program.py. */

#include <stddef.h>

typedef struct { int x, y, w, h; } F80Rect;
typedef enum { F80_FIT, F80_GROW, F80_FIXED, F80_PERCENT } F80SizeKind;
typedef struct { F80SizeKind kind; float value; int min, max; } F80Size;
typedef enum { F80_ROW, F80_COL } F80Direction;
/* A window owns a palette; ordinary boxes and controls inherit it. */
typedef enum { F80_WINDOW_INHERIT, F80_WINDOW_BLUE, F80_WINDOW_CYAN,
               F80_WINDOW_GRAY, F80_WINDOW_CONSOLE, F80_WINDOW_PURPLE,
               F80_WINDOW_ROLE_COUNT } F80WindowRole;
enum { F80_PALETTE_SLOTS = 31 };
enum { F80_PAIR_RANGE_CURSOR = 67 }; /* blue foreground on yellow selection */
typedef enum {
    F80_COLOR_PANEL=1, F80_COLOR_TEXT, F80_COLOR_SELECTION,
    F80_COLOR_HIGHLIGHT, F80_COLOR_BUTTON, F80_COLOR_STATUS,
    F80_COLOR_DIALOG, F80_COLOR_POSITIVE, F80_COLOR_NEGATIVE,
    F80_COLOR_ACCENT, F80_COLOR_MENU, F80_COLOR_SHADOW,
    F80_COLOR_MENU_HOT, F80_COLOR_MENU_ACTIVE, F80_COLOR_ALERT,
    F80_COLOR_STATUS_KEY, F80_COLOR_DIALOG_SHADOW,
    F80_COLOR_BUTTON_FOCUS, F80_COLOR_MENU_HOT_ACTIVE,
    F80_COLOR_CHART, F80_COLOR_BUTTON_HOT, F80_COLOR_DIALOG_FRAME,
    F80_COLOR_ALERT_SHADOW, F80_COLOR_BUTTON_SHADOW_BOTTOM,
    F80_COLOR_ALERT_SHADOW_BOTTOM,
    F80_COLOR_FORM_FIELD, F80_COLOR_FORM_FOCUS, F80_COLOR_FORM_HOT,
    F80_COLOR_FORM_INPUT, F80_COLOR_INPUT_OVERFLOW
} F80ColorSlot;
typedef struct { int pair[F80_PALETTE_SLOTS]; } F80Palette;
typedef enum { F80_STRETCH, F80_START, F80_CENTER, F80_END } F80Align;
typedef enum { F80_JUSTIFY_START, F80_JUSTIFY_CENTER, F80_JUSTIFY_END, F80_SPACE_BETWEEN } F80Justify;
typedef enum { F80_BOX, F80_SPACER, F80_LABEL, F80_BUTTON, F80_LISTBOX, F80_PROGRESS, F80_TABLE,
               F80_BARS, F80_LINECHART, F80_CANVAS, F80_INPUT, F80_MENUBAR,
               F80_STATUSBAR, F80_DIALOG, F80_RADIO_GROUP, F80_CHECK_GROUP } F80WidgetKind;
enum { F80_KEY_UP = 1001, F80_KEY_DOWN, F80_KEY_LEFT, F80_KEY_RIGHT,
       F80_KEY_HOME, F80_KEY_END, F80_KEY_PPAGE, F80_KEY_NPAGE,
       F80_KEY_BACKSPACE, F80_KEY_DELETE, F80_KEY_BTAB,
       F80_KEY_CTRL_LEFT, F80_KEY_CTRL_RIGHT,
       F80_KEY_SHIFT_LEFT, F80_KEY_SHIFT_RIGHT,
       F80_KEY_SHIFT_UP, F80_KEY_SHIFT_DOWN };

typedef struct F80Widget F80Widget;
typedef struct F80App F80App;
typedef void (*F80Action)(F80Widget *, void *);
typedef void (*F80Paint)(F80Widget *, void *);
typedef struct { const char *label, *shortcut; F80Action action; int enabled; } F80MenuItem;
typedef struct { const char *title; const F80MenuItem *items; int count; } F80Menu;
typedef struct { const char *key, *label; F80Action action; } F80StatusItem;
typedef struct { const char *title, *group; int width, align, pair; } F80TableField;
typedef struct {
    F80Rect rect;
    unsigned char *dots, *colors, *priority;
} F80Raster;

struct F80Widget {
    F80WidgetKind kind;
    F80Rect rect;
    F80Size width, height;
    F80Widget *parent, **children;
    size_t child_count, child_capacity;
    F80App *app;
    int visible, enabled, focusable;
    F80Direction direction;
    F80Align align;
    F80Justify justify;
    int padding_x, padding_y, gap, frame, fill;
    F80WindowRole window_role;
    F80Palette palette; /* zero entries inherit from the window/app palette */
    const char *title, *text;
    const char **items;
    int item_count, cursor, scroll;
    int scroll_detached; /* allow list viewport to move without moving its cursor */
    int range_active,range_first,range_last; /* optional highlighted list rows */
    int range_cursor_pair; /* optional foreground/background for selected row in a range */
    int selected; /* radio-group selection; -1 when the group is empty */
    unsigned char *checked; /* owned checkbox state, one entry per item */
    int gutter_width; /* optional line-number gutter for list/table content */
    float value;
    int indeterminate;
    int progress_sec, progress_total_sec;
    F80Action action;
    F80Action activate;
    void *user;
    F80Paint paint;
    char input[256];
    int input_pos, input_scroll;
    int pressed_until;
    const F80Menu *menus;
    int menu_count, menu_active, menu_index, menu_item;
    const F80StatusItem *status_items;
    int status_count;
    int style;
    const char *table_group_header;
    int table_col, table_col_count, table_starts[16], table_hscroll;
    const F80TableField *table_fields;
    const char *const *const *table_rows;
    int table_col_scroll, table_visible_count, table_visible_index[16];
    int table_visible_x[16], table_visible_width[16];
    F80Action sort;
    int table_sort_col, table_sort_desc;
    const char *const **table_sorted;
    const int *bar_values;
    const float *chart_walk;
    int chart_legend;
    int chart_raster_mode; /* 0 Braille, 1 octants (2x4), 2 quadrants (2x2) */
    const char *const *dialog_lines;
    const int *dialog_pairs;
    int dialog_count, dialog_align, dialog_default, dialog_min_width;
};

struct F80App {
    F80Widget *root, *modal, *focus, *saved_focus;
    F80Widget *modal_stack[8], *modal_focus[8];
    int modal_depth;
    int tick, animation_tick, running;
    void *user;
    F80Widget *menu;
    F80Palette palettes[F80_WINDOW_ROLE_COUNT];
};

F80Size f80_fit(void);
F80Size f80_grow(void);
F80Size f80_fixed(int cells);
F80Size f80_percent(float fraction);
F80Rect f80_rect(int x, int y, int w, int h);
F80Rect f80_inset(F80Rect rect, int x, int y);
F80Rect f80_content_rect(const F80Widget *widget);
int f80_contains(F80Rect rect, int x, int y);
F80Widget *f80_widget(F80WidgetKind kind);
F80Widget *f80_box(F80Direction direction);
F80Widget *f80_window(F80WindowRole role, F80Direction direction);
void f80_palette_set(F80Palette *palette, int slot, int pair);
int f80_palette_pair(const F80Widget *widget, int slot);
F80Widget *f80_spacer(void);
F80Widget *f80_label(const char *text);
F80Widget *f80_button(const char *text, F80Action action, void *user);
F80Widget *f80_listbox(const char **items, int count, F80Action action, void *user);
F80Widget *f80_radio_group(const char **items, int count, int selected,
                           F80Action action, void *user);
F80Widget *f80_check_group(const char **items, int count, F80Action action, void *user);
int f80_checked(const F80Widget *group, int index);
void f80_set_checked(F80Widget *group, int index, int checked);
F80Widget *f80_progress(float value, int indeterminate);
void f80_progress_set_time(F80Widget *w, int sec, int total_sec);
F80Widget *f80_table(const char **rows, int count);
void f80_table_columns(F80Widget *table, const int *starts, int count);
void f80_table_grid(F80Widget *table, const F80TableField *fields, int count,
                    const char *const *const *rows);
const char *f80_table_cell(const F80Widget *table, int row, int column);
void f80_table_sort(F80Widget *table, int column);
F80Widget *f80_bars(const char **labels, int count);
void f80_bars_values(F80Widget *bars, const int *values);
F80Widget *f80_linechart(void);
F80Widget *f80_canvas(F80Paint paint, void *user);
F80Widget *f80_input(void);
F80Widget *f80_menubar(const F80Menu *menus, int count);
F80Widget *f80_statusbar(const F80StatusItem *items, int count);
F80Widget *f80_dialog(const char *title, const char *const *lines,
                      const int *pairs, int count, int alert);
void f80_menu_open(F80Widget *menu, int index);
void f80_menu_close(F80Widget *menu);
F80Widget *f80_add(F80Widget *parent, F80Widget *child);
void f80_sized(F80Widget *widget, F80Size width, F80Size height);
void f80_set_root(F80App *app, F80Widget *root);
void f80_show_modal(F80App *app, F80Widget *modal);
void f80_hide_modal(F80App *app);
void f80_free(F80Widget *widget);
void f80_fit_size(F80Widget *widget, int *width, int *height);
void f80_layout(F80Widget *widget, F80Rect rect);
void f80_draw(F80Widget *widget);
int f80_listbox_row_pair(const F80Widget *widget,int row);
void f80_draw_screen(F80App *app);
void f80_focus_next(F80App *app, int step);
int f80_key(F80App *app, int key);
int f80_mouse(F80App *app, int x, int y, int wheel);
void f80_put(int y, int x, const char *text, int width, int pair, int bold);
void f80_fill(F80Rect rect, int pair);
void f80_frame(F80Rect rect, const char *title, int pair);
void f80_colors(void);
F80Raster f80_raster(F80Rect rect);
void f80_raster_pixel(F80Raster *raster, int x, int y, int pair, int priority);
void f80_raster_line(F80Raster *raster, int x0, int y0, int x1, int y1, int pair, int priority);
void f80_raster_outline(F80Raster *raster, int x, int y, int width, int height, int pair, int priority);
/* Normalized canvas coordinates: x=-1 left, x=+1 right, y=+1 top, y=-1 bottom. */
void f80_raster_point_norm(F80Raster *raster, float x, float y, int pair, int priority);
void f80_raster_line_norm(F80Raster *raster, float x0, float y0, float x1, float y1, int pair, int priority);
void f80_raster_rect_norm(F80Raster *raster, float left, float bottom, float right, float top, int pair, int priority);
void f80_raster_ellipse_norm(F80Raster *raster, float cx, float cy, float rx, float ry, int pair, int priority);
void f80_raster_draw(F80Raster *raster);
unsigned f80_octant_codepoint(unsigned char braille_dots);
void f80_raster_draw_octants(F80Raster *raster);
void f80_raster_draw_quadrants(F80Raster *raster);
void f80_raster_free(F80Raster *raster);

#endif
