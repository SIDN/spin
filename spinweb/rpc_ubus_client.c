
#include <cJSON.h>

#include <libubus.h>
#include <libubox/blobmsg_json.h>

#define TIMEOUT 2000

static void
receive_call_result_data(struct ubus_request *req, int type, struct blob_attr *msg) {
    if (!msg) {
        return;
    }
    char* res = blobmsg_format_json_indent(msg, true, 0);
    char** result = (char**) req->priv;
    *result = res;
}


static int
ubus_cli_call(struct ubus_context *ctx, char** result, const char* path, const char* method, const char* arguments) {
    uint32_t id;
    int ret;
    struct blob_buf b;

    memset(&b, 0, sizeof(b));
    blob_buf_init(&b, 0);
    if (!blobmsg_add_json_from_string(&b, arguments)) {
        fprintf(stderr, "Failed to parse message data\n");
        return -1;
    }

    ret = ubus_lookup_id(ctx, path, &id);
    if (ret) {
        return ret;
    }

    return ubus_invoke(ctx, id, method, b.head, receive_call_result_data, result, TIMEOUT);
}

char*
send_ubus_message_raw(const char* request) {
    const char* path = "spin";
    const char* method = "rpc";
    struct ubus_context* ctx = NULL;
    char* result = NULL;
    cJSON* json_request;
    char* params_str;

    // The received request is a JSONRPC request
    // {"jsonrpc": "2.0", "id": 34154, "method": "list_devices", "params": {"a": "b"}}
    //
    // It's not very efficient, but we'll convert to cJSON, remove jsonrpc elements
    // convert back to string, then send that

    json_request = cJSON_Parse(request);
    if (json_request == NULL) {
        fprintf(stderr, "Error parsing JSON request: %s\n", request);
        goto done;
    }
    cJSON_DeleteItemFromObject(json_request, "id");
    cJSON_DeleteItemFromObject(json_request, "jsonrpc");
    params_str = cJSON_Print(json_request);

    ctx = ubus_connect(NULL);
    if (ctx == NULL) {
        fprintf(stderr, "Error connecting to UBUS\n");
        goto done;
    }

    int call_result = ubus_cli_call(ctx, &result, path, method, params_str);
    if (call_result != 0) {
        fprintf(stderr, "Error sending request to ubus: %s\n", ubus_strerror(call_result));
    }
    int i = 0;
    while (result == NULL && i < 10) {
        usleep(200000);
        i++;
    }
    if (result == NULL ) {
        fprintf(stderr, "ubus call timeout\n");
    }

done:
    if (ctx != NULL) {
        ubus_free(ctx);
    }
    if (params_str != NULL) {
        free(params_str);
    }
    if (json_request != NULL) {
        cJSON_Delete(json_request);
    }
    return result;
}
