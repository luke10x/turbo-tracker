#ifndef TT_PATTERN_H
#define TT_PATTERN_H

#include "tt_sample.h"

void tt_cell_text(const TtCell *cell,char text[11]);
void tt_pattern_effect_text(int command,int value,char text[6]);
void tt_pattern_row_text(const TtPattern *pattern,int row,char text[126]);
int tt_pattern_parse_row(const char *text,TtPattern *pattern,int row);
int tt_pattern_save(const char *path,const TtPattern *pattern,
                        char *error,size_t error_size);
int tt_pattern_load(const char *path,TtPattern *pattern,
                        char *error,size_t error_size);

#endif
