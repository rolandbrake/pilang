#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "pi_socket.h"

#ifdef _WIN32
typedef SOCKET native_socket_t;
#define NET_INVALID_SOCKET INVALID_SOCKET
#define net_close closesocket
#else
typedef int native_socket_t;
#define NET_INVALID_SOCKET (-1)
#define net_close close
#endif

static PiSocket *require_socket(vm_t *vm, Value value)
{
    if (!IS_SOCKET(value))
        vm_error(vm, "socket operation expects a socket.");

    PiSocket *socket = AS_SOCKET(value);
    if (socket->closed)
        vm_error(vm, "socket is closed.");
    return socket;
}

static int timeout_fromOptions(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || IS_NIL(argv[2]))
        return 0;
    if (!IS_MAP(argv[2]))
        vm_error(vm, "socket.connect options must be a map.");

    Value value = map_getValueByKey(AS_MAP(argv[2]), "timeout");
    if (IS_NIL(value))
        return 0;
    if (!IS_NUM(value) || AS_NUM(value) < 0)
        vm_error(vm, "socket.connect timeout must be a non-negative number.");
    return (int)AS_NUM(value);
}

static void set_timeout(native_socket_t socket, int timeout_ms)
{
    if (timeout_ms <= 0)
        return;

#ifdef _WIN32
    DWORD timeout = (DWORD)timeout_ms;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

#ifdef _WIN32
static void ensure_winsock(vm_t *vm)
{
    static bool ready = false;
    if (ready)
        return;

    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        vm_error(vm, "socket initialization failed.");
    ready = true;
}
#endif

Value socket_Socket(vm_t *vm, int argc, Value *argv)
{
    if (argc > 3)
        vm_error(vm, "socket.Socket expects family, type, and optional protocol.");
    if ((argc > 0 && !IS_NUM(argv[0])) || (argc > 1 && !IS_NUM(argv[1])) ||
        (argc > 2 && !IS_NUM(argv[2])))
        vm_error(vm, "socket.Socket arguments must be numbers.");

    int family = argc > 0 ? (int)AS_NUM(argv[0]) : AF_INET;
    int type = argc > 1 ? (int)AS_NUM(argv[1]) : SOCK_STREAM;
    int protocol = argc > 2 ? (int)AS_NUM(argv[2]) : IPPROTO_TCP;
    if (type != SOCK_STREAM)
        vm_error(vm, "socket.Socket currently supports SOCK_STREAM only.");

#ifdef _WIN32
    ensure_winsock(vm);
#endif

    native_socket_t handle = socket(family, type, protocol);
    if (handle == NET_INVALID_SOCKET)
        vm_error(vm, "socket.Socket failed.");
    return NEW_OBJ(add_obj(vm, new_socket((intptr_t)handle, family, type, protocol)));
}

Value socket_connect(vm_t *vm, int argc, Value *argv)
{
    PiSocket *client = NULL;
    int argument_offset = 0;
    if (argc > 0 && IS_SOCKET(argv[0]))
    {
        client = require_socket(vm, argv[0]);
        argument_offset = 1;
    }
    if (argc < argument_offset + 2 || !IS_STRING(argv[argument_offset]) || !IS_NUM(argv[argument_offset + 1]))
        vm_error(vm, "socket.connect expects a socket, host, and port.");

    double port_value = AS_NUM(argv[argument_offset + 1]);
    if (port_value < 1 || port_value > 65535 || port_value != (int)port_value)
        vm_error(vm, "socket.connect port must be an integer between 1 and 65535.");

#ifdef _WIN32
    ensure_winsock(vm);
#endif

    char port[6];
    snprintf(port, sizeof(port), "%d", (int)port_value);

    struct addrinfo hints = {0};
    hints.ai_family = client ? client->family : AF_UNSPEC;
    hints.ai_socktype = client ? client->type : SOCK_STREAM;
    hints.ai_protocol = client ? client->protocol : IPPROTO_TCP;

    struct addrinfo *addresses = NULL;
    if (getaddrinfo(AS_CSTRING(argv[argument_offset]), port, &hints, &addresses) != 0)
        vm_error(vm, "socket.connect could not resolve host.");

    int timeout_ms = argc > argument_offset + 2
                         ? timeout_fromOptions(vm, 3, &argv[argument_offset])
                         : 0;
    native_socket_t handle = NET_INVALID_SOCKET;
    bool connected = false;
    for (struct addrinfo *address = addresses; address; address = address->ai_next)
    {
        if (client)
            handle = (native_socket_t)client->handle;
        else
        {
            handle = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (handle == NET_INVALID_SOCKET)
                continue;
        }

        set_timeout(handle, timeout_ms);
        if (connect(handle, address->ai_addr, (int)address->ai_addrlen) == 0)
        {
            connected = true;
            break;
        }

        if (client)
            break;
        net_close(handle);
        handle = NET_INVALID_SOCKET;
    }
    freeaddrinfo(addresses);

    if (!connected)
        vm_error(vm, "socket.connect failed.");

    if (client)
        return argv[0];
    return NEW_OBJ(add_obj(vm, new_socket((intptr_t)handle, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP)));
}

Value socket_sendAll(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_STRING(argv[1]))
        vm_error(vm, "socket.send_all expects a socket and string data.");

    PiSocket *socket = require_socket(vm, argv[0]);
    PiString *data = AS_STRING(argv[1]);
    size_t sent = 0;

    while (sent < data->length)
    {
        int written = send((native_socket_t)socket->handle, data->chars + sent,
                           (int)(data->length - sent), 0);
        if (written <= 0)
            vm_error(vm, "socket.send_all failed.");
        sent += (size_t)written;
    }

    return NEW_NUM((double)sent);
}

Value socket_receive(vm_t *vm, int argc, Value *argv)
{
    PiSocket *socket = require_socket(vm, argc > 0 ? argv[0] : NEW_NIL());
    int max_bytes = 4096;
    if (argc >= 2)
    {
        if (!IS_NUM(argv[1]) || AS_NUM(argv[1]) != (int)AS_NUM(argv[1]) || AS_NUM(argv[1]) <= 0)
            vm_error(vm, "socket.receive max_bytes must be a positive integer.");
        max_bytes = (int)AS_NUM(argv[1]);
    }

    char *buffer = malloc((size_t)max_bytes + 1);
    if (!buffer)
        vm_error(vm, "socket.receive allocation failed.");

    int count = recv((native_socket_t)socket->handle, buffer, max_bytes, 0);
    if (count < 0)
    {
        free(buffer);
        vm_error(vm, "socket.receive failed.");
    }

    buffer[count] = '\0';
    PiMap *result = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    Value data = NEW_OBJ(add_obj(vm, new_pistring(buffer)));
    Value eof = NEW_BOOL(count == 0);
    map_setValueByKey(result, "data", data);
    map_setValueByKey(result, "eof", eof);
    return NEW_OBJ(result);
}

Value socket_close(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_SOCKET(argv[0]))
        vm_error(vm, "socket.close expects a socket.");

    close_socket(AS_SOCKET(argv[0]));
    return NEW_NIL();
}

static BuiltinConst socket_consts[] = {
    {"AF_INET", NEW_NUM(AF_INET)},
    {"AF_INET6", NEW_NUM(AF_INET6)},
    {"SOCK_STREAM", NEW_NUM(SOCK_STREAM)},
    {"IPPROTO_TCP", NEW_NUM(IPPROTO_TCP)},
};

static BuiltinFunc socket_funcs[] = {
    {"Socket", skt_Socket},
    {"connect", skt_connect},
    {"send_all", skt_sendAll},
    {"receive", skt_receive},
    {"close", skt_close},
};

DEFINE_BUILTIN_MODULE(module_netSocket, "net.socket", socket_funcs, socket_consts);
