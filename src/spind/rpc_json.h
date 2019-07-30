#include "spindata.h"

char *call_ubus2json(const char *, char*);
char *call_ubus2jsonnew(char*);

char *call_string_jsonrpc(char *args);

typedef spin_data (*rpcfunc)(spin_data);

void init_json_rpc();
