#include "pi_type.h"
#include "pi_builtin.h"
#include "../pi_object.h"

Value _pi_type(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[type] expects at least one argument.");

    char *type = type_name(argv[0]);
    return NEW_OBJ(new_pistring(strdup(type)));
}

Value pi_isNum(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_num] expects one argument.");

    return NEW_BOOL(is_numeric(argv[0]));
}

Value pi_isStr(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_str] expects one argument.");

    return NEW_BOOL(IS_STRING(argv[0]));
}

Value pi_isBool(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_bool] expects one argument.");

    return NEW_BOOL(IS_BOOL(argv[0]));
}

Value pi_isList(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_list] expects one argument.");

    return NEW_BOOL(IS_LIST(argv[0]));
}

Value pi_isMap(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_map] expects one argument.");

    return NEW_BOOL(IS_MAP(argv[0]));
}

Value pi_num(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[num] expects one argument.");

    if (is_numeric(argv[0]))
        return NEW_NUM(as_number(argv[0]));

    vm_error(vm, "[num] argument is not numeric.");
    return NEW_NIL();
}

Value pi_str(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[str] expects one argument.");

    return NEW_OBJ(new_pistring(pi_displayString(vm, argv[0])));
}

Value pi_bool(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[bool] expects one argument.");

    return NEW_BOOL(as_bool(argv[0]));
}

Value tp_is(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[is] expects two arguments: a value and a type string.");

    if (!IS_STRING(argv[1]))
        vm_error(vm, "[is] second argument must be a type string.");

    char *typeName = type_name(argv[0]);
    char *givenTypeName = AS_CSTRING(argv[1]);

    return NEW_BOOL(strcmp(typeName, givenTypeName) == 0);
}

Value tp_of(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[of] expects at least one argument.");

    char *type = type_name(argv[0]);
    return NEW_OBJ(new_pistring(strdup(type)));
}

Value tp_size(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[size] expects one argument.");

    Value arg = argv[0];

    if (IS_OBJ(arg))
    {
        Object *obj = AS_OBJ(arg);
        switch (obj->type)
        {
        case OBJ_STRING:
            return NEW_NUM((double)PISTR_SIZE(arg));
        case OBJ_LIST:
            return NEW_NUM((double)PILIST_SIZE(arg));
        case OBJ_MAP:
            return NEW_NUM((double)PIMAP_SIZE(arg));
        case OBJ_TENSOR:
            return NEW_NUM((double)AS_TENSOR(arg)->size);
        case OBJ_RANGE:
            return NEW_NUM((double)(((AS_RANGE(arg)->end - AS_RANGE(arg)->start) / AS_RANGE(arg)->step) + 1));
        default:
            // For unknown object layouts, report only the base object header size.
            return NEW_NUM((double)sizeof(Object));
        }
    }

    return NEW_NUM((double)VALUE_SIZE);
}

Value tp_nil(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[nil] expects one argument.");

    return NEW_BOOL(IS_NIL(argv[0]));
}

Value tp_int(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[int] expects one argument.");

    if (IS_NUM(argv[0]))
    {
        return NEW_NUM((double)(long)AS_NUM(argv[0]));
    }
    else if (IS_STRING(argv[0]))
    {
        char *endptr;
        long val = strtol(AS_CSTRING(argv[0]), &endptr, 10);

        if (*endptr != '\0')
            vm_error(vm, "[int] cannot parse string to integer.");

        return NEW_NUM((double)val);
    }

    vm_error(vm, "[int] argument must be a number or a string.");
    return NEW_NIL();
}

Value tp_float(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[float] expects one argument.");

    if (IS_NUM(argv[0]))
    {
        return argv[0];
    }
    else if (IS_STRING(argv[0]))
    {
        char *endptr;
        double val = strtod(AS_CSTRING(argv[0]), &endptr);

        if (*endptr != '\0')
            vm_error(vm, "[float] cannot parse string to float.");

        return NEW_NUM(val);
    }

    vm_error(vm, "[float] argument must be a number or a string.");
    return NEW_NIL();
}

Value tp_string(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[string] expects one argument.");

    return NEW_OBJ(new_pistring(pi_displayString(vm, argv[0])));
}

Value tp_bool(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[bool] expects one argument.");

    return NEW_BOOL(as_bool(argv[0]));
}

Value tp_list(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[list] expects one argument.");

    Value arg = argv[0];
    list_t *new_items = list_create(sizeof(Value));

    if (IS_LIST(arg))
    {
        // list(x) creates a shallow copy; contained objects are shared.
        list_t *original_items = AS_LIST(arg)->items;
        for (int i = 0; i < original_items->size; i++)
            list_add(new_items, list_getAt(original_items, i));
    }
    else if (IS_OBJ(arg) && is_iterable(AS_OBJ(arg)))
    {
        Object *iterable_obj = AS_OBJ(arg);
        iter_reset(iterable_obj);

        while (iter_hasNext(iterable_obj))
        {
            Value item = iter_next(iterable_obj);
            list_add(new_items, &item);
        }
    }
    else
        vm_error(vm, "[list] argument is not iterable.");

    return NEW_OBJ(new_list(new_items));
}

Value tp_bytes(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[bytes] expects one argument.");

    Value arg = argv[0];
    list_t *byte_items = list_create(sizeof(Value));

    if (IS_STRING(arg))
    {
        PiString *pstr = AS_STRING(arg);
        for (size_t i = 0; i < pstr->length; i++)
        {
            Value byte_value = NEW_NUM((double)(unsigned char)pstr->chars[i]);
            list_add(byte_items, &byte_value);
        }
    }
    else if (IS_LIST(arg))
    {
        PiList *plist = AS_LIST(arg);
        if (!plist->is_numeric)
            vm_error(vm, "[bytes] list argument must contain only numbers.");

        for (int i = 0; i < plist->items->size; i++)
        {
            Value *element = (Value *)list_getAt(plist->items, i);
            Value value = *element;

            // Bytes are represented as numeric values, but must be exact integers in [0, 255].
            if (!IS_NUM(value) || AS_NUM(value) < 0 || AS_NUM(value) > 255 || AS_NUM(value) != (long)AS_NUM(value))
                vm_error(vm, "[bytes] list elements must be integers between 0 and 255.");

            list_add(byte_items, &value);
        }
    }
    else
        vm_error(vm, "[bytes] argument must be a string or a list of numbers.");

    return NEW_OBJ(new_list(byte_items));
}

static BuiltinFunc type_funcs[] = {
    {"is", tp_is},
    {"of", tp_of},
    {"size", tp_size},
    {"nil", tp_nil},
    {"int", tp_int},
    {"float", tp_float},
    {"string", tp_string},
    {"bool", tp_bool},
    {"list", tp_list},
    {"bytes", tp_bytes},
};

DEFINE_BUILTIN_MODULE(module_type, "type", type_funcs, NULL);
