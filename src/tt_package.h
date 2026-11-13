#ifndef TT_PACKAGE_H
#define TT_PACKAGE_H
#include "tt_song.h"

/* A .ttt has an authoritative text header followed by ZIP assets under
   samples/01.wav..08.wav and optional banks/01.syx..08.syx. Older ZIP-only
   packages with song.ttt inside remain readable. */
typedef struct {
    unsigned char data[TT_SYX_MAX_BYTES];
    size_t size;
} TtPackageBank;

int tt_package_is_path(const char *path);
int tt_package_save(const char *path,const TtSong *song,
                    const TtSongSample *samples,int count,
                    const char *const sample_paths[TT_SONG_SAMPLES],
                    const TtWave *const waves[TT_SONG_SAMPLES],
                    char *error,size_t error_size);
int tt_package_unpack(const char *path,char *song_path,size_t song_path_size,
                      TtPackageBank banks[TT_SONG_SAMPLES],
                      char *error,size_t error_size);
/* Archive asset references use path.zip!directory/sample.wav. */
int tt_package_list_wavs(const char *archive,char entries[][512],int capacity,
                         int *count,char *error,size_t error_size);
int tt_package_resolve_asset(const char *song_path,const char *reference,
                             char *loaded_path,size_t loaded_size,
                             char *error,size_t error_size);
void tt_package_release_asset(const char *loaded_path);
void tt_package_cleanup_all(void);
#endif
