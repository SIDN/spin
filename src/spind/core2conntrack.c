#include <libmnl/libmnl.h>
#include <libnetfilter_conntrack/libnetfilter_conntrack.h>
#include <time.h>

#include "core2conntrack.h"
#include "spin_log.h"
#include "mainloop.h"
#include "spind.h"
#include "pkt_info.h"

#include "statistics.h"

STAT_MODULE(conntrack)

// define a structure for the callback data
typedef struct {
  flow_list_t* flow_list;
  node_cache_t* node_cache;
  int local_mode;
} cb_data_t;

// for now, we just use a static variable to keep the data
static cb_data_t* cb_data_g;

static inline u_int16_t get_u16_attr(struct nf_conntrack *ct, char ATTR) {
  return ntohs(nfct_get_attr_u16(ct, ATTR));
}

static inline u_int32_t get_u32_attr(struct nf_conntrack *ct, char ATTR) {
  return ntohl(nfct_get_attr_u32(ct, ATTR));
}

static inline u_int64_t get_u64_attr(struct nf_conntrack *ct, char ATTR) {
  return be64toh(nfct_get_attr_u64(ct, ATTR));
}

void nfct_to_pkt_info(pkt_info_t* pkt_info, struct nf_conntrack *ct) {
  u_int32_t tmp;
  STAT_COUNTER(ctr4, ipv4-to-pkt, STAT_TOTAL);
  STAT_COUNTER(ctr6, ipv6-to-pkt, STAT_TOTAL);

  pkt_info->family = nfct_get_attr_u8(ct, ATTR_ORIG_L3PROTO);
  switch (pkt_info->family) {
  case AF_INET:
    tmp = nfct_get_attr_u32(ct, ATTR_IPV4_SRC);
    memset(pkt_info->src_addr, 0, 12);
    memset(pkt_info->dest_addr, 0, 12);
    memcpy((pkt_info->src_addr) + 12, &tmp, 4);
    tmp = nfct_get_attr_u32(ct, ATTR_IPV4_DST);
    memcpy((pkt_info->dest_addr) + 12, &tmp, 4);
    pkt_info->src_port = get_u16_attr(ct, ATTR_ORIG_PORT_SRC);
    pkt_info->dest_port = get_u16_attr(ct, ATTR_ORIG_PORT_DST);
    pkt_info->payload_size = get_u64_attr(ct, ATTR_ORIG_COUNTER_BYTES);
    // We count both the orig and the repl for size and packet numbers
    pkt_info->payload_size = get_u64_attr(ct, ATTR_ORIG_COUNTER_BYTES);
    pkt_info->payload_size += get_u64_attr(ct, ATTR_REPL_COUNTER_BYTES);
    pkt_info->packet_count = get_u64_attr(ct, ATTR_ORIG_COUNTER_PACKETS);
    pkt_info->packet_count += get_u64_attr(ct, ATTR_REPL_COUNTER_PACKETS);
    pkt_info->payload_offset = 0;
    pkt_info->protocol = nfct_get_attr_u8(ct, ATTR_ORIG_L4PROTO);
    STAT_VALUE(ctr4, 1);

    break;
  case AF_INET6:
    memcpy((&pkt_info->src_addr[0]), nfct_get_attr(ct, ATTR_IPV6_SRC), 16);
    memcpy((&pkt_info->dest_addr[0]), nfct_get_attr(ct, ATTR_IPV6_DST), 16);
    pkt_info->packet_count = nfct_get_attr_u64(ct, ATTR_ORIG_COUNTER_PACKETS);
    pkt_info->payload_size = get_u64_attr(ct, ATTR_ORIG_COUNTER_BYTES);
    pkt_info->payload_size += get_u64_attr(ct, ATTR_REPL_COUNTER_BYTES);
    pkt_info->packet_count = nfct_get_attr_u64(ct, ATTR_ORIG_COUNTER_PACKETS);
    pkt_info->packet_count += nfct_get_attr_u64(ct, ATTR_REPL_COUNTER_PACKETS);
    pkt_info->payload_offset = 0;
    pkt_info->protocol = nfct_get_attr_u8(ct, ATTR_ORIG_L4PROTO);
    STAT_VALUE(ctr6, 1);
    break;
    // note: ipv6 is u128
  }
}

static int check_ignore_local(pkt_info_t* pkt, node_cache_t* node_cache) {
    ip_t ip;
    node_t* src_node;
    node_t* dest_node;
    STAT_COUNTER(ctr, ignore-local, STAT_TOTAL);
    int result;

    ip.family = pkt->family;
    memcpy(ip.addr, pkt->src_addr, 16);
    src_node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
    memcpy(ip.addr, pkt->dest_addr, 16);
    dest_node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
    result = (src_node == NULL || dest_node == NULL || (src_node->mac == NULL && dest_node->mac == NULL));
    STAT_VALUE(ctr, result);
    return result;
}

static int conntrack_cb(const struct nlmsghdr *nlh, void *data)
{
    struct nf_conntrack *ct;
    pkt_info_t pkt_info;
    cb_data_t* cb_data = (cb_data_t*) data;
    //flow_list_t* flow_list = cb_data->flow_list;
    // TODO: remove time() calls, use the single one at caller
    uint32_t now = time(NULL);
    STAT_COUNTER(ctr, callback, STAT_TOTAL);
    STAT_COUNTER(ctrsf, sendflow, STAT_TOTAL);
    STAT_COUNTER(ctrlocal, ignore-local, STAT_TOTAL);
    STAT_COUNTER(ctrignore, ignore-ip, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    maybe_sendflow(cb_data->flow_list, now);

    ct = nfct_new();
    if (ct == NULL) {
      return MNL_CB_OK;
    }

    nfct_nlmsg_parse(nlh, ct);

    // TODO: remove repl?
    nfct_to_pkt_info(&pkt_info, ct);

    if (pkt_info.packet_count > 0 || pkt_info.payload_size > 0) {
        node_cache_add_pkt_info(cb_data->node_cache, &pkt_info, now);

        // small experiment, try to ignore messages from and to
        // this device, unless local_mode is set
        if (!cb_data->local_mode && check_ignore_local(&pkt_info, cb_data->node_cache)) {
            nfct_destroy(ct);
            STAT_VALUE(ctrlocal, 1);
            return MNL_CB_OK;
        }
        // check for configured ignores as well
        // do we need to cache it or should we do this check earlier?
        // do we need to check both source and reply?
        if (addr_in_ignore_list(pkt_info.family, pkt_info.src_addr) ||
            addr_in_ignore_list(pkt_info.family, pkt_info.dest_addr)
           ) {
            nfct_destroy(ct);
            STAT_VALUE(ctrignore, 1);
            return MNL_CB_OK;
        }

        STAT_VALUE(ctrsf, 1);
        flow_list_add_pktinfo(cb_data->flow_list, &pkt_info);
    }

    nfct_destroy(ct);

    return MNL_CB_OK;
}

int do_read(cb_data_t* cb_data, int inet_family) {
    struct mnl_socket *nl;
    struct nlmsghdr *nlh;
    struct nfgenmsg *nfh;
    char buf[MNL_SOCKET_BUFFER_SIZE];
    unsigned int seq, portid;
    int ret;

    nl = mnl_socket_open(NETLINK_NETFILTER);
    if (nl == NULL) {
        perror("mnl_socket_open");
        exit(EXIT_FAILURE);
    }

    if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
        perror("mnl_socket_bind");
        exit(EXIT_FAILURE);
    }
    portid = mnl_socket_get_portid(nl);

    nlh = mnl_nlmsg_put_header(buf);
    nlh->nlmsg_type = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_GET_CTRZERO;
    nlh->nlmsg_flags = NLM_F_REQUEST|NLM_F_DUMP;
    nlh->nlmsg_seq = seq = time(NULL);

    nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
    //nfh->nfgen_family = inet_family;
    nfh->nfgen_family = AF_INET;
    nfh->version = NFNETLINK_V0;
    nfh->res_id = 0;

    ret = mnl_socket_sendto(nl, nlh, nlh->nlmsg_len);
    if (ret == -1) {
        perror("mnl_socket_recvfrom");
        exit(EXIT_FAILURE);
    }

    ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
    while (ret > 0) {
        ret = mnl_cb_run(buf, ret, seq, portid, conntrack_cb, cb_data);
        if (ret <= MNL_CB_STOP)
            break;
        ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
    }
    if (ret == -1) {
        perror("mnl_socket_recvfrom");
        exit(EXIT_FAILURE);
    }

    mnl_socket_close(nl);

    return 0;
}

static void core2conntrack_callback(void *arg, int data, int timeout) {
    spin_log(LOG_DEBUG, "core2conntrack callback\n");
    if (timeout) {
        do_read(cb_data_g, AF_INET);
        do_read(cb_data_g, AF_INET6);
    } else {
        spin_log(LOG_ERR, "core2conntrack_callback should only be called on callback\n");
        // maybe not exit, but hey
        exit(1);
    }
}

void init_core2conntrack(node_cache_t* node_cache, int local_mode) {
    cb_data_g = (cb_data_t*)malloc(sizeof(cb_data_t));
    cb_data_g->flow_list = flow_list_create(time(NULL));
    cb_data_g->node_cache = node_cache;
    cb_data_g->local_mode = local_mode;

    // Register in the main loop
    // In this case, we do not need a callback on data; we just want to be
    // called every timeout
    mainloop_register("core2conntrack", core2conntrack_callback, (void *) 0, 0, 1000);

    spin_log(LOG_DEBUG, "core2conntrack initialized\n");
}

void cleanup_core2conntrack() {
    flow_list_destroy(cb_data_g->flow_list);
    free(cb_data_g);
}
