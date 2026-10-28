// clang-format off
#include "pi_draw3d.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "pi_builtin.h"
#include "../src/pi_func.h"
#include "../src/pi_object.h"
#include "draw3d/generated_shaders.h"

// OpenGL function loading
#define GL_FUNCS(X)                                      \
    X(CreateShader, CREATESHADER)                        \
    X(ShaderSource, SHADERSOURCE)                        \
    X(CompileShader, COMPILESHADER)                      \
    X(GetShaderiv, GETSHADERIV)                          \
    X(GetShaderInfoLog, GETSHADERINFOLOG)                \
    X(DeleteShader, DELETESHADER)                        \
    X(CreateProgram, CREATEPROGRAM)                      \
    X(AttachShader, ATTACHSHADER)                        \
    X(LinkProgram, LINKPROGRAM)                          \
    X(GetProgramiv, GETPROGRAMIV)                        \
    X(GetProgramInfoLog, GETPROGRAMINFOLOG)              \
    X(UseProgram, USEPROGRAM)                            \
    X(DeleteProgram, DELETEPROGRAM)                      \
    X(GenVertexArrays, GENVERTEXARRAYS)                  \
    X(BindVertexArray, BINDVERTEXARRAY)                  \
    X(DeleteVertexArrays, DELETEVERTEXARRAYS)            \
    X(GenBuffers, GENBUFFERS)                            \
    X(BindBuffer, BINDBUFFER)                            \
    X(BufferData, BUFFERDATA)                            \
    X(DeleteBuffers, DELETEBUFFERS)                      \
    X(VertexAttribPointer, VERTEXATTRIBPOINTER)          \
    X(EnableVertexAttribArray, ENABLEVERTEXATTRIBARRAY)  \
    X(GetUniformLocation, GETUNIFORMLOCATION)            \
    X(UniformMatrix4fv, UNIFORMMATRIX4FV)

typedef struct
{
#define X(name, upper) PFNGL##upper##PROC name;
    GL_FUNCS(X)
#undef X
} GLFuncs;

/* Returns 1 when every function was found, 0 otherwise. */
static int _load_gl(GLFuncs *gl)
{
#define X(name, upper)                                                \
    gl->name = (PFNGL##upper##PROC)SDL_GL_GetProcAddress("gl" #name); \
    if (!gl->name)                                                    \
        return 0;
    GL_FUNCS(X)
#undef X
    return 1;
}


typedef struct
{
    float x, y, z;
} Vec3;

typedef struct
{
    float m[16];
} Mat4;

static Mat4 _mat4_identity(void)
{
    Mat4 r = {{0}};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

/* Returns a * b: "do b first, then a". */
static Mat4 _mat4_mul(Mat4 a, Mat4 b)
{
    Mat4 r;
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = sum;
        }
    return r;
}

static Mat4 _mat4_translate(float x, float y, float z)
{
    Mat4 r = _mat4_identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

static Mat4 _mat4_scale(float k)
{
    Mat4 r = _mat4_identity();
    r.m[0] = r.m[5] = r.m[10] = k;
    return r;
}

/* Turns things around the vertical (y) axis by `angle` radians. */
static Mat4 _mat4_rotateY(float angle)
{
    Mat4 r = _mat4_identity();
    float c = cosf(angle), s = sinf(angle);
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

/* Turns things around the horizontal (x) axis by `angle` radians. */
static Mat4 _mat4_rotateX(float angle)
{
    Mat4 r = _mat4_identity();
    float c = cosf(angle), s = sinf(angle);
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

/* Turns things around the depth (z) axis by `angle` radians. */
static Mat4 _mat4_rotateZ(float angle)
{
    Mat4 r = _mat4_identity();
    float c = cosf(angle), s = sinf(angle);
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}

/* Builds the matrix that places an object in the world:
 * scale it, rotate it (around x, then y, then z), then move it. */
static Mat4 _mat4_model(Vec3 position, Vec3 rotation, float scale)
{
    Mat4 m = _mat4_scale(scale);
    m = _mat4_mul(_mat4_rotateX(rotation.x), m);
    m = _mat4_mul(_mat4_rotateY(rotation.y), m);
    m = _mat4_mul(_mat4_rotateZ(rotation.z), m);
    
    return _mat4_mul(_mat4_translate(position.x, position.y, position.z), m);
}

/* Makes far things look smaller. `fov` is the vertical view angle in radians. */
static Mat4 _mat4_perspective(float fov, float aspect, float znear, float zfar)
{
    Mat4 r = {{0}};
    float zoom = 1.0f / tanf(fov / 2.0f);
    r.m[0] = zoom / aspect;
    r.m[5] = zoom;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

/* Camera settings. */
#define CAMERA_DISTANCE 4.0f  /* how far the cube is placed in front of us */
#define FIELD_OF_VIEW 0.7854f /* 45 degrees, in radians                    */
#define NEAR_PLANE 0.1f       /* closer than this is cut off               */
#define FAR_PLANE 100.0f      /* farther than this is cut off              */


typedef struct
{
    float position[3];
    float color[3];
} Vertex;

typedef struct
{
    GLuint vao, vbo, ebo;
    GLsizei index_count;
} Mesh;

typedef struct MeshNode
{
    Mesh mesh;
    struct MeshNode *next;
} MeshNode;

static void _mesh_destroy(const GLFuncs *gl, Mesh *mesh)
{
    if (mesh->ebo)
        gl->DeleteBuffers(1, &mesh->ebo);
    if (mesh->vbo)
        gl->DeleteBuffers(1, &mesh->vbo);
    if (mesh->vao)
        gl->DeleteVertexArrays(1, &mesh->vao);
    *mesh = (Mesh){0};
}

/* Uploads vertices and indices to the GPU. Returns 1 on success. */
static int _mesh_create(const GLFuncs *gl, Mesh *mesh,
                        const Vertex *vertices, size_t vertex_count,
                        const unsigned short *indices, size_t index_count)
{
    gl->GenVertexArrays(1, &mesh->vao);
    gl->GenBuffers(1, &mesh->vbo);
    gl->GenBuffers(1, &mesh->ebo);
    if (!mesh->vao || !mesh->vbo || !mesh->ebo)
    {
        _mesh_destroy(gl, mesh);
        return 0;
    }

    /* The VAO remembers the buffers and the vertex layout set up below,
     * so drawing later only needs a single bind. */
    gl->BindVertexArray(mesh->vao);

    gl->BindBuffer(GL_ARRAY_BUFFER, mesh->vbo);

    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertex_count * 
        sizeof(Vertex)), vertices, GL_STATIC_DRAW);

    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    
    gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(index_count * 
        sizeof(unsigned short)), indices, GL_STATIC_DRAW);

    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
        (void *)offsetof(Vertex, position));

    gl->EnableVertexAttribArray(0);
    
    gl->VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
        (void *)offsetof(Vertex, color));

    gl->EnableVertexAttribArray(1);

    mesh->index_count = (GLsizei)index_count;
    return 1;
}


typedef struct
{
    GLFuncs gl;
    GLuint program;
    GLint mvp_location;  /* looked up once, not every frame            */
    Mat4 camera;         /* projection * view, redone only on resize   */
    GLuint bound_vao;    /* lets us skip re-binding the same mesh      */
    MeshNode *meshes;
} Renderer;

static GLuint _compile_shader(const GLFuncs *gl, GLenum type, const char *source)
{
    GLuint shader = gl->CreateShader(type);
    gl->ShaderSource(shader, 1, &source, NULL);
    gl->CompileShader(shader);

    GLint ok = GL_FALSE;
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512] = {0};
        gl->GetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "draw3d shader error: %s\n", log);
        gl->DeleteShader(shader);
        return 0;
    }
    return shader;
}

/* Compiles and links the program. Cleans up after itself on every path. */
static GLuint _build_program(const GLFuncs *gl)
{
    GLuint vs = _compile_shader(gl, GL_VERTEX_SHADER, DRAW3D_DEFAULT_VERTEX_SOURCE);
    GLuint fs = _compile_shader(gl, GL_FRAGMENT_SHADER, DRAW3D_DEFAULT_FRAGMENT_SOURCE);
    GLuint program = 0;

    if (vs && fs)
    {
        program = gl->CreateProgram();
        gl->AttachShader(program, vs);
        gl->AttachShader(program, fs);
        gl->LinkProgram(program);

        GLint ok = GL_FALSE;
        gl->GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[512] = {0};
            gl->GetProgramInfoLog(program, sizeof(log), NULL, log);
            fprintf(stderr, "draw3d link error: %s\n", log);
            gl->DeleteProgram(program);
            program = 0;
        }
    }

    if (vs)
        gl->DeleteShader(vs);
    if (fs)
        gl->DeleteShader(fs);
    return program;
}

/* Call when the window size changes (and once at startup): updates the
 * viewport and recomputes the camera, so draw() never has to. */
static void _renderer_resize(Renderer *r, int width, int height)
{
    float aspect = (float)width / (float)(height > 0 ? height : 1);
    Mat4 projection = _mat4_perspective(FIELD_OF_VIEW, aspect, NEAR_PLANE, FAR_PLANE);
    Mat4 view = _mat4_translate(0, 0, -CAMERA_DISTANCE);
    r->camera = _mat4_mul(projection, view);
    glViewport(0, 0, width, height);
}

/* Only call with the owning GL context current. */
static void _renderer_destroy(Renderer *r)
{
    if (!r)
        return;
    MeshNode *node = r->meshes;
    while (node)
    {
        MeshNode *next = node->next;
        _mesh_destroy(&r->gl, &node->mesh);
        free(node);
        node = next;
    }
    if (r->program)
        r->gl.DeleteProgram(r->program);
    free(r);
}

static Renderer *_renderer_create(int width, int height)
{
    Renderer *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;

    if (!_load_gl(&r->gl))
    {
        free(r);
        return NULL;
    }

    r->program = _build_program(&r->gl);
    if (!r->program)
    {
        _renderer_destroy(r);
        return NULL;
    }

    r->mvp_location = r->gl.GetUniformLocation(r->program, "mvp");
    r->gl.UseProgram(r->program); /* the only program we use: set it once */

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE); /* skip faces turned away from us (free speedup) */

    _renderer_resize(r, width, height);
    return r;
}

static void _renderer_draw(Renderer *r, const Mesh *mesh, Mat4 model)
{
    Mat4 mvp = _mat4_mul(r->camera, model);
    r->gl.UniformMatrix4fv(r->mvp_location, 1, GL_FALSE, mvp.m);

    if (r->bound_vao != mesh->vao)
    {
        r->gl.BindVertexArray(mesh->vao);
        r->bound_vao = mesh->vao;
    }
    glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_SHORT, (void *)0);
}

static PiContext *_context(Value value)
{
    if (!IS_CONTEXT(value))
        return NULL;
    PiContext *ctx = AS_CONTEXT(value);
    return ctx->gl_context ? ctx : NULL;
}

static Vec3 _transform_vector(VM *vm, PiMap *transform, const char *name)
{
    Value value = map_getValueByKey(transform, name);
    if (IS_NIL(value))
        return (Vec3){0, 0, 0};
    if (!IS_LIST(value) || PILIST_SIZE(value) != 3)
        vm_errorf(vm, "draw() transform '%s' must be a three-number list", name);

    Value x = *(Value *)_list_getAt(AS_LIST(value)->items, 0);
    Value y = *(Value *)_list_getAt(AS_LIST(value)->items, 1);
    Value z = *(Value *)_list_getAt(AS_LIST(value)->items, 2);

    if (!IS_NUM(x) || !IS_NUM(y) || !IS_NUM(z))
        vm_errorf(vm, "draw() transform '%s' must be a three-number list", name);
        
    return (Vec3){(float)as_number(x), (float)as_number(y), (float)as_number(z)};
}

/* Checks that argv[0] is a live draw3d canvas and makes its OpenGL context
 * current, so the GL calls that follow go to the right window. */
static PiContext *_require_canvas(VM *vm, int argc, Value *argv, const char *message)
{
    PiContext *ctx = argc ? _context(argv[0]) : NULL;
    if (!ctx)
        vm_error(vm, message);
    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);
    return ctx;
}

Value d3_canvas(VM *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "canvas() takes width and height numbers");
    int width = (int)as_number(argv[0]), height = (int)as_number(argv[1]);
    if (width <= 0 || height <= 0)
        vm_error(vm, "canvas() width and height must be positive");
    const char *title = argc > 2 && IS_STRING(argv[2]) ? AS_CSTRING(argv[2]) : "PiLang 3D";

    SDL_Window *window = NULL;
    SDL_GLContext gl_context = NULL;
    Renderer *renderer = NULL;
    char error[256] = "";

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    /* Everything that can fail is created first; the Pilang object only once
     * nothing can fail anymore, so an error never leaves a half-made object. */
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        snprintf(error, sizeof(error), "Failed to create OpenGL window: %s", SDL_GetError());
        goto fail;
    }

    gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
    {
        snprintf(error, sizeof(error), "Failed to create OpenGL context: %s", SDL_GetError());
        goto fail;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    renderer = _renderer_create(width, height);
    if (!renderer)
    {
        snprintf(error, sizeof(error), "OpenGL 3.3 is required for draw3d");
        goto fail;
    }

    PiContext *ctx = (PiContext *)new_context();
    ctx->window = window;
    ctx->gl_context = gl_context;
    ctx->userdata = renderer;
    ctx->width = width;
    ctx->height = height;
    ctx->running = TRUE;
    ctx->cleaned = FALSE;
    ctx->frame_callback = NEW_NIL();
    return NEW_OBJ(add_obj(vm, (Object *)ctx));

fail:
    if (gl_context)
        SDL_GL_DeleteContext(gl_context);
    if (window)
        SDL_DestroyWindow(window);
    vm_errorf(vm, "%s", error);
    return NEW_NIL();
}

Value d3_clear(VM *vm, int argc, Value *argv)
{
    _require_canvas(vm, argc, argv, "clear() takes a draw3d canvas");
    int color = argc > 1 && IS_NUM(argv[1]) ? (int)as_number(argv[1]) : 0;
    glClearColor(((color >> 16) & 255) / 255.0f,
                 ((color >> 8) & 255) / 255.0f,
                 (color & 255) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return NEW_NIL();
}

static Value _mesh_field(VM *vm, PiMap *data, const char *name)
{
    Value value = map_getValueByKey(data, name);
    if (!IS_LIST(value))
        vm_errorf(vm, "mesh() '%s' must be a list", name);
    return value;
}

static Vec3 _mesh_vec3(VM *vm, Value value, const char *name, int index)
{
    if (!IS_LIST(value) || PILIST_SIZE(value) != 3)
        vm_errorf(vm, "mesh() %s[%d] must be a three-number list", name, index);
    Value x = *(Value *)_list_getAt(AS_LIST(value)->items, 0);
    Value y = *(Value *)_list_getAt(AS_LIST(value)->items, 1);
    Value z = *(Value *)_list_getAt(AS_LIST(value)->items, 2);
    if (!IS_NUM(x) || !IS_NUM(y) || !IS_NUM(z))
        vm_errorf(vm, "mesh() %s[%d] must be a three-number list", name, index);
    return (Vec3){(float)as_number(x), (float)as_number(y), (float)as_number(z)};
}

Value d3_mesh(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _require_canvas(vm, argc, argv, "mesh() takes a draw3d canvas and data map");
    if (argc != 2 || !IS_MAP(argv[1]))
        vm_error(vm, "mesh() takes a draw3d canvas and data map");

    PiMap *data = AS_MAP(argv[1]);

    Value positions = _mesh_field(vm, data, "position");
    Value colors = _mesh_field(vm, data, "color");
    Value indices = _mesh_field(vm, data, "indices");

    int vertex_count = PILIST_SIZE(positions);
    int index_count = PILIST_SIZE(indices);

    if (vertex_count <= 0 || vertex_count > 65535 || PILIST_SIZE(colors) != vertex_count)
        vm_error(vm, "mesh() position and color must have the same 1..65535 entries");

    if (index_count <= 0 || index_count % 3 != 0)
        vm_error(vm, "mesh() indices must contain complete triangles");

    Vertex *vertices = malloc((size_t)vertex_count * sizeof(*vertices));
    unsigned short *index_data = malloc((size_t)index_count * sizeof(*index_data));

    if (!vertices || !index_data)
    {
        free(vertices);
        free(index_data);
        vm_error(vm, "mesh() ran out of memory");
    }

    for (int i = 0; i < vertex_count; i++)
    {
        Vec3 position = _mesh_vec3(vm, *(Value *)_list_getAt(AS_LIST(positions)->items, i), "position", i);
        Vec3 color = _mesh_vec3(vm, *(Value *)_list_getAt(AS_LIST(colors)->items, i), "color", i);
        vertices[i] = (Vertex){{position.x, position.y, position.z}, {color.x, color.y, color.z}};
    }

    for (int i = 0; i < index_count; i++)
    {
        Value index = *(Value *)_list_getAt(AS_LIST(indices)->items, i);
        if (!IS_NUM(index) || as_number(index) < 0 || as_number(index) >= vertex_count 
                           || floor(as_number(index)) != as_number(index))
        {
            free(vertices);
            free(index_data);
            vm_errorf(vm, "mesh() indices[%d] must be a valid integer vertex index", i);
        }
        index_data[i] = (unsigned short)as_number(index);
    }

    MeshNode *node = calloc(1, sizeof(*node));
    Renderer *renderer = ctx->userdata;
    if (!node || !_mesh_create(&renderer->gl, &node->mesh, vertices, 
                (size_t)vertex_count, index_data, (size_t)index_count))
    {
        free(vertices);
        free(index_data);
        free(node);
        vm_error(vm, "mesh() failed to upload geometry to the GPU");
    }

    free(vertices);
    free(index_data);

    node->next = renderer->meshes;
    renderer->meshes = node;

    return NEW_OBJ(add_obj(vm, (Object *)new_model3d(ctx, node)));
}

Value d3_draw(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _require_canvas(vm, argc, argv, "draw() takes a canvas and draw3d model");
    if (argc < 2 || !IS_MODEL3D(argv[1]))
        vm_error(vm, "draw() takes a canvas and draw3d model");

    if (argc > 3 || (argc == 3 && !IS_MAP(argv[2])))
        vm_error(vm, "draw() transform must be a map");

    PiMap *transform = argc == 3 ? AS_MAP(argv[2]) : NULL;
    
    Vec3 position = _transform_vector(vm, transform, "position");
    Vec3 rotation = _transform_vector(vm, transform, "rotation");

    ObjModel3D *model3d = AS_MODEL3D(argv[1]);
    
    if (model3d->ctx != ctx || !model3d->native)
        vm_error(vm, "draw() model belongs to a different draw3d canvas");
    Mat4 model = _mat4_model(position, rotation, 1.0f);

    Renderer *renderer = ctx->userdata;
    _renderer_draw(renderer, &((MeshNode *)model3d->native)->mesh, model);
    return NEW_NIL();
}

Value d3_animate(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _require_canvas(vm, argc, argv, "animate() takes a canvas and callback");
    if (argc < 2 || !IS_FUN(argv[1]))
        vm_error(vm, "animate() takes a canvas and callback");
    ctx->frame_callback = argv[1];
    return NEW_NIL();
}

Value d3_close(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = argc ? _context(argv[0]) : NULL;
    if (ctx)
        ctx->running = FALSE;
    return NEW_NIL();
}

void d3_cleanupContext(PiContext *ctx)
{
    if (!ctx || ctx->cleaned)
        return;
    ctx->cleaned = TRUE;
    ctx->running = FALSE;

    if (ctx->gl_context && ctx->window)
        SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);
    _renderer_destroy(ctx->userdata);
    ctx->userdata = NULL;

    if (ctx->gl_context)
    {
        SDL_GL_DeleteContext(ctx->gl_context);
        ctx->gl_context = NULL;
    }
    if (ctx->window)
    {
        SDL_DestroyWindow(ctx->window);
        ctx->window = NULL;
    }
    ctx->frame_callback = NEW_NIL();
}

static void _handle_events(PiContext *ctx)
{
    SDL_Event event;
    Uint32 window_id = SDL_GetWindowID((SDL_Window *)ctx->window);
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_KEYDOWN && event.key.windowID == window_id &&
             event.key.keysym.sym == SDLK_ESCAPE))
            ctx->running = FALSE;

        if (event.type == SDL_WINDOWEVENT && event.window.windowID == window_id &&
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            ctx->width = event.window.data1;
            ctx->height = event.window.data2;
            _renderer_resize(ctx->userdata, ctx->width, ctx->height);
        }
    }
}

Value d3_run(VM *vm, int argc, Value *argv)
{
    PiContext *ctx = _require_canvas(vm, argc, argv, "run() takes a draw3d canvas");

    while (ctx->running)
    {
        _handle_events(ctx);
        if (!ctx->running)
            break;

        if (!IS_NIL(ctx->frame_callback))
        {
            Value args[] = {NEW_OBJ((Object *)ctx)};
            call_func(vm, AS_FUN(ctx->frame_callback), 1, args, NEW_NIL());
        }
        if (!ctx->running)
            break;
        SDL_GL_SwapWindow(ctx->window);
    }

    d3_cleanupContext(ctx);
    return NEW_NIL();
}


static BuiltinFunc draw3d_funcs[] = {
    {"canvas", d3_canvas},
    {"run", d3_run},
    {"clear", d3_clear},
    {"mesh", d3_mesh},
    {"draw", d3_draw},
    {"animate", d3_animate},
    {"close", d3_close},
};
static BuiltinConst draw3d_consts[] = {
    {"COLOR_BLACK", NEW_NUM(0x000000)},
    {"COLOR_WHITE", NEW_NUM(0xffffff)},
};
DEFINE_BUILTIN_MODULE(module_draw3d, "draw3d", draw3d_funcs, draw3d_consts);
