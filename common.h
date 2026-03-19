#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define PI_VERSION "Pi-Lang 0.1.0"

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

typedef uint8_t byte;
#define INIT_CAP 16 // initial capacity must not be zero

#define TARGET_FPS 60 // Target frames per second

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? -(a) : (a))

#define False 0
#define True 1

#define EPSILON 1e-5f





// A simple 3D vector structure
typedef struct
{
    float x, y, z; // 3D coordinates
} vec3d;

typedef struct
{
    float u, v; // 2D texture coordinates
} vec2d;

// Triangle structure used in rendering
typedef struct
{
    vec3d v[3];       // The three vertices of the triangle
    vec2d t[3];       // The three texture coordinates
    short color;      // Index to a color in the palette
    float brightness; // Lighting brightness (0.0 to 1.0) used for shading
} triangle;

char *itos(int num);
void error(const char *format, ...);
int find_paletteColor(uint8_t r, uint8_t g, uint8_t b);

// Error handler callback definition
typedef void (*error_handlerFn)(const char *message, int line, int column);

// Global variable to store the custom error handler
extern error_handlerFn global_errorHandler;

// Function to set a custom error handler
void set_errorHandler(error_handlerFn handler);

// Command-line arguments (native only; emscripten may leave these empty)
extern int pi_cli_argc;
extern char **pi_cli_argv;


#endif
