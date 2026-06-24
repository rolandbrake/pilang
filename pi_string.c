#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pi_string.h"

String *new_string(const char *data)
{
    if (!data)
        return NULL;

    String *_string = malloc(sizeof(String));
    if (!_string)
        return NULL;

    _string->data = strdup(data);
    if (!_string->data)
    {
        free(_string);
        return NULL;
    }

    _string->length = strlen(data);
    return _string;
}

char *string_get(list_t *list, int index)
{
    if (!list)
        return NULL;
    String *_string = (String *)list_getAt(list, index);
    return _string ? _string->data : NULL;
}

void free_strings(list_t *list)
{
    if (!list)
        return;

    for (int i = 0; i < list_size(list); i++)
    {
        String *_string = (String *)list_getAt(list, i);
        if (_string && _string->data)
        {
            free(_string->data);
            _string->data = NULL;
        }
    }

    list_free(list);
}

void free_string(String *string)
{
    if (!string)
        return;
    free(string->data);
    free(string);
}