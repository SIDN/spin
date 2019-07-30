#include "spindata.h"
#include "rpc_json.h"
#include "spinhook.h"

#include "spin_log.h"

#include "rpc_common.h"

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
    cJSON_AddNumberToObject(errorobj, "error", errorno);
    return errorobj;
}

static rpc_arg_t callreg_args[100];
static rpc_arg_t callreg_res;

static
spin_data rpc_json_callreg(char *method, spin_data jsonparams) {
    int nargs;
    spin_data jsonresult;
    cJSON *param;
    int res;

    nargs = 0;

    if (jsonparams != NULL && cJSON_IsObject(jsonparams)) {
        // Iterate over Object
        cJSON_ArrayForEach(param, jsonparams) {
            callreg_args[nargs].rpc_desc.rpca_name = param->string;
            if (cJSON_IsNumber(param)) {
                callreg_args[nargs].rpc_desc.rpca_type = RPCAT_INT;
                callreg_args[nargs].rpc_val.rpca_ivalue = param->valueint;
            } else if (cJSON_IsString(param)) {
                callreg_args[nargs].rpc_desc.rpca_type = RPCAT_STRING;
                callreg_args[nargs].rpc_val.rpca_svalue = param->valuestring;
            } else {
                callreg_args[nargs].rpc_desc.rpca_type = RPCAT_COMPLEX;
                // This must be checked !! TODO
                callreg_args[nargs].rpc_val.rpca_cvalue = param->child;
            }
            nargs++;
        }
    }

    res = rpc_call(method, nargs, callreg_args, &callreg_res);

    if (res != 0) {
        spin_log(LOG_ERR, "RPC not zero\n");
    }

    switch(callreg_res.rpc_desc.rpca_type) {
    case RPCAT_INT:
        jsonresult = cJSON_CreateNumber(callreg_res.rpc_val.rpca_ivalue);
        break;
    case RPCAT_STRING:
        jsonresult = cJSON_CreateString(callreg_res.rpc_val.rpca_svalue);
        break;
    case RPCAT_COMPLEX:
        jsonresult = callreg_res.rpc_val.rpca_cvalue;
        break;
    default: // Cannot happen
        jsonresult = NULL;
    }

    return jsonresult;
}

spin_data rpc_json(spin_data call_info) {
    spin_data jsonrpc;
    spin_data jsonid;
    spin_data jsonmethod;
    spin_data jsonparams;
    char *method;
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
        return json_error(call_info, 2);
    }
    method = jsonmethod->valuestring;

    jsonparams = cJSON_GetObjectItemCaseSensitive(call_info, "params");

    if (jsonparams != NULL && !cJSON_IsObject(jsonparams)) {
        return json_error(call_info, 3);
    }

    jsonretval = rpc_json_callreg(method, jsonparams);

    if (jsonid != 0) {
        jsonanswer = make_answer(jsonid);
        cJSON_AddItemToObject(jsonanswer, "result", jsonretval);
        return jsonanswer;
    }

    return NULL;
}

char *
call_ubus2jsonnew(char *args) {
    spin_data rpc, json_res;
    char *resultstr;
    static int nextid = 1;

    spin_log(LOG_DEBUG, "jsonnew(%s)\n", args);
    rpc = cJSON_Parse(args);
    cJSON_AddStringToObject(rpc, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(rpc, "id", ++nextid);

    json_res = rpc_json(rpc);

    cJSON_DeleteItemFromObjectCaseSensitive(json_res, "jsonrpc");
    cJSON_DeleteItemFromObjectCaseSensitive(json_res, "id");
    resultstr = cJSON_PrintUnformatted(json_res);

    spin_log(LOG_DEBUG, "jsonnew(%s) returns %s\n", args, resultstr);

    cJSON_Delete(rpc);
    cJSON_Delete(json_res);

    return resultstr;
}

#ifdef notdef
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
#endif

char *
call_string_jsonrpc(char *args) {
    spin_data rpc, json_res;
    char *resultstr;

    rpc = cJSON_Parse(args);

    json_res = rpc_json(rpc);

    resultstr = cJSON_PrintUnformatted(json_res);

    cJSON_Delete(rpc);
    cJSON_Delete(json_res);

    return resultstr;
}

static int rpc_fd;

// When ubus is not available, we listen in a unix domain socket
// for JSON RPC calls. This is the callback worker function when a call
// comes in
static void
wf_jsonrpc(void *arg, int data, int timeout) {
    spin_log(LOG_DEBUG, "Got JSON RPC command (data: %d)\n", data);
    char buf[4096] __attribute__ ((aligned));
    int rv;
    int msgsock;
    char* response = NULL;

    memset(buf, 0, 4096);
    if (data) {
        msgsock = accept(rpc_fd, NULL, NULL);
        rv = recv(msgsock, buf, sizeof(buf), 0);
        spin_log(LOG_DEBUG, "Received %d bytes of data\n", rv);
        // TODO: check for incomplete reads
        spin_log(LOG_DEBUG, "Got data: rv: %d buf: %s\n", rv, buf);
        response = call_string_jsonrpc(buf);
        spin_log(LOG_DEBUG, "json rpc called, response: %s\n", response);
        if (response != NULL) {
            write(msgsock, response, strlen(response));
            write(msgsock, "\n", 1);
        }
        spin_log(LOG_DEBUG, "Closing domain msg socket\n");
        close(msgsock);
    }
    if (timeout) {
        // called due to timeout, do nothing
        spin_log(LOG_DEBUG, "Timeout in RPC call, aborting");
    }
}

#include <sys/socket.h>
#include <sys/un.h>
#include "mainloop.h"

void
init_json_rpc() {
    spin_log(LOG_INFO, "No ubus; setting up JSON RPC handling\n");
    const char* socket_path = "/tmp/spin.sock";
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
    rpc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unlink(socket_path) != 0) {
        spin_log(LOG_ERR, "Error unlinking domain socket %s: %s\n", socket_path, strerror(errno));
        exit(errno);
    }
    if (bind(rpc_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        spin_log(LOG_ERR, "Error opening domain socket %s: %s\n", socket_path, strerror(errno));
        exit(errno);
    }
    if (listen(rpc_fd, 100) != 0) {
        spin_log(LOG_ERR, "Error listening on domain socket %s: %s\n", socket_path, strerror(errno));
        exit(errno);
    }
    spin_log(LOG_INFO, "Listening for JSON-RPC commands on %s\n", socket_path);
    mainloop_register("jsonrpc", wf_jsonrpc, (void *) 0, rpc_fd, 0);
}

void
cleanup_json_rpc() {
}
