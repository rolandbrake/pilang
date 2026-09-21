#ifndef PI_NET_SOCKET_H
#define PI_NET_SOCKET_H

#include "../pi_builtin.h"

Value socket_Socket(vm_t *vm, int argc, Value *argv);
Value socket_connect(vm_t *vm, int argc, Value *argv);
Value socket_sendAll(vm_t *vm, int argc, Value *argv);
Value socket_receive(vm_t *vm, int argc, Value *argv);
Value socket_close(vm_t *vm, int argc, Value *argv);

extern BuiltinModule module_netSocket;

#endif // PI_NET_SOCKET_H
