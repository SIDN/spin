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

char *count_to_number(uint32_t num)
{
	uint32_t ptr = 0, size = 0;
	uint32_t written = 0, i;
	int new_line_every_n_numbers = 30;
	char *s;

	for (i=0; i < num; ++i) {
		size += snprintf(NULL, 0, "%u ", i);
		if (i > 0 && i % new_line_every_n_numbers == 0)
			size++;
	}
	size++; /* one for null char */

	s = calloc(size, sizeof(char));
	if (!s)
		goto out;

	for (i=0; i < num; ++i) {
		written = sprintf(&s[ptr], "%u ", i);
		ptr  += written;
		if (i > 0 && i % new_line_every_n_numbers == 0) {
			sprintf(&s[ptr], "\n");
			ptr++;
		}
	}

out:
	return s;
}
static struct ubus_context *ctx;
static struct ubus_subscriber spin_event;
static struct blob_buf b;

enum {
	HELLO_ID,
	HELLO_MSG,
	__HELLO_MAX
};

static const struct blobmsg_policy hello_policy[] = {
	[HELLO_ID] = { .name = "id", .type = BLOBMSG_TYPE_INT32 },
	[HELLO_MSG] = { .name = "msg", .type = BLOBMSG_TYPE_STRING },
};

struct hello_request {
	struct ubus_request_data req;
	struct uloop_timeout timeout;
	int fd;
	int idx;
	char data[];
};

#undef WIERD

#ifdef WIERD
static void spin_hello_fd_reply(struct uloop_timeout *t)
{
	struct hello_request *req = container_of(t, struct hello_request, timeout);
	char *data;

	data = alloca(strlen(req->data) + 32);
	sprintf(data, "msg%d: %s\n", ++req->idx, req->data);
	if (write(req->fd, data, strlen(data)) < 0) {
		close(req->fd);
		free(req);
		return;
	}

	uloop_timeout_set(&req->timeout, 1000);
}

static void spin_hello_reply(struct uloop_timeout *t)
{
	struct hello_request *req = container_of(t, struct hello_request, timeout);
	int fds[2];

	blob_buf_init(&b, 0);
	blobmsg_add_string(&b, "message", req->data);
	ubus_send_reply(ctx, &req->req, b.head);

	if (pipe(fds) == -1) {
		fprintf(stderr, "Failed to create pipe\n");
		return;
	}
	ubus_request_set_fd(ctx, &req->req, fds[0]);
	ubus_complete_deferred_request(ctx, &req->req, 0);
	req->fd = fds[1];

	req->timeout.cb = spin_hello_fd_reply;
	spin_hello_fd_reply(t);
}
#endif

static int spin_hello(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct hello_request *hreq;
	struct blob_attr *tb[__HELLO_MAX];
	const char *format = "%s received a message: %d %s";
	const char *msgstr = "(unknown)";
	int num=0;

	blobmsg_parse(hello_policy, ARRAY_SIZE(hello_policy), tb, blob_data(msg), blob_len(msg));

	if (tb[HELLO_ID])
		num = blobmsg_get_u32(tb[HELLO_ID]);
	if (tb[HELLO_MSG])
		msgstr = blobmsg_data(tb[HELLO_MSG]);

        fprintf(stderr, "Hello(%d, %s) called\n", num, msgstr);
	hreq = calloc(1, sizeof(*hreq) + strlen(format) + strlen(obj->name) + 20 + strlen(msgstr) + 1);
	if (!hreq)
		return UBUS_STATUS_UNKNOWN_ERROR;

	sprintf(hreq->data, format, obj->name, num, msgstr);

#ifdef WIERD
	ubus_defer_request(ctx, req, &hreq->req);
	hreq->timeout.cb = spin_hello_reply;
	uloop_timeout_set(&hreq->timeout, 1000);
#else
	blob_buf_init(&b, 0);
	blobmsg_add_string(&b, "message", hreq->data);
	ubus_send_reply(ctx, req, b.head);
#endif
	return 0;
}

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

struct spindlist_request {
	struct ubus_request_data req;
	struct uloop_timeout timeout;
	int fd;
	int idx;
	char data[];
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
	SPINBLOCKFLOW_NODE1,
	SPINBLOCKFLOW_NODE2,
	__SPINBLOCKFLOW_MAX
};

static const struct blobmsg_policy spinblockflow_policy[] = {
	[SPINBLOCKFLOW_NODE1] = { .name = "node1", .type = BLOBMSG_TYPE_INT32 },
	[SPINBLOCKFLOW_NODE2] = { .name = "node2", .type = BLOBMSG_TYPE_INT32 },
};

struct spinblockflow_request {
	struct ubus_request_data req;
	struct uloop_timeout timeout;
	int fd;
	int idx;
	char data[];
};

static int spin_spinblockflow(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct blob_attr *tb[__SPINBLOCKFLOW_MAX];
        int node1, node2;
        int result;

	blobmsg_parse(spinblockflow_policy, ARRAY_SIZE(spinblockflow_policy), tb, blob_data(msg), blob_len(msg));

        node1 = 0; node2 = 0;
	if (tb[SPINBLOCKFLOW_NODE1])
		node1 = blobmsg_get_u32(tb[SPINBLOCKFLOW_NODE1]);
	if (tb[SPINBLOCKFLOW_NODE2])
		node2 = blobmsg_get_u32(tb[SPINBLOCKFLOW_NODE2]);

        fprintf(stderr, "spinblockflow(%d, %d) called\n", node1, node2);

        result = spinrpc_flowblock(node1, node2);

	blob_buf_init(&b, 0);
	blobmsg_add_u32(&b, "return", result);
	ubus_send_reply(ctx, req, b.head);
	return 0;
}


static const struct ubus_method spin_methods[] = {
	UBUS_METHOD("hello", spin_hello, hello_policy),
	UBUS_METHOD("spindlist", spin_spindlist, spindlist_policy),
	UBUS_METHOD("spinblockflow", spin_spinblockflow, spinblockflow_policy),
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

#ifdef notdef
	uloop_run();
#endif
}

int ubus_main()
{
	const char *ubus_socket = NULL;

#ifdef notdef
	uloop_init();
	signal(SIGPIPE, SIG_IGN);
#endif

	ctx = ubus_connect(ubus_socket);
	if (!ctx) {
		fprintf(stderr, "Failed to connect to ubus\n");
		return -1;
	} else {
            spin_log(LOG_DEBUG, "Connected to ubus\n");
        }

#ifdef notdef
	ubus_add_uloop(ctx);
#endif
        fd_set_blocking(ctx->sock.fd, 0);
        mainloop_register("ubus", wf_ubus, NULL, ctx->sock.fd, 0);

	server_main();

#ifdef notdef
	ubus_free(ctx);
	uloop_done();
#endif

	return 0;
}
