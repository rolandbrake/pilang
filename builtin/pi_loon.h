#ifndef PI_LOON_BUILTIN_H
#define PI_LOON_BUILTIN_H

#include <stdbool.h>
#include <stddef.h>

#include "../pi_vm.h"

typedef enum
{
    LOON_NULL,
    LOON_BOOL,
    LOON_NUMBER,
    LOON_STRING,
    LOON_ARRAY,
    LOON_OBJECT,
} loon_type_t;

typedef struct loon_value_t loon_value_t;
typedef struct
{
    char *key;
    loon_value_t *value;
} loon_member_t;

struct loon_value_t
{
    loon_type_t type;
    union
    {
        bool boolean;
        double number;
        char *string;
        struct
        {
            loon_value_t **items;
            size_t count;
        } array;
        struct
        {
            loon_member_t *members;
            size_t count;
        } object;
    } as;
};

typedef struct
{
    size_t line;
    size_t column;
    char message[160];
} loon_error_t;

loon_value_t *loon_parse(const char *source, loon_error_t *error);
void loon_free(loon_value_t *value);
const loon_value_t *loon_objectGet(const loon_value_t *value, const char *key);

Value loon_parseValue(vm_t *vm, int argc, Value *argv);

#endif
