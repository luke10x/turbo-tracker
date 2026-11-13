#ifndef F80_AUTOCOMPLETE_H
#define F80_AUTOCOMPLETE_H

#include "f80.h"

typedef void (*F80AutocompleteChosen)(int index, void *user);

typedef struct {
    F80Widget *panel;
    const char *const *items;
    int count, columns, cursor, scroll_row, max_rows, active;
    F80AutocompleteChosen chosen;
    void *user;
} F80Autocomplete;

/* Attach last among a window's children so the purple popup draws on top. */
int f80_autocomplete_attach(F80Autocomplete *state, F80Widget *parent,
                            F80AutocompleteChosen chosen, void *user);
void f80_autocomplete_set(F80Autocomplete *state, const char *const *items,
                          int count, int columns);
void f80_autocomplete_open(F80Autocomplete *state);
void f80_autocomplete_close(F80Autocomplete *state);
void f80_autocomplete_layout(F80Autocomplete *state, F80Rect anchor,
                             F80Rect bounds, int max_rows);
int f80_autocomplete_key(F80Autocomplete *state, int key);
int f80_autocomplete_mouse(F80Autocomplete *state, int x, int y, int wheel);

#endif
