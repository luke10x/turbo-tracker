#ifndef F80_FILE_DIALOG_H
#define F80_FILE_DIALOG_H

#include "f80.h"
#include "f80_autocomplete.h"

enum { F80_FILE_DIALOG_PATH_MAX = 1024, F80_FILE_DIALOG_ITEMS = 512,
       F80_FILE_DIALOG_RECENT = 10 };
typedef enum { F80_FILE_OPEN, F80_FILE_SAVE } F80FileMode;
typedef void (*F80FileChosen)(const char *path, void *user);
typedef void (*F80FilePreviewChanged)(const char *path, void *user);

typedef struct {
    char name[256];
    int directory;
    unsigned long long size;
} F80FileEntry;

typedef struct F80FileBrowseApi {
    void *user;
    int (*list)(void *user,const char *directory,F80FileEntry *entries,
                int capacity,int *count,char *error,size_t error_size);
    int (*join)(void *user,const char *directory,const char *name,
                char *path,size_t path_size);
    int (*parent)(void *user,const char *directory,char *path,size_t path_size);
    int (*stat)(void *user,const char *path,int *directory,
                unsigned long long *size);
} F80FileBrowseApi;

typedef struct {
    F80App *app;
    F80Widget *dialog, *labels, *list_background, *list, *preview, *footer;
    F80Widget *input, *dropdown, *accept, *cancel, *help;
    F80FileMode mode;
    const F80FileBrowseApi *browse;
    F80FileChosen chosen;
    F80FilePreviewChanged preview_changed;
    void *user;
    char directory[F80_FILE_DIALOG_PATH_MAX];
    char filter[80];
    char title[80];
    char status[192];
    char overwrite_path[F80_FILE_DIALOG_PATH_MAX];
    F80FileEntry entries[F80_FILE_DIALOG_ITEMS];
    const char *labels_for_list[F80_FILE_DIALOG_ITEMS];
    char display[F80_FILE_DIALOG_ITEMS][260];
    int count;
    F80Autocomplete autocomplete;
    const char *recent_items[F80_FILE_DIALOG_RECENT];
    int recent_count;
} F80FileDialog;

/* Call layout each frame while active. The callback runs after the modal closes. */
int f80_file_dialog_show(F80FileDialog *dialog, F80App *app, F80FileMode mode,
                         const char *title, const char *initial_path,
                         const char *extensions, F80FileChosen chosen, void *user);
int f80_file_dialog_show_browse(F80FileDialog *dialog,F80App *app,
                                F80FileMode mode,const char *title,
                                const char *initial_path,const char *extensions,
                                const F80FileBrowseApi *browse,
                                F80FileChosen chosen,void *user);
const F80FileBrowseApi *f80_file_dialog_local_browse_api(void);
void f80_file_dialog_layout(F80FileDialog *dialog, int columns, int rows);
int f80_file_dialog_key(F80FileDialog *dialog, int key);
int f80_file_dialog_mouse(F80FileDialog *dialog, int x, int y, int wheel);
int f80_file_dialog_active(const F80FileDialog *dialog);
void f80_file_dialog_close(F80FileDialog *dialog);
/* Optional per-application storage; without it history lasts for this process. */
void f80_file_dialog_recent_storage(const char *path);
void f80_file_dialog_recent_add(const char *path);
void f80_file_dialog_set_preview(F80FileDialog *dialog,
                                 F80FilePreviewChanged preview_changed);

#endif
