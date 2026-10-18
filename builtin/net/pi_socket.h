#ifndef PI_NET_SOCKET_H
#define PI_NET_SOCKET_H

#include "../pi_builtin.h"

Value socket_Socket(VM *vm, int argc, Value *argv);
Value socket_connect(VM *vm, int argc, Value *argv);
Value socket_sendAll(VM *vm, int argc, Value *argv);
Value socket_receive(VM *vm, int argc, Value *argv);
Value socket_close(VM *vm, int argc, Value *argv);

extern BuiltinModule module_netSocket;

#endif // PI_NET_SOCKET_H
