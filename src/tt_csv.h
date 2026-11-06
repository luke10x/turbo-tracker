#ifndef TT_CSV_H
#define TT_CSV_H

#include "tt_sample.h"

int tt_csv_save(const char *path,const TtWave *wave,
                    const TtPhrase *phrase,char *error,size_t error_size);
int tt_csv_load(const char *path,TtWave *wave,
                    TtPhrase *phrase,char *error,size_t error_size);

#endif
