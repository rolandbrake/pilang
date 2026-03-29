#include "pi_type.h"
#include "pi_builtin.h"
#include "../pi_object.h"

/**
 * @brief Returns the type of the given value as a string.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments passed to the function.
 * @param argv The arguments provided to the function.
 * @return A string representing the type of the argument.
 */
Value _pi_type(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[type] expects at least one argument.");

    // Get the type name of the argument
    char *type = type_name(argv[0]);

    // Return the type name as a string object
    return NEW_OBJ(new_pistring(strdup(type)));
}

// Returns true if the argument is numeric (integer or float)
Value pi_isNum(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_num] expects one argument.");

    return NEW_BOOL(is_numeric(argv[0]));
}

// Returns true if the argument is a string
Value pi_isStr(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_str] expects one argument.");

    return NEW_BOOL(IS_STRING(argv[0]));
}

// Returns true if the argument is a boolean
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

Value pi_asNum(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[as_num] expects one argument.");

    if (is_numeric(argv[0]))
        return NEW_NUM(as_number(argv[0]));
    else
        vm_error(vm, "[as_num] argument is not numeric.");

    return NEW_NIL();
}

Value pi_asStr(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[as_str] expects one argument.");

    return NEW_OBJ(new_pistring(strdup(as_string(argv[0]))));
}

Value pi_asBool(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[as_bool] expects one argument.");

    return NEW_BOOL(as_bool(argv[0]));
}

Value tp_is(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "[is] expects two arguments: a value and a type string.");

    if (!IS_STRING(argv[1]))
        vm_error(vm, "[is] second argument must be a type string.");

    char *typeName = type_name(argv[0]);
    char *givenTypeName = AS_CSTRING(argv[1]);

    return NEW_BOOL(strcmp(typeName, givenTypeName) == 0);
}

// Returns the type name of any value.
Value tp_of(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[of] expects at least one argument.");

    char *type = type_name(argv[0]);
    return NEW_OBJ(new_pistring(strdup(type)));
}

// Returns memory size of a value in bytes.
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
        case OBJ_MATRIX:
            // For matrix, return number of elements (rows * cols)
            return NEW_NUM((double)(AS_MATRIX(arg)->rows * AS_MATRIX(arg)->cols));
        case OBJ_RANGE:
            // For range, return the number of elements in the range
            return NEW_NUM((double)(((AS_RANGE(arg)->end - AS_RANGE(arg)->start) / AS_RANGE(arg)->step) + 1));
        default:
            // For other object types, return the size of the Object header.
            // A more precise size would require inspecting the specific struct.
            return NEW_NUM((double)sizeof(Object));
        }
    }
    else
    {
        // For primitive types, return the size of the Value struct.
        return NEW_NUM((double)VALUE_SIZE);
    }
}

// Returns true if x is nil.
Value tp_nil(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[nil] expects one argument.");

    return NEW_BOOL(IS_NIL(argv[0]));
}

// Converts x to int. Parses strings, truncates floats.
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
        {
            vm_error(vm, "[int] cannot parse string to integer.");
        }
        return NEW_NUM((double)val);
    }
    else
    {
        vm_error(vm, "[int] argument must be a number or a string.");
    }
    return NEW_NIL();
}

// Converts x to a floating-point number.
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
    else
        vm_error(vm, "[float] argument must be a number or a string.");

    return NEW_NIL();
}

// Converts x to its string representation.
Value tp_string(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[string] expects one argument.");

    return NEW_OBJ(new_pistring(strdup(as_string(argv[0]))));
}

// Converts x to a boolean value.
Value tp_bool(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[bool] expects one argument.");

    return NEW_BOOL(as_bool(argv[0]));
}

// Converts an iterable to a list.
Value tp_list(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[list] expects one argument.");

    Value arg = argv[0];
    list_t *new_items = list_create(sizeof(Value));

    if (IS_LIST(arg))
    {
        // If it's already a list, create a shallow copy
        list_t *original_items = AS_LIST(arg)->items;
        for (int i = 0; i < original_items->size; i++)
        {
            list_add(new_items, list_getAt(original_items, i));
        }
    }
    else if (IS_OBJ(arg) && is_iterable(AS_OBJ(arg)))
    {
        // If it's an iterable object, iterate and add elements
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

// Converts a string or int list to a bytes object.
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
        {
            vm_error(vm, "[bytes] list argument must contain only numbers.");
        }
        for (int i = 0; i < plist->items->size; i++)
        {
            Value *element = (Value *)list_getAt(plist->items, i);
            Value value = *element;
            if (!IS_NUM(value) || AS_NUM(value) < 0 || AS_NUM(value) > 255 || AS_NUM(value) != (long)AS_NUM(value))
            {
                vm_error(vm, "[bytes] list elements must be integers between 0 and 255.");
            }
            list_add(byte_items, &value);
        }
    }
    else
    {
        vm_error(vm, "[bytes] argument must be a string or a list of numbers.");
    }

    return NEW_OBJ(new_list(byte_items));
}

// Module Registration

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
