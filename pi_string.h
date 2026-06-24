#ifndef PI_STRING_H
#define PI_STRING_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "pi_list.h"

typedef struct
{
    char *data;
    size_t length;
} String;

#define GET_STRING(str_ptr) ((str_ptr)->data)

String *new_string(const char *data);
char *string_get(list_t *list, int index);
void free_strings(list_t *list);
void free_string(String *string);

#endif /* PI_STRING_H */