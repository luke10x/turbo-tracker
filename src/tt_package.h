#ifndef TT_PACKAGE_H
#define TT_PACKAGE_H
#include "tt_song.h"

/* A packed song contains song.ttt, samples/01.wav..08.wav, and optional
   banks/01.syx..08.syx. Names are fixed so no archive paths are trusted. */
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
void tt_package_cleanup_all(void);
#endif
