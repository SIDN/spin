#ifndef SPIN_RPC_JSON_H
#define SPIN_RPC_JSON_H 1

#include "rpc_common.h"
#include "spindata.h"

#define JSON_RPC_SOCKET_PATH "/var/run/spin_rpc.sock"

char *call_ubus2json(const char *, char*);
char *call_ubus2jsonnew(char*);

char *call_string_jsonrpc(char *args);

typedef spin_data (*rpcfunc)(spin_data);

void init_json_rpc();

#endif // SPIN_RPC_JSON_HS
