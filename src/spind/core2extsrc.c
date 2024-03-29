
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netdb.h>
#include <time.h>

#include "core2extsrc.h"
#include "dnshooks.h"
#include "extsrc.h"
#include "mainloop.h"
#include "process_pkt_info.h"
#include "spind.h"
#include "spin_log.h"


static node_cache_t *node_cache;
static dns_cache_t *dns_cache;
static trafficfunc traffic_hook;
static char *extsrc_socket_path;

static flow_list_t *flow_list;

struct wf_extsrc_arg {
    int fd;
};

#define EXTSRC_MAX 1024 /* XXX */

/* #define EXTSRC_DEBUG */

/*
 * If not using UNIX domain sockets, use TCP for communication between
 * an extsrc client and spind if defined; UDP otherwise.
 */
#define EXTSRC_TCP

/*
 * Note: when processing a message received from the socket, it is important
 * to:
 * (1) verify that the payload has the expected size. This is done in
 * wf_extsrc().
 * (2) sanitize input. For instance, in the case of a string, make sure it is
 * NUL-terminated. This is done in each of the process_*() functions.
 */

static void
process_pkt_info_extsrc(pkt_info_t *pkt_info)
{
    time_t now;

    /*
     * Sanitize input: not necessary.
     */

     /*
      * XXX
      */
    pkt_info->family = extsrc_af_from_wire(pkt_info->family);

    /*
     * We could potentially send a timestamp through the socket and use
     * that here, too.
     */
    now = time(NULL);

    /*
     * Here we mirror what conntrack_cb() is doing with the flow_list and
     * the pkt_info_t. Probably would be a good idea to refactor that bit
     * of code so we don't have to duplicate it here.
     */
    maybe_sendflow(flow_list, now);

    // XXX: we should probably use the local_mode flag instead of passing 1
    process_pkt_info(node_cache, flow_list, traffic_hook, 1, pkt_info);
}

static void
process_dns_query(struct extsrc_dns_query_hdr *hdr, dns_pkt_info_t *dns_pkt)
{
    /*
     * Sanitize input: XXX what about the dname field?
     */

    dns_query_hook(dns_pkt, hdr->family, hdr->src_addr);
}

static void
process_dns_answer(dns_pkt_info_t *dns_pkt)
{
    /*
     * Sanitize input: XXX what about the dname field?
     */

    dns_answer_hook(dns_pkt);
}

static void
process_device_info(struct extsrc_arp_table_update *up)
{
#ifdef EXTSRC_DEBUG
    char ipstr[INET6_ADDRSTRLEN];
#endif

    /*
     * Sanitize input.
     */
    up->mac[sizeof(up->mac) - 1] = 0;

    arp_table_add(node_cache->arp_table, &up->ip, up->mac);

#ifdef EXTSRC_DEBUG
    spin_ntop(ipstr, &up->ip, sizeof(ipstr));
    spin_log(LOG_DEBUG, "core2extsrc: IP %s for MAC address %s\n", ipstr,
        up->mac);
    spin_log(LOG_DEBUG, "Size of ARP table: %d\n",
        arp_table_size(node_cache->arp_table));
#endif
}

static void
wf_extsrc(void *arg, int data, int timeout)
{
    struct extsrc_msg_hdr hdr;
    void *msg = NULL;
    int fd = ((struct wf_extsrc_arg *)arg)->fd;

    spin_log(LOG_DEBUG, "wf_extsrc\n");

    if (recv(fd, &hdr, sizeof(hdr), MSG_PEEK) != sizeof(hdr)) {
        spin_log(LOG_WARNING, "%s: recv: %s\n", __func__, strerror(errno));
        goto fail;
    }

    if (hdr.length == 0 || hdr.length > EXTSRC_MAX) {
        spin_log(LOG_WARNING, "%s: hdr.length %d invalid\n", __func__,
            hdr.length);
        goto fail;
    }
    ssize_t len = sizeof(hdr) + hdr.length;
    msg = malloc(len);
    if (msg == NULL) {
        /*
         * Usually I would shut down the program when a malloc failure
         * occurs but in this case, let's continue. This failure can be
         * dealt with in a contained fashion (just close the socket and
         * move on).
         */
        spin_log(LOG_WARNING, "%s: malloc: %s\n", __func__, strerror(errno));
        goto fail;
    }

    if (read(fd, msg, len) != len) {
        spin_log(LOG_WARNING, "%s: read: %s\n", __func__, strerror(errno));
        goto fail;
    }

    switch (hdr.type) {
    case EXTSRC_MSG_TYPE_PKT_INFO:
        if (len != sizeof(struct extsrc_msg_hdr) + sizeof(pkt_info_t)) {
            spin_log(LOG_WARNING, "%s: incorrect message size\n", __func__);
            goto fail;
        }

        process_pkt_info_extsrc((pkt_info_t *)
	    (msg + sizeof(struct extsrc_msg_hdr)));
        break;

    case EXTSRC_MSG_TYPE_DNS_QUERY:
        if (len != sizeof(struct extsrc_msg_hdr) +
            sizeof(struct extsrc_dns_query_hdr) + sizeof(dns_pkt_info_t)) {
            spin_log(LOG_WARNING, "%s: incorrect message size\n", __func__);
            goto fail;
        }

        process_dns_query((struct extsrc_dns_query_hdr *)
            (msg + sizeof(struct extsrc_msg_hdr)),
            (dns_pkt_info_t *)(msg + sizeof(struct extsrc_msg_hdr) +
            sizeof(struct extsrc_dns_query_hdr)));
        break;

    case EXTSRC_MSG_TYPE_DNS_ANSWER:
        if (len != sizeof(struct extsrc_msg_hdr) + sizeof(dns_pkt_info_t)) {
            spin_log(LOG_WARNING, "%s: incorrect message size\n", __func__);
            goto fail;
        }

        process_dns_answer((dns_pkt_info_t *)(msg +
            sizeof(struct extsrc_msg_hdr)));
        break;

    case EXTSRC_MSG_TYPE_ARP_TABLE_UPDATE:
        if (len != sizeof(struct extsrc_msg_hdr) +
	    sizeof(struct extsrc_arp_table_update)) {
            spin_log(LOG_WARNING, "%s: incorrect message size\n", __func__);
            goto fail;
        }

        process_device_info((struct extsrc_arp_table_update *)(msg +
            sizeof(struct extsrc_msg_hdr)));
        break;

    default:
        spin_log(LOG_WARNING, "%s: unknown message type\n", __func__);
        goto fail;
    }

    goto out;

fail:
    /*
     * When a client sends something we did not expect, we ruin the party
     * for everybody and close the socket. At least for now.
     */
    spin_log(LOG_WARNING, "closing core2extsrc fd\n");
    close(fd);
    fd = fd * -1; // XXX or another way to make mainloop stop using this fd

out:
    free(msg);
}

#ifdef EXTSRC_TCP
static void
wf_extsrc_accept(void *arg, int data, int timeout)
{
    struct wf_extsrc_arg *wf_arg = NULL;
    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    int fd = ((struct wf_extsrc_arg *)arg)->fd;

    int cfd = accept(fd, &addr, &addrlen);
    if (cfd == -1) {
        spin_log(LOG_WARNING, "accept: %s\n", strerror(errno));
        goto bad;
    }

    wf_arg = malloc(sizeof(struct wf_extsrc_arg));
    if (!wf_arg) {
        spin_log(LOG_WARNING, "malloc: %s", strerror(errno));
        goto bad;
    }
    wf_arg->fd = cfd;

    if (mainloop_register("external-source-tcp", wf_extsrc, (void *) wf_arg, cfd,
        0, 0) == 1) {
        spin_log(LOG_WARNING, "mainloop_register returned failure\n");
        goto bad;
    }

    return;

 bad:
    close(cfd);
    free(wf_arg);
    spin_log(LOG_WARNING, "%s: can't serve extsrc client\n", __func__);
}
#endif /* EXTSRC_TCP */

static void
removesocket(void)
{
    remove(extsrc_socket_path);
}

static int
socket_open_inet(const char *addr)
{
    struct wf_extsrc_arg *wf_arg;
    struct addrinfo hints, *res;
    char port[sizeof("65535")];
    int error;
    int fd;
    int opt = 1;

    spin_log(LOG_DEBUG, "registering external source (%s)\n", addr);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
#ifdef EXTSRC_TCP
    hints.ai_socktype = SOCK_STREAM;
#else
    hints.ai_socktype = SOCK_DGRAM;
#endif
    snprintf(port, sizeof(port), "%d", EXTSRC_PORT);
    /*
     * XXX should we loop through the results? don't want to spend too many
     * fds.
     */
    error = getaddrinfo(addr, port, &hints, &res);
    if (error) {
        spin_log(LOG_ERR, "getaddrinfo: %s", gai_strerror(error));
        return 1;
    }

    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == -1) {
        spin_log(LOG_ERR, "socket: %s\n", strerror(errno));
        return 1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        spin_log(LOG_ERR, "setsockopt SO_REUSEADDR: %s\n", strerror(errno));
        return 1;
    }

    if (bind(fd, res->ai_addr, res->ai_addrlen) == -1) {
        spin_log(LOG_ERR, "bind: %s\n", strerror(errno));
        return 1;
    }

    freeaddrinfo(res);

#ifdef EXTSRC_TCP
    if (listen(fd, 2) == -1) {
        spin_log(LOG_ERR, "listen: %s\n", strerror(errno));
        return 1;
    }
#endif /* EXTSRC_TCP */

    // XXX is currently never freed
    wf_arg = malloc(sizeof(struct wf_extsrc_arg));
    if (!wf_arg) {
        spin_log(LOG_ERR, "malloc: %s", strerror(errno));
        return 1;
    }
    wf_arg->fd = fd;

#ifdef EXTSRC_TCP
    mainloop_register("external-source", wf_extsrc_accept, (void *) wf_arg, fd,
        0, 1);
#else
    mainloop_register("external-source", wf_extsrc, (void *) wf_arg, fd, 0, 1);
#endif

    spin_log(LOG_INFO, "extsrc: listening on [%s]:%d\n", addr, EXTSRC_PORT);
    return 0;
}

static int
socket_open_unix()
{
    struct wf_extsrc_arg *wf_arg;
    struct sockaddr_un s_un;
    mode_t old_umask;
    int fd;

    spin_log(LOG_DEBUG, "registering external source (%s)\n",
        extsrc_socket_path);

    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        spin_log(LOG_ERR, "socket: %s\n", strerror(errno));
        return 1;
    }

    memset(&s_un, 0, sizeof(s_un));
    s_un.sun_family = AF_UNIX;
    if (snprintf(s_un.sun_path, sizeof(s_un.sun_path), "%s",
        extsrc_socket_path) >= (ssize_t)sizeof(s_un.sun_path)) {
        spin_log(LOG_ERR, "%s: socket path too long\n", extsrc_socket_path);
        return 1;
    }

    remove(extsrc_socket_path);

    old_umask = umask(077);
    if (bind(fd, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
        spin_log(LOG_ERR, "bind: %s: %s\n", extsrc_socket_path,
            strerror(errno));
        return 1;
    }
    umask(old_umask);
    if (chmod(extsrc_socket_path, 0600) == -1) {
        spin_log(LOG_ERR, "chmod: %s: %s\n", extsrc_socket_path,
            strerror(errno));
        return 1;
    }

    atexit(removesocket);

    // XXX is currently never freed
    wf_arg = malloc(sizeof(struct wf_extsrc_arg));
    if (!wf_arg) {
        spin_log(LOG_ERR, "malloc: %s", strerror(errno));
        return 1;
    }
    wf_arg->fd = fd;

    mainloop_register("external-source", wf_extsrc, (void *) wf_arg, fd, 0, 1);

    return 0;
}

int
init_core2extsrc(node_cache_t *nc, dns_cache_t *dc, trafficfunc th, char *sp, char *la)
{
    node_cache = nc;
    dns_cache = dc;
    traffic_hook = th;

    if (sp) {
        extsrc_socket_path = sp;
    } else {
        extsrc_socket_path = EXTSRC_SOCKET_PATH;
    }

    // XXX does it matter what timestamp we use? No idea.
    flow_list = flow_list_create(time(NULL));

    if (la) {
        if (socket_open_inet(la)) {
            spin_log(LOG_ERR, "socket_open_inet() failed\n");
            return 1;
        }
    } else {
        if (socket_open_unix()) {
            spin_log(LOG_ERR, "socket_open_unix() failed\n");
            return 1;
        }
    }

    spin_log(LOG_DEBUG, "registered external source\n");
    return 0;
}

void
cleanup_core2extsrc()
{
    // XXX should we close the socket here?
    flow_list_destroy(flow_list);
}
