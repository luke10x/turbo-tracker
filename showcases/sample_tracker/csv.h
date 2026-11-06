#ifndef F80_SAMPLE_TRACKER_CSV_H
#define F80_SAMPLE_TRACKER_CSV_H

#include "sample.h"

int sample_csv_save(const char *path,const SampleWave *wave,
                    const SamplePhrase *phrase,char *error,size_t error_size);
int sample_csv_load(const char *path,SampleWave *wave,
                    SamplePhrase *phrase,char *error,size_t error_size);

#endif
