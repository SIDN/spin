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

static struct ubus_context *ctx;
static struct ubus_subscriber spin_event;
static struct blob_buf b;

enum {
	SPINDLIST_LIST,
	SPINDLIST_ADDREM,
	SPINDLIST_NODE,
	__SPINDLIST_MAX
};

static const struct blobmsg_policy spindlist_policy[] = {
	[SPINDLIST_LIST] = { .name = "list", .type = BLOBMSG_TYPE_INT32 },
	[SPINDLIST_ADDREM] = { .name = "addrem", .type = BLOBMSG_TYPE_INT32 },
	[SPINDLIST_NODE] = { .name = "node", .type = BLOBMSG_TYPE_INT32 },
};

static int spin_spindlist(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct blob_attr *tb[__SPINDLIST_MAX];
        int list, addrem, node;
        void handle_list_membership(int, int, int);

	blobmsg_parse(spindlist_policy, ARRAY_SIZE(spindlist_policy), tb, blob_data(msg), blob_len(msg));

        list = 0; addrem = 0; node = 0;
	if (tb[SPINDLIST_LIST])
		list = blobmsg_get_u32(tb[SPINDLIST_LIST]);
	if (tb[SPINDLIST_ADDREM])
		addrem = blobmsg_get_u32(tb[SPINDLIST_ADDREM]);
	if (tb[SPINDLIST_NODE])
		node = blobmsg_get_u32(tb[SPINDLIST_NODE]);

        fprintf(stderr, "spindlist(%d, %d, %d) called\n", list, addrem, node);

        handle_list_membership(list, addrem, node);

	blob_buf_init(&b, 0);
	blobmsg_add_string(&b, "fake-return", "ok");
	ubus_send_reply(ctx, req, b.head);
	return 0;
}

enum {
	BLOCKFLOW_NODE1,
	BLOCKFLOW_NODE2,
        BLOCKFLOW_BLOCK,
	__BLOCKFLOW_MAX
};

static const struct blobmsg_policy blockflow_policy[] = {
	[BLOCKFLOW_NODE1] = { .name = "node1", .type = BLOBMSG_TYPE_INT32 },
	[BLOCKFLOW_NODE2] = { .name = "node2", .type = BLOBMSG_TYPE_INT32 },
	[BLOCKFLOW_BLOCK] = { .name = "block", .type = BLOBMSG_TYPE_INT32 },
};

static int spin_blockflow(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct blob_attr *tb[__BLOCKFLOW_MAX];
        int node1, node2, block;
        int result;

	blobmsg_parse(blockflow_policy, ARRAY_SIZE(blockflow_policy), tb, blob_data(msg), blob_len(msg));

        node1 = 0; node2 = 0; block = 0;
	if (tb[BLOCKFLOW_NODE1])
		node1 = blobmsg_get_u32(tb[BLOCKFLOW_NODE1]);
	if (tb[BLOCKFLOW_NODE2])
		node2 = blobmsg_get_u32(tb[BLOCKFLOW_NODE2]);
	if (tb[BLOCKFLOW_BLOCK])
		block = blobmsg_get_u32(tb[BLOCKFLOW_BLOCK]);

        fprintf(stderr, "blockflow(%d, %d, %d) called\n", node1, node2, block);

        result = spinrpc_blockflow(node1, node2, block);

	blob_buf_init(&b, 0);
	blobmsg_add_u32(&b, "return", result);
	ubus_send_reply(ctx, req, b.head);
	return 0;
}

enum {
	__GET_BLOCKFLOW_MAX
};

static const struct blobmsg_policy get_blockflow_policy[] = {
};

static int spin_get_blockflow(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct blob_attr *tb[__GET_BLOCKFLOW_MAX];
        char *result;

	blobmsg_parse(get_blockflow_policy, ARRAY_SIZE(get_blockflow_policy), tb, blob_data(msg), blob_len(msg));

        fprintf(stderr, "get_blockflow() called\n");

        result = spinrpc_get_blockflow();

	blob_buf_init(&b, 0);
        blobmsg_add_json_from_string(&b, result);
        free(result);   // This was malloced
	ubus_send_reply(ctx, req, b.head);
	return 0;
}

static const struct ubus_method spin_methods[] = {
	UBUS_METHOD("spindlist", spin_spindlist, spindlist_policy),
	UBUS_METHOD("blockflow", spin_blockflow, blockflow_policy),
	UBUS_METHOD("get_blockflow", spin_get_blockflow, get_blockflow_policy),
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

static void server_main(void)
{
	int ret;

	ret = ubus_add_object(ctx, &spin_object);
	if (ret)
		fprintf(stderr, "Failed to add object: %s\n", ubus_strerror(ret));

	ret = ubus_register_subscriber(ctx, &spin_event);
	if (ret)
		fprintf(stderr, "Failed to add watch handler: %s\n", ubus_strerror(ret));

}

int ubus_main()
{
	const char *ubus_socket = NULL;

	ctx = ubus_connect(ubus_socket);
	if (!ctx) {
		fprintf(stderr, "Failed to connect to ubus\n");
		return -1;
	} else {
            spin_log(LOG_DEBUG, "Connected to ubus\n");
        }

        fd_set_blocking(ctx->sock.fd, 0);
        mainloop_register("ubus", wf_ubus, NULL, ctx->sock.fd, 0);

	server_main();

	return 0;
}
