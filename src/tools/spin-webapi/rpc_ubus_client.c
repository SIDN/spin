
#include <libubus.h>

static void
receive_call_result_data(struct ubus_request *req, int type, struct blob_attr *msg) {
    char *str;
    if (!msg) {
        return;
    }

    str = blobmsg_format_json_indent(msg, true, simple_output ? -1 : 0);
    printf("[XX] UBUS CLIENT CALL RESULT: %s\n", str);
    free(str);
}


static int
ubus_cli_call(struct ubus_context *ctx, int argc, char **argv) {
    uint32_t id;
    int ret;

    if (argc < 2 || argc > 3)
        return -2;

    blob_buf_init(&b, 0);
    if (argc == 3 && !blobmsg_add_json_from_string(&b, argv[2])) {
        if (!simple_output)
            fprintf(stderr, "Failed to parse message data\n");
        return -1;
    }

    ret = ubus_lookup_id(ctx, argv[0], &id);
    if (ret)
        return ret;

    return ubus_invoke(ctx, id, argv[1], b.head, receive_call_result_data, NULL, timeout * 1000);
}


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

