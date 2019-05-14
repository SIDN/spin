#include "spindata.h"
#include "rpc_json.h"
#include "spinhook.h"

#include "spin_log.h"

extern node_cache_t* node_cache;

static spin_data
make_answer(spin_data id) {
    spin_data retobj;

    retobj = cJSON_CreateObject();
    cJSON_AddStringToObject(retobj, "jsonrpc", "2.0");
    cJSON_AddItemReferenceToObject(retobj, "id", id);
    return retobj;
}

static spin_data
json_error(spin_data call_info, int errorno) {
    spin_data errorobj;
    spin_data idobj;

    idobj = cJSON_GetObjectItemCaseSensitive(call_info, "id");
    errorobj = make_answer(idobj);
    // This should not be present I think
    // cJSON_AddNullToObject(errorobj, "result");
    cJSON_AddNumberToObject(errorobj, "error", errorno);
    return errorobj;
}

spin_data jsonrpc_devices(spin_data arg) {

    return spin_data_devicelist(node_cache);
}

spin_data jsonrpc_deviceflow(spin_data arg) {
    node_t *node;

    spin_log(LOG_DEBUG, "Start of deviceflow\n");
    if (!cJSON_IsString(arg)) {
        spin_log(LOG_DEBUG, "Not a string\n");
        return NULL;
    }
    spin_log(LOG_DEBUG, "Getting node by mac\n");
    node = node_cache_find_by_mac(node_cache, arg->valuestring);
    if (node == NULL) {
        spin_log(LOG_DEBUG, "Not a MAC address\n");
        return NULL;
    }
    return spin_data_flowlist(node);
}

spin_data jsonrpc_hello(spin_data arg) {

    return cJSON_CreateString("hello world");
}

struct funclist {
    const char * rpc_name;
    rpcfunc      rpc_func;
} funclist[] = {
    { "hello",  jsonrpc_hello },
    { "get_devices",  jsonrpc_devices },
    { "get_deviceflow",  jsonrpc_deviceflow },
    { 0, 0}
};

spin_data rpc_json(spin_data call_info) {
    spin_data jsonrpc;
    spin_data jsonid;
    spin_data jsonmethod;
    spin_data jsonparams;
    char *method;
    struct funclist *p;
    spin_data jsonretval;
    spin_data jsonanswer;

    jsonrpc = cJSON_GetObjectItemCaseSensitive(call_info, "jsonrpc");
    if (!cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0")) {
        return json_error(call_info, 1);
    }

    // Get id, if not there it is a Notification
    jsonid = cJSON_GetObjectItemCaseSensitive(call_info, "id");

    jsonmethod = cJSON_GetObjectItemCaseSensitive(call_info, "method");
    if (!cJSON_IsString(jsonmethod)) {
        return json_error(call_info, 1);
    }
    method = jsonmethod->valuestring;

    jsonparams = cJSON_GetObjectItemCaseSensitive(call_info, "params");

    for (p=funclist; p->rpc_name; p++) {
        if (strcmp(p->rpc_name, method) == 0) {
            break;
        }
    }

    if (p->rpc_name == 0) {
        return json_error(call_info, 1);
    }

    jsonretval = (*p->rpc_func)(jsonparams);

    jsonanswer = make_answer(jsonid);
    cJSON_AddItemToObject(jsonanswer, "result", jsonretval);

    return jsonanswer;
}

char *
call_ubus2json(const char *method, char *args) {
    spin_data sd;
    spin_data rpc, json_res, res;
    char *resultstr;
    static int nextid = 1;

    sd = cJSON_Parse(args);
    rpc = cJSON_CreateObject();
    cJSON_AddStringToObject(rpc, "jsonrpc", "2.0");
    cJSON_AddStringToObject(rpc, "method", method);
    cJSON_AddItemToObject(rpc, "params", sd);
    cJSON_AddNumberToObject(rpc, "id", ++nextid);

    json_res = rpc_json(rpc);

    res = cJSON_GetObjectItemCaseSensitive(json_res, "result");

    resultstr = cJSON_PrintUnformatted(res);

    cJSON_Delete(rpc);
    cJSON_Delete(json_res);

    return resultstr;
}

char *
call_string_jsonrpc(char *args) {
    spin_data rpc, json_res;
    char *resultstr;

    rpc = cJSON_Parse(args);

    spin_log(LOG_DEBUG, "About to call rpc_json\n");
    json_res = rpc_json(rpc);
    spin_log(LOG_DEBUG, "Back from call rpc_json\n");

    resultstr = cJSON_PrintUnformatted(json_res);

    cJSON_Delete(rpc);
    cJSON_Delete(json_res);

    return resultstr;
}

