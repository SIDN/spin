/*
 * Copyright (C) 2011-2014 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mainloop.h"
#include "spin_log.h"
#include "spind.h"

#include <unistd.h>
#include <signal.h>

#include <libubox/blobmsg_json.h>
#include "libubus.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <fcntl.h>

#include "rpc_json.h"

static struct ubus_context *ctx;
static struct ubus_subscriber spin_event;
static struct blob_buf b;

static const struct blobmsg_policy rpc_policy[] = {
};

static int spin_rpc(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
    char *args, *result;

    args = blobmsg_format_json(msg, true);
    result = call_ubus2jsonnew(args);

    blob_buf_init(&b, 0);
    blobmsg_add_json_from_string(&b, result);
    free(result);   // This was malloced
    ubus_send_reply(ctx, req, b.head);
    return 0;
}

static const struct ubus_method spin_methods[] = {
    UBUS_METHOD("rpc", spin_rpc, rpc_policy),
};

static struct ubus_object_type spin_object_type =
    UBUS_OBJECT_TYPE("spin", spin_methods);

static struct ubus_object spin_object = {
    .name = "spin",
    .type = &spin_object_type,
    .methods = spin_methods,
    .n_methods = ARRAY_SIZE(spin_methods),
};

static int
fd_set_blocking(int fd, int blocking) {
    /* Save the current flags */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return 0;
    }

    if (blocking) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags) != -1;
}

void wf_ubus(void *arg, int data, int timeout) {

    spin_log(LOG_DEBUG, "wf_ubus called\n");

    if (data) {
        ubus_handle_event(ctx);
        // ctx->sock.cb(&ctx->sock, ULOOP_READ);
    }
}

int ubus_main()
{
	const char *ubus_socket = NULL;
	int ret;

	ctx = ubus_connect(ubus_socket);
	if (!ctx) {
            fprintf(stderr, "Failed to connect to ubus\n");
            return -1;
	} else {
            spin_log(LOG_DEBUG, "Connected to ubus\n");
        }

        fd_set_blocking(ctx->sock.fd, 0);
        mainloop_register("ubus", wf_ubus, NULL, ctx->sock.fd, 0);

	ret = ubus_add_object(ctx, &spin_object);
	if (ret) {
            fprintf(stderr, "Failed to add object: %s\n", ubus_strerror(ret));
        }

	ret = ubus_register_subscriber(ctx, &spin_event);
	if (ret) {
            fprintf(stderr, "Failed to add watch handler: %s\n", ubus_strerror(ret));
        }

	return 0;
}
