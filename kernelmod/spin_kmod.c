//'Hello World' netfilter hooks example
//For any packet, we drop it, and log fact to /var/log/messages

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/version.h>

#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/inet.h>
#include <linux/time.h>

#include <linux/errno.h>

#include <net/net_namespace.h>

#include "messaging.h"
#include "pkt_info.h"
#include "pkt_info_list.h"
#include "spin_util.h"
#include "spin_cfg.h"

#include "nf_hook_def.h"

#include "spin_traffic_send_thread.h"

//#include <arpa/inet.h>

// kernel module examples from http://www.paulkiddie.com/2009/11/creating-a-netfilter-kernel-module-which-filters-udp-packets/
// netlink examples from https://gist.github.com/arunk-s/c897bb9d75a6c98733d6

// Module parameters
static char* mode = "forward";
module_param(mode, charp, 0000);
MODULE_PARM_DESC(mode, "Run mode (local or forward, defaults to forward)");

// printk has its own verbosity but we need some more granularity
static int verbosity = 0;
module_param(verbosity, int, 0000);
MODULE_PARM_DESC(verbosity, "Logging verbosity (0 = silent, 5 = debug)");

// Hooks for packet capture
static struct nf_hook_ops nfho1;
static struct nf_hook_ops nfho2;
static struct nf_hook_ops nfho3;
static struct nf_hook_ops nfho4;


struct tcphdr *tcp_header;
struct iphdr *ip_header;            //ip header struct
struct ipv6hdr *ipv6_header;            //ip header struct

ip_store_t* ignore_ips;
ip_store_t* block_ips;
ip_store_t* except_ips;

struct sock *traffic_nl_sk = NULL;
struct sock *config_nl_sk = NULL;

//handler_info_t* handler_info;
traffic_clients_t* traffic_clients;

#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>

// structures for the timer to clear the pkt_info_list when there was no
// new traffic
static struct hrtimer htimer;
static ktime_t kt_periode;



// storage of pkt_infos so we do not have to send every single packet
// (this should solve a lot when we have large streams)
pkt_info_list_t* pkt_info_list = NULL;

#define NETLINK_CONFIG_PORT 30
#define NETLINK_TRAFFIC_PORT 31

int parse_ipv6_packet(struct sk_buff* sockbuff, pkt_info_t* pkt_info) {
    //uint8_t* b;
    struct udphdr *udp_header;

    ipv6_header = (struct ipv6hdr *)ipv6_hdr(sockbuff);

    // hard-code a number of things to ignore; such as broadcasts (for now)
    /*
    b = (uint8_t*)(&ipv6_header->daddr);
    if (*b == 255) {
        return -1;
    }*/

    if (ipv6_header->nexthdr == 17) {
        udp_header = (struct udphdr *)skb_transport_header(sockbuff);
        pkt_info->src_port = ntohs(udp_header->source);
        pkt_info->dest_port = ntohs(udp_header->dest);
        pkt_info->payload_size = (uint32_t)ntohs(udp_header->len) - 8;
        pkt_info->payload_offset = skb_network_header_len(sockbuff) + 8;
    } else if (ipv6_header->nexthdr == 6) {
        tcp_header = (struct tcphdr *)skb_transport_header(sockbuff);
        pkt_info->src_port = ntohs(tcp_header->source);
        pkt_info->dest_port = ntohs(tcp_header->dest);
        pkt_info->payload_size = (uint32_t)sockbuff->len - skb_network_header_len(sockbuff) - (4*tcp_header->doff);
        if (pkt_info->payload_size > 2) {
            pkt_info->payload_size = pkt_info->payload_size - 2;
            pkt_info->payload_offset = skb_network_header_len(sockbuff) + (4*tcp_header->doff) + 2;
        } else {
            // if size is zero, ignore tcp packet
            printv(5, KERN_DEBUG "Zero length TCP packet, ignoring\n");
            pkt_info->payload_size = 0;
            //pkt_info->payload_offset = skb_network_header_len(sockbuff) + (4*tcp_header->doff) + 2;
            pkt_info->payload_offset = 0;
        }
    } else if (ipv6_header->nexthdr != 58) {
        if (ipv6_header->nexthdr == 0) {
            // ignore hop-by-hop option header
            return 1;
        } else if (ipv6_header->nexthdr == 44) {
            // what to do with fragments?
            return 1;
        }
        printv(3, KERN_DEBUG "unsupported IPv6 next header: %u\n", ipv6_header->nexthdr);
        return -1;
    } else {
        pkt_info->payload_size = (uint32_t)sockbuff->len - skb_network_header_len(sockbuff);
        pkt_info->payload_offset = skb_network_header_len(sockbuff);
    }

    // rest of basic info
    pkt_info->packet_count = 1;
    pkt_info->family = AF_INET6;
    pkt_info->protocol = ipv6_header->nexthdr;
    memcpy(pkt_info->src_addr, &ipv6_header->saddr, 16);
    memcpy(pkt_info->dest_addr, &ipv6_header->daddr, 16);
    return 0;
}
// Parse packet into pkt_info structure
// Return values:
// 0: all ok, structure filled
// 1: zero-size packet, structure not filled
// -1: error
int parse_packet(struct sk_buff* sockbuff, pkt_info_t* pkt_info) {
    //uint8_t* b;
    struct udphdr *udp_header;

    ip_header = (struct iphdr *)skb_network_header(sockbuff);
    if (ip_header->version == 6) {
        return parse_ipv6_packet(sockbuff, pkt_info);
    }

    // hard-code a number of things to ignore; such as broadcasts (for now)
    // TODO: this should be based on netmask...
    /*
    b = (uint8_t*)(&ip_header->daddr);
    if (*b == 255 ||
        *b == 224 ||
        *b == 239) {
        return -1;
    }*/

    if (ip_header->protocol == 17) {
        udp_header = (struct udphdr *)skb_transport_header(sockbuff);
        pkt_info->src_port = ntohs(udp_header->source);
        pkt_info->dest_port = ntohs(udp_header->dest);
        pkt_info->payload_size = (uint32_t)ntohs(udp_header->len) - 8;
        pkt_info->payload_offset = skb_network_header_len(sockbuff) + 8;
    } else if (ip_header->protocol == 6) {
        tcp_header = (struct tcphdr*)((char*)ip_header + (ip_header->ihl * 4));
        pkt_info->src_port = ntohs(tcp_header->source);
        pkt_info->dest_port = ntohs(tcp_header->dest);
        pkt_info->payload_size = (uint32_t)sockbuff->len - skb_network_header_len(sockbuff) - (4*tcp_header->doff);
        if (pkt_info->payload_size > 2) {
            pkt_info->payload_size = pkt_info->payload_size - 2;
            //pkt_info->payload_offset = skb_network_header_len(sockbuff) + (4*tcp_header->doff) + 2;
            pkt_info->payload_offset = 0;
        } else {
            // if size is zero, ignore tcp packet
            printv(5, KERN_DEBUG "Payload size: %u\n", pkt_info->payload_size);
            pkt_info->payload_size = 0;
            pkt_info->payload_offset = skb_network_header_len(sockbuff) + (4*tcp_header->doff) + 2;
        }
    /* ignore some protocols */
    // TODO: de-capsulate encapsulated ipv6?
    } else if (ip_header->protocol != 1 &&
               ip_header->protocol != 2 &&
               ip_header->protocol != 41
              ) {
        printv(2, KERN_WARNING "unsupported IPv4 protocol: %u\n", ip_header->protocol);
        return -1;
    } else {
        pkt_info->payload_size = (uint32_t)sockbuff->len - skb_network_header_len(sockbuff);
        pkt_info->payload_offset = skb_network_header_len(sockbuff);
    }

    // rest of basic info
    pkt_info->packet_count = 1;
    pkt_info->family = AF_INET;
    pkt_info->protocol = ip_header->protocol;
    memset(pkt_info->src_addr, 0, 12);
    memcpy(pkt_info->src_addr + 12, &ip_header->saddr, 4);
    memset(pkt_info->dest_addr, 0, 12);
    memcpy(pkt_info->dest_addr + 12, &ip_header->daddr, 4);
    return 0;
}

/*int send_netlink_message(int msg_size, void* msg_data, uint32_t client_port_id) {
    traffic_clients_send(traffic_clients, msg_size, msg_data, client
    send_queue_add(handler_info->send_queue, msg_size, msg_data, client_port_id);
    return 1;
}*/

#define PACKET_SIZE 800
// TODO: refactor two functions below
void send_pkt_info(message_type_t type, pkt_info_t* pkt_info) {
    int msg_size;
    unsigned char data[PACKET_SIZE];
    //int i;
    //uint32_t port_id;

    // Nobody's listening, carry on
    if (traffic_clients_count(traffic_clients) == 0) {
        return;
    }

    // Check ignore list
    if (ip_store_contains_ip(ignore_ips, pkt_info->src_addr) ||
        ip_store_contains_ip(ignore_ips, pkt_info->dest_addr)) {
        return;
    }

    msg_size = pktinfo_msg_size();

    if (msg_size > PACKET_SIZE) {
        printv(2, KERN_WARNING "message too large, skipping\n");
        return;
    }

    pktinfo_msg2wire(type, data, pkt_info);

    traffic_clients_send(traffic_clients, msg_size, data);
}

void send_dns_pkt_info(message_type_t type, dns_pkt_info_t* dns_pkt_info) {
    int msg_size;
    //int res;
    unsigned char data[PACKET_SIZE];
    //int i;
    //uint32_t port_id;

    // Nobody's listening, carry on
    if (traffic_clients_count(traffic_clients) == 0) {
        return;
    }

    msg_size = dns_pktinfo_msg_size();
    if (msg_size > PACKET_SIZE) {
        printv(2, KERN_WARNING "message too large, skipping\n");
        return;
    }

    dns_pktinfo_msg2wire(type, data, dns_pkt_info);

    traffic_clients_send(traffic_clients, msg_size, data);
}

static inline uint16_t read_int16(uint8_t* data) {
    return (256*(uint8_t)data[0]) + (uint8_t)data[1];
}

unsigned int skip_dname(uint8_t* data, unsigned int cur_pos, size_t payload_size) {
    uint8_t labellen;
    if (cur_pos + 1 > payload_size) {
        printv(2, KERN_WARNING "unexpected end of payload when trying to read label\n");
        return -1;
    }
    labellen = data[cur_pos++];
    while (labellen > 0) {
        if ((labellen & 0xc0) == 0xc0) {
            // compressed, just skip it
            return ++cur_pos;
        }
        if (cur_pos + labellen > payload_size) {
            printv(2, KERN_WARNING "label len (%u at %u) past payload (%u)\n", (unsigned int)labellen, (unsigned int)cur_pos - 1, (unsigned int)payload_size);
            return -1;
        }
        cur_pos += labellen;
        labellen = data[cur_pos++];
    }
    return cur_pos;
}

static inline void printv_pkt_info(int module_verbosity, const char* msg, pkt_info_t* pkt_info) {
    if (log_get_verbosity() >= module_verbosity) {
        char p[1024];
        pktinfo2str(p, pkt_info, 1024);
        printv(module_verbosity, KERN_DEBUG "%s: %s\n", msg, p);
    }
}

void handle_dns_query(pkt_info_t* pkt_info, struct sk_buff* skb) {
    uint16_t offset = pkt_info->payload_offset;
    uint8_t flag_bits;
    uint8_t flag_bits2;
    uint16_t cur_pos;
    uint16_t cur_pos_name;
    uint8_t labellen;
    unsigned char dnsname[256];
    uint8_t* data = (uint8_t*)skb->data + pkt_info->payload_offset;
    size_t payload_size;
    dns_pkt_info_t dpkt_info;

    if (offset > skb->len) {
        return;
    } else {
        payload_size = skb->len - offset;
    }
    //hexdump_k(skb->data, pkt_info->payload_offset, pkt_info->payload_size);
    // check data size as well
    if (payload_size < 12) {
        return;
    }
    flag_bits = data[2];
    flag_bits2 = data[3];
    // must be qr=0
    if ((flag_bits & 0x80)) {
        return;
    }
    if (!(flag_bits2 & 0x0f) == 0) {
        return;
    }
    // check if there is a query message
    if (read_int16(data + 4) != 1) {
        return;
    }

    cur_pos = 12;
    cur_pos_name = 0;
    labellen = data[cur_pos++];
    while(labellen > 0) {
        if (cur_pos + labellen > payload_size) {
            printv(2, KERN_WARNING "Error: label len larger than packet payload\n");
            return;
        }
        if (cur_pos_name + labellen > 255) {
            printv(2, KERN_WARNING "Error: domain name over 255 octets\n");
            return;
        }
        dnsname[cur_pos_name++] = labellen;
        memcpy(dnsname + cur_pos_name, data + cur_pos, labellen);
        cur_pos += labellen;
        cur_pos_name += labellen;
        labellen = data[cur_pos++];
    }
    dnsname[cur_pos_name] = '\0';
    strncpy(dpkt_info.dname, dnsname, 256);

    dpkt_info.ttl = 0;

    memcpy(dpkt_info.ip, pkt_info->src_addr, 16);
    dpkt_info.family = pkt_info->family;

    // Don't send the info if the sender of the query is on the
    // ignore list
    if (!ip_store_contains_ip(ignore_ips, dpkt_info.ip)) {
        send_dns_pkt_info(SPIN_DNS_QUERY, &dpkt_info);
    }
}

void handle_dns_answer(pkt_info_t* pkt_info, struct sk_buff* skb) {
    uint16_t offset = pkt_info->payload_offset;
    uint8_t flag_bits;
    uint8_t flag_bits2;
    uint16_t answer_count;
    uint16_t cur_pos;
    uint16_t cur_pos_name;
    uint16_t rr_type;
    uint8_t labellen;
    unsigned int i;
    unsigned char dnsname[256];
    uint8_t* data = (uint8_t*)skb->data + pkt_info->payload_offset;
    size_t payload_size;
    dns_pkt_info_t dpkt_info;

    if (offset > skb->len) {
        return;
    } else {
        payload_size = skb->len - offset;
    }
    //hexdump_k(skb->data, pkt_info->payload_offset, pkt_info->payload_size);
    // check data size as well
    if (payload_size < 12) {
        return;
    }
    flag_bits = data[2];
    flag_bits2 = data[3];
    // must be qr answer
    if (!(flag_bits & 0x80)) {
        return;
    }
    if (!(flag_bits2 & 0x0f) == 0) {
        return;
    }
    // check if there is a query message
    if (read_int16(data + 4) != 1) {
        return;
    }
    answer_count = read_int16(data + 6);

    // copy the query name
    cur_pos = 12;
    cur_pos_name = 0;
    labellen = data[cur_pos++];
    while(labellen > 0) {
        if (cur_pos + labellen > payload_size) {
            printv(2, KERN_WARNING "Error: label len larger than packet payload\n");
            return;
        }
        if (cur_pos_name + labellen > 255) {
            printv(2, KERN_WARNING "Error: domain name over 255 octets\n");
            return;
        }
        dnsname[cur_pos_name++] = labellen;
        memcpy(dnsname + cur_pos_name, data + cur_pos, labellen);
        cur_pos += labellen;
        cur_pos_name += labellen;
        labellen = data[cur_pos++];
    }
    dnsname[cur_pos_name] = '\0';

    // then read all answer ips
    // type should be 1 (A) or 28 (AAAA) and class should be IN (1)
    if (cur_pos + 4 > payload_size) {
        printv(2, KERN_WARNING "unexpected end of payload when reading question RR\n");
        return;
    }
    rr_type = read_int16(data + cur_pos);
    if (rr_type != 1 && rr_type != 28) {
        return;
    }
    cur_pos += 2;
    if (read_int16(data + cur_pos) != 1) {
        return;
    }
    cur_pos += 2;
    // we are now at answer section, so read all of those
    for (i = 0; i < answer_count; i++) {
        // skip the dname
        cur_pos = skip_dname(data, cur_pos, payload_size);
        if (cur_pos < 0) {
            return;
        }
        if (cur_pos + 4 > payload_size) {
            printv(2, KERN_WARNING "unexpected end of payload while reading answer RR\n");
        }
        // read the type
        rr_type = read_int16(data + cur_pos);
        // skip the class
        cur_pos += 4;
        if (rr_type == 1) {
            // data format:
            // <dns type> <ip family> <ip data> <TTL> <domain name wire format>
            if (cur_pos + 10 > payload_size) {
                printv(2, KERN_WARNING "unexpected end of payload while reading answer A RR\n");
            }
            memset(&dpkt_info, 0, sizeof(dns_pkt_info_t));
            dpkt_info.family = AF_INET;
            memcpy(&dpkt_info.ttl, data + cur_pos, 4);
            dpkt_info.ttl = ntohl(dpkt_info.ttl);
            // skip ttl and size of rdata (which should be 4, check?)
            cur_pos += 4;
            cur_pos += 2;
            memcpy(dpkt_info.ip + 12, data + cur_pos, 4);
            cur_pos += 4;
            //hexdump_k((uint8_t*)&dpkt_info, 0, sizeof(dns_pkt_info_t));
            strncpy(dpkt_info.dname, dnsname, 256);
            send_dns_pkt_info(SPIN_DNS_ANSWER, &dpkt_info);
        } else if (rr_type == 28) {
            // data format:
            // <dns type> <ip family> <ip data> <TTL> <domain name wire format>
            if (cur_pos + 22 > payload_size) {
                printv(2, KERN_WARNING "unexpected end of payload while reading answer AAAA RR\n");
            }
            memset(&dpkt_info, 0, sizeof(dns_pkt_info_t));
            dpkt_info.family = AF_INET6;
            memcpy(&dpkt_info.ttl, data + cur_pos, 4);
            dpkt_info.ttl = ntohl(dpkt_info.ttl);
            // skip ttl and size of rdata (which should be 16, check?)
            cur_pos += 4;
            cur_pos += 2;
            memcpy(dpkt_info.ip, data + cur_pos, 16);
            cur_pos += 16;
            strncpy(dpkt_info.dname, dnsname, 256);
            send_dns_pkt_info(SPIN_DNS_ANSWER, &dpkt_info);
        } else {
            // skip rr data
            // skip ttl
            if (cur_pos + 6 > payload_size) {
                printv(2, KERN_WARNING "unexpected end of payload while reading answer RR size\n");
            }
            cur_pos += 4;
            // skip rdata
            cur_pos += read_int16(data + cur_pos) + 2;
            if (cur_pos > payload_size) {
                printv(2, KERN_WARNING "unexpected end of payload while skipping answer RR\n");
            }

        }
    }
}

void check_pkt_info_timestamp(void) {
    struct timeval tv;
    unsigned int i;
    do_gettimeofday(&tv);

    if (!pkt_info_list_check_timestamp(pkt_info_list, tv.tv_sec)) {
        // send all and clear
        for (i = 0; i < pkt_info_list->cur_size; i++) {
            send_pkt_info(SPIN_TRAFFIC_DATA, pkt_info_list->pkt_infos[i]);
        }
        pkt_info_list_clear(pkt_info_list, tv.tv_sec);
    }
}

void add_pkt_info(pkt_info_t* pkt_info) {
    check_pkt_info_timestamp();
    pkt_info_list_add(pkt_info_list, pkt_info);
}

NF_CALLBACK(hook_func_new, skb)
{
    struct sk_buff *sock_buff;
    pkt_info_t pkt_info;
    int pres;

    memset(&pkt_info, 0, sizeof(pkt_info_t));
    sock_buff = skb;

    if(!sock_buff) {
        printv(5, KERN_DEBUG "Callback called but no skb, accepting packet\n");
        return NF_ACCEPT;
    }

    pres = parse_packet(skb, &pkt_info);
    if (pres == 0) {
        if (ip_store_contains_ip(block_ips, pkt_info.src_addr) ||
            ip_store_contains_ip(block_ips, pkt_info.dest_addr)) {
            // block it unless it is specifically held
            if (!ip_store_contains_ip(except_ips, pkt_info.src_addr) &&
                !ip_store_contains_ip(except_ips, pkt_info.dest_addr)) {
                if (!ip_store_contains_ip(ignore_ips, pkt_info.src_addr) &&
                    !ip_store_contains_ip(ignore_ips, pkt_info.dest_addr)) {
                    // but do check if we should ignore it for the messageing
                    send_pkt_info(SPIN_BLOCKED, &pkt_info);
                }
                printv(5, KERN_DEBUG "Address in block list, dropping packet\n");
                return NF_DROP;
            } else {
                printv(5, KERN_DEBUG "Address in block list, but also in allow list, letting through\n");
            }
        }
        // if message is dns response, send DNS info as well
        if (pkt_info.src_port == 53) {
            handle_dns_answer(&pkt_info, skb);
        }
        if (pkt_info.dest_port == 53) {
            handle_dns_query(&pkt_info, skb);
        }
        if (!ip_store_contains_ip(ignore_ips, pkt_info.src_addr) &&
            !ip_store_contains_ip(ignore_ips, pkt_info.dest_addr)) {
            printv_pkt_info(5, "Parsed packet: ", &pkt_info);
            add_pkt_info(&pkt_info);
        }
    } else {
        if (pres < -1) {
            printv(5, KERN_DEBUG "packet not parsed\n");
        }
    }
    return NF_ACCEPT;
}

static void traffic_client_connect(struct sk_buff *skb) {
    struct nlmsghdr *nlh;
    int pid;
    struct sk_buff *skb_out;
    int msg_size;
    char *msg="Hello from kernel";
    int client_id;
    //int res;

    msg_size=strlen(msg);

    nlh=(struct nlmsghdr*)skb->data;
    pid = nlh->nlmsg_pid; /* port id of sending process */

    client_id = traffic_clients_add(traffic_clients, pid);

    skb_out = nlmsg_new(msg_size,0);

    printv(2, KERN_INFO "Got a ping from client (pid %u)\n", pid);

    if(!skb_out) {
        printv(1, KERN_ERR "Failed to allocate new skb\n");
        return;
    }
}

void send_config_response(int port_id, config_command_t cmd, size_t msg_size, void* msg_src) {
    struct nlmsghdr *nlh;
    struct sk_buff *skb_out;
    int res;

    skb_out = nlmsg_new(msg_size + 2, 0);

    if (!skb_out) {
        printv(1, KERN_ERR "Failed to allocate new skb\n");
        return;
    }

    nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size + 2, 0);

    /* not in mcast group */
    NETLINK_CB(skb_out).dst_group = 0;
    *((uint8_t*)nlmsg_data(nlh)) = (uint8_t)SPIN_NETLINK_PROTOCOL_VERSION;
    *((uint8_t*)nlmsg_data(nlh) + 1) = (uint8_t)cmd;

    memcpy(nlmsg_data(nlh) + 2, msg_src, msg_size);

    hexdump_k(nlmsg_data(nlh), 0, msg_size + 2);
    hexdump_k(skb_out->data, 0, msg_size + 1 + 16);

    printv(5, KERN_INFO "sending config response\n");
    res = nlmsg_unicast(config_nl_sk, skb_out, port_id);

    if (res < 0) {
        printv(1, KERN_INFO "Error while sending config response to user\n");
    }
}

void send_config_response_ip_list_callback(unsigned char ip[16], int is_ipv6, void* port_id_p) {
    unsigned char msg[17];
    int port_id = *(int*)port_id_p;
    if (is_ipv6) {
        msg[0] = (uint8_t)AF_INET6;
        memcpy(msg + 1, ip, 16);
        send_config_response(port_id, SPIN_CMD_IP, 17, msg);
    } else {
        msg[0] = (uint8_t)AF_INET;
        memcpy(msg + 1, ip + 12, 4);
        send_config_response(port_id, SPIN_CMD_IP, 5, msg);
    }
}

// return address family (AF_INET or AF_INET6)
int cmd_read_ip(uint8_t* databuf, unsigned char ip[16]) {
    int family = databuf[0];
    if (family == AF_INET) {
        memset(ip, 0, 12);
        memcpy(ip+12, databuf + 1, 4);
    } else if (family == AF_INET6) {
        memcpy(ip, databuf + 1, 16);
    } else {
        return -1;
    }
    return family;
}

int cmd_add_ip(uint8_t* databuf, ip_store_t* ip_store) {
    unsigned char ip[16];
    int family = cmd_read_ip(databuf, ip);
    if (family == AF_INET6) {
        ip_store_add_ip(ip_store, 1, ip);
        return 1;
    } else if (family == AF_INET) {
        ip_store_add_ip(ip_store, 0, ip);
        return 1;
    } else {
        return 0;
    }
}

int cmd_remove_ip(uint8_t* databuf, ip_store_t* ip_store) {
    unsigned char ip[16];
    int family = cmd_read_ip(databuf, ip);
    if (family > 0) {
        ip_store_remove_ip(ip_store, ip);
        return 1;
    }
    return 0;
}

static void config_client_connect(struct sk_buff *skb) {
    struct nlmsghdr *nlh;
    int pid;
    char error_msg[640];
    config_command_t cmd;
    uint8_t* cmdbuf;

    nlh = (struct nlmsghdr *) skb->data;
    printv(2, KERN_INFO "Got command of size %u (%u)\n", skb->len, nlh->nlmsg_len);

    /* pid of sending process */
    pid = nlh->nlmsg_pid;
    printv(2, KERN_INFO "Client (pid %u) connected to config port\n", pid);

    cmdbuf = (uint8_t*) NLMSG_DATA(nlh);

    hexdump_k(cmdbuf, 0, nlh->nlmsg_len);

    if (skb->len < 2) {
        printv(1, KERN_INFO "got command of size < 2\n");
        snprintf(error_msg, 1024, "empty command");
        send_config_response(pid, SPIN_CMD_ERR, strlen(error_msg), error_msg);
    } else {
        if (cmdbuf[0] != SPIN_NETLINK_PROTOCOL_VERSION) {
            printv(1, KERN_ERR "Bad protocol version from client: %u\n", cmdbuf[0]);
            return;
        }
        cmd = cmdbuf[1];
        printv(3, KERN_DEBUG "[XX] Got command %u\n", cmd);
        switch (cmd) {
        case SPIN_CMD_GET_IGNORE:
            ip_store_for_each(ignore_ips, send_config_response_ip_list_callback, &pid);
            break;
        case SPIN_CMD_ADD_IGNORE:
            cmd_add_ip(cmdbuf+2, ignore_ips);
            break;
        case SPIN_CMD_REMOVE_IGNORE:
            cmd_remove_ip(cmdbuf+2, ignore_ips);
            break;
        case SPIN_CMD_CLEAR_IGNORE:
            ip_store_destroy(ignore_ips);
            ignore_ips = ip_store_create();
            break;
        case SPIN_CMD_GET_BLOCK:
            ip_store_for_each(block_ips, send_config_response_ip_list_callback, &pid);
            break;
        case SPIN_CMD_ADD_BLOCK:
            cmd_add_ip(cmdbuf+2, block_ips);
            break;
        case SPIN_CMD_REMOVE_BLOCK:
            cmd_remove_ip(cmdbuf+2, block_ips);
            break;
        case SPIN_CMD_CLEAR_BLOCK:
            ip_store_destroy(block_ips);
            block_ips = ip_store_create();
            break;
        case SPIN_CMD_GET_EXCEPT:
            ip_store_for_each(except_ips, send_config_response_ip_list_callback, &pid);
            break;
        case SPIN_CMD_ADD_EXCEPT:
            cmd_add_ip(cmdbuf+2, except_ips);
            break;
        case SPIN_CMD_REMOVE_EXCEPT:
            cmd_remove_ip(cmdbuf+2, except_ips);
            break;
        case SPIN_CMD_CLEAR_EXCEPT:
            ip_store_destroy(except_ips);
            except_ips = ip_store_create();
            break;
        default:
            snprintf(error_msg, 1024, "unknown command: %u", cmd);
            send_config_response(pid, SPIN_CMD_ERR, strlen(error_msg), error_msg);
        }
    }
    send_config_response(pid, SPIN_CMD_END, 0, NULL);
}


//This is for 3.6 kernels and above.
struct netlink_kernel_cfg netlink_traffic_cfg = {
    .input = traffic_client_connect,
};
struct netlink_kernel_cfg netlink_config_cfg = {
    .input = config_client_connect,
};

static int __init init_netfilter(void) {
    traffic_nl_sk = netlink_kernel_create(&init_net, NETLINK_TRAFFIC_PORT, &netlink_traffic_cfg);
    if(!traffic_nl_sk)
    {
        printv(1, KERN_ALERT "Error creating socket.\n");
        return -10;
    }
    printv(1, KERN_INFO "SPIN traffic port created\n");

    config_nl_sk = netlink_kernel_create(&init_net, NETLINK_CONFIG_PORT, &netlink_config_cfg);
    if(!config_nl_sk)
    {
        printv(1, KERN_ALERT "Error creating socket.\n");
        return -10;
    }
    printv(1, KERN_INFO "SPIN config port created\n");

    return 0;
}


static void close_netfilter(void) {
    netlink_kernel_release(traffic_nl_sk);
    netlink_kernel_release(config_nl_sk);
}

static inline void log_ip(unsigned char ip[16], int is_ipv6, void* foo) {
    char sa[INET6_ADDRSTRLEN];
    (void) foo;
    if (is_ipv6) {
        ntop(AF_INET6, sa, ip, INET6_ADDRSTRLEN);
    } else {
        ntop(AF_INET, sa, ip, INET6_ADDRSTRLEN);
    }
    printv(5, KERN_DEBUG "IP: %s\n", sa);
}

static void timer_cleanup(void)
{
    hrtimer_cancel(& htimer);
}

static enum hrtimer_restart timer_function(struct hrtimer * timer)
{
    check_pkt_info_timestamp();

    hrtimer_forward_now(timer, kt_periode);
    return HRTIMER_RESTART;
}

static void timer_init(void)
{
    kt_periode = ktime_set(1, 0); //seconds,nanoseconds
    hrtimer_init (& htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
    htimer.function = timer_function;
    hrtimer_start(& htimer, kt_periode, HRTIMER_MODE_REL);
}

void register_hook(struct nf_hook_ops* hook) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
    nf_register_net_hook(&init_net, hook);
#else
    nf_register_hook(hook);
#endif
}

void unregister_hook(struct nf_hook_ops* hook) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
    nf_unregister_net_hook(&init_net, hook);
#else
    nf_unregister_hook(hook);
#endif
}

void init_local(void) {
    printv(1, KERN_INFO "SPIN initializing local mode\n");

    nfho1.hook = hook_func_new;
    nfho1.hooknum = NF_INET_LOCAL_IN;
    nfho1.pf = PF_INET;
    nfho1.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho1);

    nfho2.hook = hook_func_new;
    nfho2.hooknum = NF_INET_LOCAL_OUT;
    nfho2.pf = PF_INET;
    nfho2.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho2);

    nfho3.hook = hook_func_new;
    nfho3.hooknum = NF_INET_LOCAL_IN;
    nfho3.pf = PF_INET6;
    nfho3.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho3);

    nfho4.hook = hook_func_new;
    nfho4.hooknum = NF_INET_LOCAL_OUT;
    nfho4.pf = PF_INET6;
    nfho4.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho4);
}

void init_forward(void) {
    printv(1, KERN_INFO "SPIN initializing forward mode\n");

    nfho1.hook = hook_func_new;
    nfho1.hooknum = NF_INET_PRE_ROUTING;
    nfho1.pf = PF_INET;
    nfho1.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho1);

    nfho2.hook = hook_func_new;
    nfho2.hooknum = NF_INET_POST_ROUTING;
    nfho2.pf = PF_INET;
    nfho2.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho2);

    nfho3.hook = hook_func_new;
    nfho3.hooknum = NF_INET_PRE_ROUTING;
    nfho3.pf = PF_INET6;
    nfho3.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho3);

    nfho4.hook = hook_func_new;
    nfho4.hooknum = NF_INET_POST_ROUTING;
    nfho4.pf = PF_INET6;
    nfho4.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho4);
}

void init_forward2(void) {
    printv(1, KERN_INFO "SPIN initializing forward2 mode\n");

    nfho1.hook = hook_func_new;
    nfho1.hooknum = NF_INET_PRE_ROUTING;
    nfho1.pf = PF_INET;
    nfho1.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho1);

    nfho2.hook = hook_func_new;
    nfho2.hooknum = NF_INET_POST_ROUTING;
    nfho2.pf = PF_INET;
    nfho2.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho2);

    nfho3.hook = hook_func_new;
    nfho3.hooknum = NF_INET_FORWARD;
    nfho3.pf = PF_INET;
    nfho3.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho3);

    nfho4.hook = hook_func_new;
    nfho4.hooknum = NF_INET_LOCAL_OUT;
    nfho4.pf = PF_INET;
    nfho4.priority = NF_IP_PRI_FIRST;
    register_hook(&nfho4);
}


//Called when module loaded using 'insmod'
int init_module()
{
    log_set_verbosity(verbosity);
    pkt_info_list = pkt_info_list_create(1024);

    init_netfilter();

    printv(1, KERN_INFO "SPIN module loaded\n");

    if (strncmp(mode, "local", 6) == 0) {
        init_local();
    } else if (strncmp(mode, "forward", 8) == 0) {
        init_forward();
    } else if (strncmp(mode, "forward2", 8) == 0) {
        init_forward2();
    } else {
        pkt_info_list_destroy(pkt_info_list);
        close_netfilter();
        return -ENODEV;
    }

    ignore_ips = ip_store_create();
    block_ips = ip_store_create();
    except_ips = ip_store_create();
    timer_init();

    //handler_info = data_handler_init(traffic_nl_sk);
    traffic_clients = traffic_clients_create(traffic_nl_sk);

    return 0;
}

//Called when module unloaded using 'rmmod'
void cleanup_module()
{
    printv(1, KERN_INFO "SPIN module shutting down\n");
    printv(3, KERN_INFO "stopping handler thread\n");
    traffic_clients_destroy(traffic_clients);
    printv(3, KERN_INFO "stopped handler thread\n");

    timer_cleanup();
    pkt_info_list_destroy(pkt_info_list);
    close_netfilter();
    unregister_hook(&nfho1);
    unregister_hook(&nfho2);
    unregister_hook(&nfho3);
    unregister_hook(&nfho4);

    ip_store_destroy(ignore_ips);
    printv(1, KERN_INFO "SPIN module finished\n");
}

MODULE_LICENSE("GPL");
