#ifndef STR_BUILDER_H
#define STR_BUILDER_H

#include <stddef.h>

typedef struct str_builder str_builder_t;

str_builder_t *create_str_builder(void);
void destroy_str_builder(str_builder_t *sb);

void sb_strcat(str_builder_t *sb, const char *str);
void sb_putchar(str_builder_t *sb, char c);
void sb_cat_sprintf(str_builder_t *sb, const char *fmt, ...);
void sb_cat_random(str_builder_t *sb, size_t min_size, size_t max_size, const char *characters);
void sb_clear(str_builder_t *sb);

const char *sb2str(const str_builder_t *sb);
size_t sb_size(const str_builder_t *sb);

#endif
