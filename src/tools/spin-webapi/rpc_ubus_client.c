
#include <cJSON.h>

#include <libubus.h>
#include <libubox/blobmsg_json.h>

#define TIMEOUT 2000

static void
receive_call_result_data(struct ubus_request *req, int type, struct blob_attr *msg) {
    if (!msg) {
        return;
    }

    printf("[XX] UBUS CLIENT DATA POINTER %p deref: %p\n", req->priv, &(req->priv));

    char* res = blobmsg_format_json_indent(msg, true, 0);

    char** result = (char**) req->priv;
    *result = res;
    //printf("[XX] UBUS CLIENT CALL RESULT: %s\n", req->priv);

    printf("[XX] PLACED IT IN %p\n", req->priv);
}


//static struct blob_buf b;

static int
ubus_cli_call(struct ubus_context *ctx, char** result, const char* path, const char* method, const char* arguments) {
    uint32_t id;
    int ret;
    struct blob_buf b;

    printf("[XX] Call UBUS\n");
    printf("[XX] Path: %s, method: %s, arguments: %s\n", path, method, arguments);

    memset(&b, 0, sizeof(b));
    blob_buf_init(&b, 0);
    if (!blobmsg_add_json_from_string(&b, arguments)) {
        fprintf(stderr, "Failed to parse message data\n");
        return -1;
    }

    printf("[XX] ubus lookup id\n");
    ret = ubus_lookup_id(ctx, path, &id);
    if (ret) {
        return ret;
    }

    printf("[XX] calling ubus_invoke()\n");
    return ubus_invoke(ctx, id, method, b.head, receive_call_result_data, result, TIMEOUT);
}

/*
 * Synchronous ubus message request
 *
 * Returns the response as a string containing JSON data
 * Return data must be freed by called
 */
char*
send_ubus_message_raw(const char* request) {
    fprintf(stdout, "[XX] RPC Request to send to UBUS: %s\n", request);
    // The received request is a JSONRPC request
    // {"jsonrpc": "2.0", "id": 34154, "method": "list_devices", "params": {"a": "b"}}
    //
    struct ubus_context* ctx = NULL;
    char* result = NULL;

    cJSON* json_request = cJSON_Parse(request);
    if (json_request == NULL) {
        fprintf(stderr, "Error parsing JSON request: %s\n", request);
        goto done;
    }
    //char* method = cJSON_GetObjectItem(json_request, "method")->valuestring;
    //cJSON* params_json = cJSON_DetachItemFromObject(json_request, "params");
    cJSON_DeleteItemFromObject(json_request, "id");
    cJSON_DeleteItemFromObject(json_request, "jsonrpc");
    char* params_str = cJSON_Print(json_request);
    const char* path = "spin";
    const char* method = "rpc";

    printf("[XX] METHOD: %s\n", method);
    printf("[XX] PARAMS: %s\n", params_str);

    printf("[XX] connecting to ubus");
    ctx = ubus_connect(NULL);
    if (ctx == NULL) {
        fprintf(stderr, "Error connecting to UBUS\n");
        goto done;
    }

    printf("[XX] sending ubus request\n");
    int call_result = ubus_cli_call(ctx, &result, path, method, params_str);
    int i = 0;
    while (result == NULL && i < 10) {
        printf("[XX] waiting for result\n");
        usleep(200000);
        i++;
    }
    printf("[XX] result: %d %s\n", call_result, ubus_strerror(call_result));
    printf("[XX] result at %p now %p\n", &result, result);
    if (result == NULL ) {
        printf("[XX] ubus call timeout\n");
    } else {
        printf("[XX] ubus call result: %s\n", result);
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


#if 0

int
main(int argc, char **argv) {
    const char *progname, *ubus_socket = NULL;
    static struct ubus_context *ctx;
    char *cmd;
    int ret = 0;
    int i, ch;

    progname = argv[0];

    while ((ch = getopt(argc, argv, "vs:t:S")) != -1) {
        switch (ch) {
        case 's':
            ubus_socket = optarg;
            break;
        case 't':
            timeout = atoi(optarg);
            break;
        case 'S':
            simple_output = true;
            break;
        case 'v':
            verbose++;
            break;
        default:
            return usage(progname);
        }
    }

    argc -= optind;
    argv += optind;

    cmd = argv[0];
    if (argc < 1)
        return usage(progname);

    ctx = ubus_connect(ubus_socket);
    if (!ctx) {
        if (!simple_output)
            fprintf(stderr, "Failed to connect to ubus\n");
        return -1;
    }

    argv++;
    argc--;

    ret = -2;
    for (i = 0; i < ARRAY_SIZE(commands); i++) {
        if (strcmp(commands[i].name, cmd) != 0)
            continue;

        ret = commands[i].cb(ctx, argc, argv);
        break;
    }

    if (ret > 0 && !simple_output)
        fprintf(stderr, "Command failed: %s\n", ubus_strerror(ret));
    else if (ret == -2)
        usage(progname);

    ubus_free(ctx);
    return ret;
}

#endif