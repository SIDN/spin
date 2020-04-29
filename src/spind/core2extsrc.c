
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>

#include "dnshooks.h"
#include "extsrc.h"
#include "mainloop.h"
#include "spind.h"
#include "spin_log.h"


static node_cache_t *node_cache;
static dns_cache_t *dns_cache;
static char *extsrc_socket_path;

static flow_list_t *flow_list;

static int fd;

/* #define EXTSRC_DEBUG */

/*
 * Note: when processing a message received from the socket, it is important
 * to:
 * (1) verify that the payload has the expected size. This is done in
 * wf_extsrc().
 * (2) sanitize input. For instance, in the case of a string, make sure it is
 * NUL-terminated. This is done in each of the process_*() functions.
 */

static void
process_pkt_info(pkt_info_t *pkt_info)
{
    time_t now;

    /*
     * Sanitize input: not necessary.
     */

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

    if (pkt_info->packet_count > 0 || pkt_info->payload_size > 0) {
        node_cache_add_pkt_info(node_cache, pkt_info, now);

        flow_list_add_pktinfo(flow_list, pkt_info);
    }
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
    node_t *node;
#ifdef EXTSRC_DEBUG
    char ipstr[INET6_ADDRSTRLEN];
#endif
    time_t now;

    /*
     * Sanitize input.
     */
    up->mac[sizeof(up->mac) - 1] = 0;

    now = time(NULL);

    arp_table_add_ip_t(node_cache->arp_table, &up->ip, up->mac);

    node = node_cache_find_by_mac(node_cache, up->mac);
    if (node == NULL) {
        node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &up->ip);
        if (node == NULL) {
            node = node_create(0);
            if (node == NULL) {
                spin_log(LOG_ERR, "node_create failed\n");
                exit(1);
            }

            node_cache_add_node(node_cache, node);
        }

        node_set_mac(node, up->mac);

        spin_log(LOG_DEBUG, "core2extsrc: new MAC address: %s\n", up->mac);
    }

    node_add_ip(node, &up->ip);
    node_set_modified(node, now);

#ifdef EXTSRC_DEBUG
    spin_ntop(ipstr, &up->ip, sizeof(ipstr));
    spin_log(LOG_DEBUG, "core2extsrc: IP %s for MAC address %s\n", ipstr,
        up->mac);
#endif
}

static void
wf_extsrc(void *arg, int data, int timeout)
{
    struct extsrc_msg_hdr hdr;
    void *msg = NULL;

    spin_log(LOG_DEBUG, "wf_extsrc\n");

    if (recv(fd, &hdr, sizeof(hdr), MSG_PEEK) != sizeof(hdr)) {
        // XXX interesting errno?
        spin_log(LOG_WARNING, "%s: recv: short read\n", __func__);
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
        // XXX interesting errno?
        spin_log(LOG_WARNING, "%s: read: short read\n", __func__);
        goto fail;
    }

    switch (hdr.type) {
    case EXTSRC_MSG_TYPE_PKT_INFO:
        if (len != sizeof(struct extsrc_msg_hdr) + sizeof(pkt_info_t)) {
            spin_log(LOG_WARNING, "%s: incorrect message size\n", __func__);
            goto fail;
        }

        process_pkt_info((pkt_info_t *)(msg + sizeof(struct extsrc_msg_hdr)));
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

static void
removesocket(void)
{
    remove(extsrc_socket_path);
}

void
init_core2extsrc(node_cache_t *nc, dns_cache_t *dc, char *sp)
{
    struct sockaddr_un s_un;
    mode_t old_umask;

    spin_log(LOG_DEBUG, "registering external source\n");

    node_cache = nc;
    dns_cache = dc;
    extsrc_socket_path = sp;

    // XXX does it matter what timestamp we use? No idea.
    flow_list = flow_list_create(time(NULL));

    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        spin_log(LOG_ERR, "socket: %s\n", strerror(errno));
        exit(1);
    }

    s_un.sun_family = AF_UNIX;
    if (snprintf(s_un.sun_path, sizeof(s_un.sun_path), "%s",
        extsrc_socket_path) >= (ssize_t)sizeof(s_un.sun_path)) {
        spin_log(LOG_ERR, "%s: socket path too long\n", extsrc_socket_path);
        exit(1);
    }

    remove(extsrc_socket_path);

    old_umask = umask(077);
    if (bind(fd, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
        spin_log(LOG_ERR, "bind: %s: %s\n", extsrc_socket_path,
            strerror(errno));
        exit(1);
    }
    umask(old_umask);
    if (chmod(extsrc_socket_path, 0600) == -1) {
        spin_log(LOG_ERR, "chmod: %s: %s\n", extsrc_socket_path,
            strerror(errno));
        exit(1);
    }

    atexit(removesocket);

    mainloop_register("external-source", wf_extsrc, (void *) 0, fd, 0);

    spin_log(LOG_DEBUG, "registered external source\n");
}

void
cleanup_core2extsrc()
{
    // XXX should we close the socket here?
    flow_list_destroy(flow_list);
}
