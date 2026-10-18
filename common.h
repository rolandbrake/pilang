#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "pi_value.h"

#define PI_VERSION "Pi-Lang 0.1.3"

#define PI 3.1415926535897932384626433832795
#define E 2.7182818284590452353602874713527
#define TAU 6.283185307179586476925286766559
#define PHI 1.6180339887498948482045868343656

#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#define ALLOCATE(type, count) (type *)calloc(count, sizeof(type))

#define CLAMP(v, min, max) ((v) < (min) ? (min) : (v) > (max) ? (max) \
                                                              : (v))

#define PALETTE_SIZE 32

#define ERROR_COMPARE -2

typedef uint8_t Byte;
#define INIT_CAP 8 // initial capacity must not be zero

#define TARGET_FPS 60 // Target frames per second

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? -(a) : (a))

#define FALSE 0
#define TRUE 1


#define AND &&
#define OR  ||
#define NOT !

#define EQ  ==
#define NE  !=

#define LT  <
#define LE  <=
#define GT  >
#define GE  >=

#define ADD +
#define SUB -
#define MUL *
#define DIV /
#define MOD %

#define EPSILON 1e-5f

#ifndef NIL_VAL
#define NIL_VAL NEW_NIL()
#define TRUE_VAL NEW_BOOL(TRUE)
#define FALSE_VAL NEW_BOOL(FALSE)
#endif


// Maximum number of dimensions for tensors (e.g., matrices, higher-dimensional arrays).
#define MAX_TENSOR_DIMS 16



// A simple 3D vector structure
typedef struct
{
    float x, y, z; // 3D coordinates
} Vec3D;

typedef struct
{
    float u, v; // 2D texture coordinates
} Vec2D;

// Triangle structure used in rendering
typedef struct
{
    Vec3D v[3];       // The three vertices of the Triangle
    Vec2D t[3];       // The three texture coordinates
    short color;      // Index to a color in the palette
    float brightness; // Lighting brightness (0.0 to 1.0) used for shading
} Triangle;

char *itos(int num);
void error(const char *format, ...);
int find_paletteColor(uint8_t r, uint8_t g, uint8_t b);
bool is_AbsolutePath(const char *path);
// Returns an owned path, resolving relative paths from vm->current_path.
struct VM;
char *resolve_path(struct VM *vm, const char *path);

// Error handler callback definition
typedef void (*ErrorHandlerFunc)(const char *message, int line, int column);

// Global variable to store the custom error handler
extern ErrorHandlerFunc global_errorHandler;

// Function to set a custom error handler
void set_errorHandler(ErrorHandlerFunc handler);

// Command-line arguments (native only; emscripten may leave these empty)
extern int pi_cli_argc;
extern char **pi_cli_argv;

#define MAX_HANDLERS 32
#define MAX_EVENT_TYPES 16

// Event type enumeration
typedef enum
{
    EVENT_KEYDOWN,
    EVENT_KEYUP,
    EVENT_MOUSEDOWN,
    EVENT_MOUSEUP,
    EVENT_MOUSEMOVE,
    EVENT_CLICK,
    EVENT_SCROLL,
    EVENT_RESIZE,
    EVENT_CLOSE,
    EVENT_COUNT
} EventType;

#endif
