#ifndef F80_SAMPLE_TRACKER_PATTERN_H
#define F80_SAMPLE_TRACKER_PATTERN_H

#include "sample.h"

void sample_cell_text(const SampleCell *cell,char text[11]);
void sample_pattern_row_text(const SamplePattern *pattern,int row,char text[67]);
int sample_pattern_parse_row(const char *text,SamplePattern *pattern,int row);
int sample_pattern_save(const char *path,const SamplePattern *pattern,
                        char *error,size_t error_size);
int sample_pattern_load(const char *path,SamplePattern *pattern,
                        char *error,size_t error_size);

#endif
