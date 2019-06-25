#include "spindata.h"

char *call_ubus2json(const char *, char*);
char *call_ubus2jsonnew(char*);

typedef spin_data (*rpcfunc)(spin_data);
