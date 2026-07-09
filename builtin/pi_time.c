#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#endif

#include "pi_time.h"
#include "../pi_object.h"
#include "../pi_table.h"
#include "../pi_func.h"
#include "../pi_vm.h"
#include "pi_builtin.h"

#define SLEEP_INTERRUPT_SLICE_MS 25.0

static double now_millis(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static time_t timegm_portable(struct tm *tm_ptr)
{
#ifdef _WIN32
    return _mkgmtime(tm_ptr);
#else
    return timegm(tm_ptr);
#endif
}

static Value time_string(vm_t *vm, const char *text)
{
    return NEW_OBJ(add_obj(vm, new_pistring(strdup(text ? text : ""))));
}

static void map_putValue(PiMap *map, const char *key, Value value)
{
    if (ht_set(map->table, key, &value) || ht_put(map->table, key, &value))
        map_dirty(map);
}

static Value map_getValue(PiMap *map, const char *key, Value fallback)
{
    Value *value = (Value *)ht_get(map->table, key);
    return value ? *value : fallback;
}

static bool is_timeMap(Value value)
{
    return IS_MAP(value) && ht_get(AS_MAP(value)->table, "_type") != NULL;
}

static PiMap *require_timeMap(vm_t *vm, Value value, const char *name)
{
    if (!is_timeMap(value))
        vm_errorf(vm, "[time.%s] expects a Time map.", name);
    return AS_MAP(value);
}

static Value new_timeObject(vm_t *vm, const struct tm *tm_ptr, int millis, bool utc)
{
    struct tm copy = *tm_ptr;
    PiMap *map = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));

    map->proto = vm->object_proto;
    map->intrinsic_name = strdup("Time");
    map_putValue(map, "_type", time_string(vm, "Time"));
    map_putValue(map, "year", NEW_NUM(copy.tm_year + 1900));
    map_putValue(map, "month", NEW_NUM(copy.tm_mon + 1));
    map_putValue(map, "day", NEW_NUM(copy.tm_mday));
    map_putValue(map, "hour", NEW_NUM(copy.tm_hour));
    map_putValue(map, "minute", NEW_NUM(copy.tm_min));
    map_putValue(map, "second", NEW_NUM(copy.tm_sec));
    map_putValue(map, "millisecond", NEW_NUM(millis));
    map_putValue(map, "weekday", NEW_NUM(copy.tm_wday));
    map_putValue(map, "yearday", NEW_NUM(copy.tm_yday + 1));
    map_putValue(map, "isdst", NEW_BOOL(copy.tm_isdst > 0));
    map_putValue(map, "utc", NEW_BOOL(utc));

    time_t epoch = utc ? timegm_portable(&copy) : mktime(&copy);
    map_putValue(map, "unix", NEW_NUM((double)epoch));
    map_putValue(map, "millis", NEW_NUM((double)epoch * 1000.0 + millis));

    return NEW_OBJ(map);
}

static struct tm tm_fromMap(PiMap *map)
{
    struct tm tm_value;
    memset(&tm_value, 0, sizeof(struct tm));

    tm_value.tm_year = (int)as_number(map_getValue(map, "year", NEW_NUM(1970))) - 1900;
    tm_value.tm_mon = (int)as_number(map_getValue(map, "month", NEW_NUM(1))) - 1;
    tm_value.tm_mday = (int)as_number(map_getValue(map, "day", NEW_NUM(1)));
    tm_value.tm_hour = (int)as_number(map_getValue(map, "hour", NEW_NUM(0)));
    tm_value.tm_min = (int)as_number(map_getValue(map, "minute", NEW_NUM(0)));
    tm_value.tm_sec = (int)as_number(map_getValue(map, "second", NEW_NUM(0)));
    tm_value.tm_isdst = as_bool(map_getValue(map, "isdst", NEW_BOOL(false))) ? 1 : 0;

    return tm_value;
}

static int parse_fixedInt(const char *src, int width, int *out)
{
    int value = 0;
    for (int i = 0; i < width; i++)
    {
        if (src[i] < '0' || src[i] > '9')
            return 0;
        value = value * 10 + (src[i] - '0');
    }
    *out = value;
    return 1;
}

static bool parse_timeString(const char *text, const char *format, struct tm *tm_value, int *millis)
{
    memset(tm_value, 0, sizeof(struct tm));
    tm_value->tm_mday = 1;
    *millis = 0;

    const char *s = text;
    const char *f = format;

    while (*f)
    {
        if (*f != '%')
        {
            if (*s != *f)
                return false;
            s++;
            f++;
            continue;
        }

        f++;
        int parsed = 0;

        switch (*f)
        {
        case 'Y':
            if (!parse_fixedInt(s, 4, &parsed))
                return false;
            tm_value->tm_year = parsed - 1900;
            s += 4;
            break;
        case 'm':
            if (!parse_fixedInt(s, 2, &parsed))
                return false;
            tm_value->tm_mon = parsed - 1;
            s += 2;
            break;
        case 'd':
            if (!parse_fixedInt(s, 2, &parsed))
                return false;
            tm_value->tm_mday = parsed;
            s += 2;
            break;
        case 'H':
            if (!parse_fixedInt(s, 2, &parsed))
                return false;
            tm_value->tm_hour = parsed;
            s += 2;
            break;
        case 'M':
            if (!parse_fixedInt(s, 2, &parsed))
                return false;
            tm_value->tm_min = parsed;
            s += 2;
            break;
        case 'S':
            if (!parse_fixedInt(s, 2, &parsed))
                return false;
            tm_value->tm_sec = parsed;
            s += 2;
            break;
        case 'f':
            if (!parse_fixedInt(s, 3, &parsed))
                return false;
            *millis = parsed;
            s += 3;
            break;
        case '%':
            if (*s != '%')
                return false;
            s++;
            break;
        default:
            return false;
        }

        f++;
    }

    return *s == '\0';
}

static char *replace_millisToken(const char *format, int millis)
{
    char millis_buf[4];
    snprintf(millis_buf, sizeof(millis_buf), "%03d", millis);

    size_t size = strlen(format) + 32;
    char *result = (char *)malloc(size);
    result[0] = '\0';

    const char *cursor = format;
    while (*cursor)
    {
        if (cursor[0] == '%' && cursor[1] == 'f')
        {
            strcat(result, millis_buf);
            cursor += 2;
            continue;
        }

        size_t len = strlen(result);
        result[len] = *cursor;
        result[len + 1] = '\0';
        cursor++;
    }

    return result;
}

Value pi_sleep(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_NUM(argv[0]))
        vm_error(vm, "[sleep] expects a single millisecond number.");

    double ms = AS_NUM(argv[0]);
    if (ms < 0)
        ms = 0;

    while (ms > 0 && !interrupt_requested)
    {
        double chunk = ms < SLEEP_INTERRUPT_SLICE_MS ? ms : SLEEP_INTERRUPT_SLICE_MS;
#ifdef _WIN32
        Sleep((DWORD)chunk);
#else
        struct timespec req;
        req.tv_sec = (time_t)(chunk / 1000);
        req.tv_nsec = (long)((chunk - (req.tv_sec * 1000)) * 1e6);
        while (nanosleep(&req, &req) == -1 && errno == EINTR && !interrupt_requested)
        {
        }
#endif
        ms -= chunk;
    }

    return NEW_NIL();
}

Value _pi_time(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;
    return NEW_NUM(now_millis());
}

Value tm_now(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm *local = localtime(&now);
    return new_timeObject(vm, local, (int)(tv.tv_usec / 1000), false);
}

Value tm_utc(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm *utc = gmtime(&now);
    return new_timeObject(vm, utc, (int)(tv.tv_usec / 1000), true);
}

Value tm_unix(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;
    return NEW_NUM((double)time(NULL));
}

Value tm_millis(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;
    return NEW_NUM(now_millis());
}

Value tm_clock(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;

#ifdef _WIN32
    LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return NEW_NUM((double)counter.QuadPart / (double)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return NEW_NUM((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
#endif
}

Value tm_parse(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        vm_error(vm, "[time.parse] expects (string, format).");

    struct tm tm_value;
    int millis = 0;
    if (!parse_timeString(AS_CSTRING(argv[0]), AS_CSTRING(argv[1]), &tm_value, &millis))
        vm_error(vm, "[time.parse] failed to parse the given time string.");

    mktime(&tm_value);
    return new_timeObject(vm, &tm_value, millis, false);
}

Value tm_of(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3)
        vm_error(vm, "[time.of] expects year, month, day, and optional hour, minute, second.");

    for (int i = 0; i < argc && i < 6; i++)
        if (!IS_NUM(argv[i]))
            vm_error(vm, "[time.of] date parts must be numbers.");

    struct tm tm_value;
    memset(&tm_value, 0, sizeof(struct tm));
    tm_value.tm_year = (int)as_number(argv[0]) - 1900;
    tm_value.tm_mon = (int)as_number(argv[1]) - 1;
    tm_value.tm_mday = (int)as_number(argv[2]);
    tm_value.tm_hour = (argc >= 4) ? (int)as_number(argv[3]) : 0;
    tm_value.tm_min = (argc >= 5) ? (int)as_number(argv[4]) : 0;
    tm_value.tm_sec = (argc >= 6) ? (int)as_number(argv[5]) : 0;
    tm_value.tm_isdst = -1;

    mktime(&tm_value);
    return new_timeObject(vm, &tm_value, 0, false);
}

Value tm_format(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_STRING(argv[1]))
        vm_error(vm, "[time.format] expects (time, format).");

    PiMap *time_map = require_timeMap(vm, argv[0], "format");
    struct tm tm_value = tm_fromMap(time_map);
    int millis = (int)as_number(map_getValue(time_map, "millisecond", NEW_NUM(0)));

    char *format = replace_millisToken(AS_CSTRING(argv[1]), millis);
    char buffer[256];
    if (strftime(buffer, sizeof(buffer), format, &tm_value) == 0)
    {
        free(format);
        vm_error(vm, "[time.format] failed to format time.");
    }

    free(format);
    return time_string(vm, buffer);
}

Value tm_iso(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[time.iso] expects a single Time argument.");

    PiMap *time_map = require_timeMap(vm, argv[0], "iso");
    struct tm tm_value = tm_fromMap(time_map);
    bool utc = as_bool(map_getValue(time_map, "utc", NEW_BOOL(false)));

    char buffer[64];
    strftime(buffer, sizeof(buffer), utc ? "%Y-%m-%dT%H:%M:%SZ" : "%Y-%m-%dT%H:%M:%S", &tm_value);
    return time_string(vm, buffer);
}

static Value new_timerObject(vm_t *vm, double ms, bool repeating, int ticks)
{
    PiMap *map = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    map->proto = vm->object_proto;
    map->intrinsic_name = strdup("Timer");
    map_putValue(map, "_type", time_string(vm, "Timer"));
    map_putValue(map, "ms", NEW_NUM(ms));
    map_putValue(map, "repeating", NEW_BOOL(repeating));
    map_putValue(map, "active", NEW_BOOL(false));
    map_putValue(map, "ticks", NEW_NUM(ticks));
    return NEW_OBJ(map);
}

Value tm_timer(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[0]) || !IS_FUN(argv[1]))
        vm_error(vm, "[time.timer] expects (ms, callback).");

    Value sleep_args[1] = {argv[0]};
    pi_sleep(vm, 1, sleep_args);
    call_func(vm, AS_FUN(argv[1]), 0, NULL, NEW_NIL());
    return new_timerObject(vm, as_number(argv[0]), false, 1);
}

Value tm_interval(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[0]) || !IS_FUN(argv[1]))
        vm_error(vm, "[time.interval] expects (ms, callback).");

    double ms = as_number(argv[0]);
    int ticks = 0;

    while (1)
    {
        Value sleep_args[1] = {NEW_NUM(ms)};
        pi_sleep(vm, 1, sleep_args);
        Value result = call_func(vm, AS_FUN(argv[1]), 0, NULL, NEW_NIL());
        ticks++;

        if (!as_bool(result))
            break;
    }

    return new_timerObject(vm, ms, true, ticks);
}

static BuiltinFunc time_funcs[] = {
    {"now", tm_now},
    {"utc", tm_utc},
    {"unix", tm_unix},
    {"millis", tm_millis},
    {"clock", tm_clock},
    {"parse", tm_parse},
    {"of", tm_of},
    {"format", tm_format},
    {"iso", tm_iso},
    {"timer", tm_timer},
    {"interval", tm_interval},
};

DEFINE_BUILTIN_MODULE(module_time, "time", time_funcs, NULL);
