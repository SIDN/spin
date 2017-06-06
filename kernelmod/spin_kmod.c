//'Hello World' netfilter hooks example
//For any packet, we drop it, and log fact to /var/log/messages

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>

#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/inet.h>
#include <linux/kernel.h>

#include <linux/errno.h>

#include "messaging.h"
#include "pkt_info.h"
#include "spin_util.h"
#include "spin_cfg.h"

//#include <arpa/inet.h>

// kernel module examples from http://www.paulkiddie.com/2009/11/creating-a-netfilter-kernel-module-which-filters-udp-packets/
// netlink examples from https://gist.github.com/arunk-s/c897bb9d75a6c98733d6


// Hooks for packet capture
static struct nf_hook_ops nfho1;
static struct nf_hook_ops nfho2;
static struct nf_hook_ops nfho3;
static struct nf_hook_ops nfho4;


struct sk_buff *sock_buff;
struct udphdr *udp_header;
struct tcphdr *tcp_header;
struct iphdr *ip_header;            //ip header struct
struct ipv6hdr *ipv6_header;            //ip header struct

ip_store_t* ignore_ips;
ip_store_t* block_ips;
ip_store_t* except_ips;

struct sock *traffic_nl_sk = NULL;
struct sock *config_nl_sk = NULL;
uint32_t client_port_id = 0;

#define NETLINK_CONFIG_PORT 30
#define NETLINK_TRAFFIC_PORT 31

void log_packet(pkt_info_t* pkt_info) {
	char pkt_str[INET6_ADDRSTRLEN];
	pktinfo2str(pkt_str, pkt_info, INET6_ADDRSTRLEN);
	printk("%s\n", pkt_str);
}

int parse_ipv6_packet(struct sk_buff* sockbuff, pkt_info_t* pkt_info) {
    ipv6_header = (struct ipv6hdr *)ipv6_hdr(sock_buff);
    if (ipv6_header->nexthdr == 17) {
		udp_header = (struct udphdr *)skb_transport_header(sock_buff);
		pkt_info->src_port = udp_header->source;
		pkt_info->dest_port = udp_header->dest;
		pkt_info->payload_size = htonl((uint32_t)ntohs(udp_header->len)) - 8;
		pkt_info->payload_offset = htons(skb_network_header_len(sockbuff)) + 8;
	} else if (ipv6_header->nexthdr == 6) {
		tcp_header = (struct tcphdr *)skb_transport_header(sock_buff);
		pkt_info->src_port = tcp_header->source;
		pkt_info->dest_port = tcp_header->dest;
		pkt_info->payload_size = htonl((uint32_t)sockbuff->len - skb_network_header_len(sockbuff) - (4*tcp_header->doff) - 2);
		pkt_info->payload_offset = htons(skb_network_header_len(sockbuff) + (4*tcp_header->doff) + 2);
		// if size is zero, ignore tcp packet
		if (pkt_info->payload_size == 0) {
			return 1;
		}
	} else if (ipv6_header->nexthdr != 58) {
		if (ipv6_header->nexthdr == 0) {
			// ignore hop-by-hop option header
			return 1;
		}
		printk("[XX] unsupported IPv6 next header: %u\n", ipv6_header->nexthdr);
		return -1;
	} else {
		pkt_info->payload_size = htonl((uint32_t)sockbuff->len - skb_network_header_len(sockbuff));
		pkt_info->payload_offset = htons(skb_network_header_len(sockbuff));
	}
	//printk("data len: %u header len: %u\n", sockbuff->data_len, skb_network_header_len(sockbuff));
	
	// rest of basic info
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
    ip_header = (struct iphdr *)skb_network_header(sock_buff);
    if (ip_header->version == 6) {
		return parse_ipv6_packet(sockbuff, pkt_info);
	}
	
    if (ip_header->protocol == 17) {
		udp_header = (struct udphdr *)skb_transport_header(sock_buff);
		pkt_info->src_port = udp_header->source;
		pkt_info->dest_port = udp_header->dest;
		pkt_info->payload_size = htonl((uint32_t)ntohs(udp_header->len) - 8);
		pkt_info->payload_offset = htons(skb_network_header_len(sockbuff) + 8);
	} else if (ip_header->protocol == 6) {
		tcp_header = (struct tcphdr *)skb_transport_header(sock_buff);
		pkt_info->src_port = tcp_header->source;
		pkt_info->dest_port = tcp_header->dest;
		pkt_info->payload_size = htonl((uint32_t)sockbuff->len - skb_network_header_len(sockbuff) - (4*tcp_header->doff) - 2);
		pkt_info->payload_offset = htons(skb_network_header_len(sockbuff) + (4*tcp_header->doff) + 2);
		// if size is zero, ignore tcp packet
		if (pkt_info->payload_size == 0) {
			return 1;
		}
	} else if (ip_header->protocol != 1) {
		printk("[XX] unsupported IPv4 protocol: %u\n", ip_header->protocol);
		return -1;
	} else {
		pkt_info->payload_size = htonl((uint32_t)sockbuff->len - skb_network_header_len(sockbuff));
		pkt_info->payload_offset = htons(skb_network_header_len(sockbuff));
	}
	//printk("data len: %u header len: %u\n", sockbuff->data_len, skb_network_header_len(sockbuff));
	
	// rest of basic info
	pkt_info->family = AF_INET;
	pkt_info->protocol = ip_header->protocol;
	memset(pkt_info->src_addr, 0, 12);
	memcpy(pkt_info->src_addr + 12, &ip_header->saddr, 4);
	memset(pkt_info->dest_addr, 0, 12);
	memcpy(pkt_info->dest_addr + 12, &ip_header->daddr, 4);
	return 0;
}

// TODO: refactor two functions below
void send_pkt_info(message_type_t type, pkt_info_t* pkt_info) {
	struct nlmsghdr *nlh;
	int msg_size;
	struct sk_buff* skb_out;
	int res;
	
	char msg[INET6_ADDRSTRLEN];
	pktinfo2str(msg, pkt_info, INET6_ADDRSTRLEN);
	
	// Nobody's listening, carry on
	if (client_port_id == 0) {
		printk("Client not connected, not sending\n");
		return;
	}
	
	// Check ignore list
	if (ip_store_contains_ip(ignore_ips, pkt_info->src_addr) ||
	    ip_store_contains_ip(ignore_ips, pkt_info->dest_addr)) {
		return;
	}

	msg_size = pktinfo_msg_size();
	skb_out = nlmsg_new(msg_size, 0);

    if(!skb_out) {
        printk(KERN_ERR "Failed to allocate new skb\n");
        return;
    }

    nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
    NETLINK_CB(skb_out).dst_group = 0;
    //strncpy(nlmsg_data(nlh),msg,msg_size);
    pktinfo_msg2wire(type, nlmsg_data(nlh), pkt_info);

    res = nlmsg_unicast(traffic_nl_sk, skb_out, client_port_id);

    if(res<0) {
        printk(KERN_INFO "Error sending data to client: %d\n", res);
		if (res == -111) {
			printk(KERN_INFO "Client disappeared\n");
			client_port_id = 0;
		}
    }
}

void hexdump_k(uint8_t* data, unsigned int offset, unsigned int size) {
	unsigned int i;
	printk("%02u: ", 0);
	for (i = 0; i < size; i++) {
		if (i > 0 && i % 10 == 0) {
			printk("\n%02u: ", i);
		}
		printk("%02x ", data[i + offset]);
	}
	printk("\n");
}

void send_dns_pkt_info(message_type_t type, dns_pkt_info_t* dns_pkt_info) {
	struct nlmsghdr *nlh;
	int msg_size;
	struct sk_buff* skb_out;
	int res;
	
	printk("yoyoyoyoyoyoyo\n");
	//char msg[INET6_ADDRSTRLEN];
	//pktinfo2str(msg, pkt_info, INET6_ADDRSTRLEN);
	
	// Nobody's listening, carry on
	if (client_port_id == 0) {
		printk("Client not connected, not sending\n");
		return;
	}
	
	msg_size = dns_pktinfo_msg_size();
	skb_out = nlmsg_new(msg_size, 0);

    if(!skb_out) {
        printk(KERN_ERR "Failed to allocate new skb\n");
        return;
    }

    nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
    NETLINK_CB(skb_out).dst_group = 0;
    //strncpy(nlmsg_data(nlh),msg,msg_size);
    dns_pktinfo_msg2wire(nlmsg_data(nlh), dns_pkt_info);

    res = nlmsg_unicast(traffic_nl_sk, skb_out, client_port_id);

    if(res<0) {
        printk(KERN_INFO "Error sending data to client: %d\n", res);
		if (res == -111) {
			printk(KERN_INFO "Client disappeared\n");
			client_port_id = 0;
		}
    }
    printk("[XX] SENT!!\n");
}

static inline uint16_t read_int16(uint8_t* data) {
	return (256*(uint8_t)data[0]) + (uint8_t)data[1];
}

unsigned int skip_dname(uint8_t* data, unsigned int cur_pos) {
	uint8_t labellen = data[cur_pos++];
	while (labellen > 0) {
		if ((labellen & 0xc0) == 0xc0) {
			// compressed, just skip it
			return ++cur_pos;
		}
		cur_pos += labellen;
		labellen = data[cur_pos++];
	}
	return cur_pos;
}

void handle_dns_answer(pkt_info_t* pkt_info, struct sk_buff *skb) {
	uint16_t offset = ntohs(pkt_info->payload_offset);
	uint8_t flag_bits;
	uint8_t flag_bits2;
	uint16_t answer_count;
	uint16_t cur_pos;
	uint16_t cur_pos_name;
	uint16_t rr_type;
	uint8_t labellen;
	unsigned int i;
	unsigned char dnsname[256];
	uint8_t* data = (uint8_t*)skb->data + ntohs(pkt_info->payload_offset);
	dns_pkt_info_t dpkt_info;
	
	printk("[XX] DNS answer header offset %u packet len %u\n", offset, ntohl(pkt_info->payload_size));
	printk("\n");
	if (offset > skb->len) {
		printk("[XX] error: offset (%u) larger than packet size (%u)\n", offset, skb->len);
		return;
	}
	hexdump_k(skb->data, ntohs(pkt_info->payload_offset), ntohl(pkt_info->payload_size));
	// check data size as well
	flag_bits = data[2];
	flag_bits2 = data[3];
	// must be qr answer
	if (!(flag_bits & 0x80)) {
		printk("[XX] QR not set\n");
		return;
	}
	if (!(flag_bits2 & 0x0f) == 0) {
		printk("[XX] not NOERROR\n");
		return;
	}
	// check if there is a query message
	if (read_int16(data + 4) != 1) {
		printk("0 or more than 1 question rr (%u %04x), skip packet\n", read_int16(data + 4), read_int16(data + 4));
		return;
	}
	answer_count = read_int16(data + 6);
	printk("[XX] answer count: %u\n", answer_count);
	
	// copy the query name
	cur_pos = 12;
	cur_pos_name = 0;
	labellen = data[cur_pos++];

	// skip to next, etc
	//printk("Label len: %u\n", labellen);
	
	while(labellen > 0) {
		//printk("Label len: %u\n", labellen);
		memcpy(dnsname + cur_pos_name, data + cur_pos, labellen);
		cur_pos += labellen;
		cur_pos_name += labellen;
		dnsname[cur_pos_name++] = '.';
		labellen = data[cur_pos++];
	}
	// if we want trailing dot, remove deduction here
	dnsname[--cur_pos_name] = '\0';
	printk("DNS NAME: %s\n", dnsname);

	// then read all answer ips
	// type should be 1 (A) or 28 (AAAA) and class should be IN (1)
	rr_type = read_int16(data + cur_pos);
	if (rr_type != 1 && rr_type != 28) {
		printk("[XX] query rr type (%u) not 1 or 28, skip packet\n", rr_type);
		return;
	}
	cur_pos += 2;
	if (read_int16(data + cur_pos) != 1) {
		printk("[XX] class not IN (%u: %u), skip packet\n", cur_pos, read_int16(data + cur_pos));
		return;
	}
	cur_pos += 2;
	// we are now at answer section, so read all of those
	for (i = 0; i < answer_count; i++) {
		// skip the dname
		printk("[XX] skip dname from: %u\n", cur_pos);
		cur_pos = skip_dname(data, cur_pos);
		printk("[XX] dname skipped pos now: %u\n", cur_pos);
		// read the type
		rr_type = read_int16(data + cur_pos);
		// skip the class
		cur_pos += 4;
		if (rr_type == 1) {
			// okay send
			printk("[XX] found A answer\n");
			// data format:
			// <dns type> <ip family> <ip data> <TTL> <domain name string> (null-terminated?)
			memset(&dpkt_info, 0, sizeof(dns_pkt_info_t));
			dpkt_info.family = 4;
			memcpy(&dpkt_info.ttl, data + cur_pos, 4);
			// skip ttl and size of rdata (which should be 4, check?)
			cur_pos += 4;
			cur_pos += 2;
			memcpy(dpkt_info.ip + 12, data + cur_pos, 4);
			cur_pos += 4;
			hexdump_k((uint8_t*)&dpkt_info, 0, sizeof(dns_pkt_info_t));
			strncpy(dpkt_info.dname, dnsname, 256);
			send_dns_pkt_info(SPIN_DNS_ANSWER, &dpkt_info);
		} else if (rr_type == 28) {
			// okay send
			printk("[XX] found AAAA answer\n");
			// data format:
			// <dns type> <ip family> <ip data> <TTL> <domain name string> (null-terminated?)
			memset(&dpkt_info, 0, sizeof(dns_pkt_info_t));
			dpkt_info.family = 6;
			memcpy(&dpkt_info.ttl, data + cur_pos, 4);
			// skip ttl and size of rdata (which should be 16, check?)
			cur_pos += 4;
			cur_pos += 2;
			memcpy(dpkt_info.ip, data + cur_pos, 16);
			cur_pos += 16;
			strncpy(dpkt_info.dname, dnsname, 256);
			send_dns_pkt_info(SPIN_DNS_ANSWER, &dpkt_info);
		} else {
			// skip rr data
			// 
			printk("[XX] not A or AAAA in answer at %u (val: %u)\n", cur_pos - 4, rr_type);
			printk("[XX] now at %u\n", cur_pos);
			// skip ttl
			cur_pos += 4;
			printk("[XX] after ttl at %u (val here: %u)\n", cur_pos, read_int16(data + cur_pos));
			// skip rdata
			cur_pos += read_int16(data + cur_pos) + 2;
			printk("[XX] skip to: %u\n", cur_pos);
			
		}
	}
}

unsigned int hook_func_new(const struct nf_hook_ops *ops,
						   struct sk_buff *skb,
						   const struct net_device *in,
						   const struct net_device *out,
						   int (*okfn)(struct sk_buff *))
{
	pkt_info_t pkt_info;
    int pres;
	memset(&pkt_info, 0, sizeof(pkt_info_t));
    sock_buff = skb;
    (void)in;
    (void)out;
    (void)okfn;
    
    if(!sock_buff) { return NF_ACCEPT;}

	pres = parse_packet(skb, &pkt_info);
	if (pres == 0) {
		if (ip_store_contains_ip(block_ips, pkt_info.src_addr) ||
			ip_store_contains_ip(block_ips, pkt_info.dest_addr)) {
			// block it unless it is specifically held
			printk(KERN_INFO "[XX] address in block list!\n");
			if (!ip_store_contains_ip(except_ips, pkt_info.src_addr) &&
				!ip_store_contains_ip(except_ips, pkt_info.dest_addr)) {
				send_pkt_info(SPIN_BLOCKED, &pkt_info);
				return NF_DROP;
			}
		}
		// if message is dns response, send DNS info as well
		printk(KERN_INFO "SRC PORT: %u\n", ntohs(pkt_info.src_port));
		if (ntohs(pkt_info.src_port) == 53) {
			handle_dns_answer(&pkt_info, skb);
		}
		send_pkt_info(SPIN_TRAFFIC_DATA, &pkt_info);
	} else {
		if (pres < 0) {
			printk("packet not parsed\n");
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
    //int res;

    printk(KERN_INFO "Entering: %s\n", __FUNCTION__);

    msg_size=strlen(msg);

    nlh=(struct nlmsghdr*)skb->data;
    printk(KERN_INFO "Netlink received msg payload:%s\n",(char*)nlmsg_data(nlh));
    pid = nlh->nlmsg_pid; /* port id of sending process */
	client_port_id = pid;
	
    skb_out = nlmsg_new(msg_size,0);

    if(!skb_out) {
        printk(KERN_ERR "Failed to allocate new skb\n");
        return;
    }
}

void send_config_response(int port_id, config_command_t cmd, size_t msg_size, void* msg_src) {
	struct nlmsghdr *nlh;
	struct sk_buff *skb_out;
	int res;
	
	skb_out = nlmsg_new(msg_size + 1, 0);
	
	if (!skb_out) {
		printk(KERN_ERR "Failed to allocate new skb\n");
		return;
	}
	
	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size + 1, 0);
	
	/* not in mcast group */
	NETLINK_CB(skb_out).dst_group = 0;
	
	memcpy(nlmsg_data(nlh), (uint8_t*)&cmd, 1);
	memcpy(nlmsg_data(nlh) + 1, msg_src, msg_size);
	
	res = nlmsg_unicast(config_nl_sk, skb_out, port_id);

	if (res < 0) {
		printk(KERN_INFO "Error while sending config response to user\n");
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
	char error_msg[1024];
	config_command_t cmd;
	uint8_t* cmdbuf;
	
	printk(KERN_INFO "Entering: %s\n", __FUNCTION__);
	
	nlh = (struct nlmsghdr *) skb->data;
	printk(KERN_INFO "got command of size %u\n", skb->len);

	/* pid of sending process */
	pid = nlh->nlmsg_pid;
	cmdbuf = (uint8_t*) NLMSG_DATA(nlh);
	if (skb->len < 1) {
		printk(KERN_INFO "got command of size 0\n");
		snprintf(error_msg, 1024, "empty command");
		send_config_response(pid, SPIN_CMD_ERR, strlen(error_msg), error_msg);
	} else {
		cmd = cmdbuf[0];
		switch (cmd) {
		case SPIN_CMD_GET_IGNORE:
			ip_store_for_each(ignore_ips, send_config_response_ip_list_callback, &pid);
			break;
		case SPIN_CMD_ADD_IGNORE:
			cmd_add_ip(cmdbuf+1, ignore_ips);
			break;
		case SPIN_CMD_REMOVE_IGNORE:
			cmd_remove_ip(cmdbuf+1, ignore_ips);
			break;
		case SPIN_CMD_CLEAR_IGNORE:
			ip_store_destroy(ignore_ips);
			ignore_ips = ip_store_create();
			break;
		case SPIN_CMD_GET_BLOCK:
			ip_store_for_each(block_ips, send_config_response_ip_list_callback, &pid);
			break;
		case SPIN_CMD_ADD_BLOCK:
			cmd_add_ip(cmdbuf+1, block_ips);
			break;
		case SPIN_CMD_REMOVE_BLOCK:
			cmd_remove_ip(cmdbuf+1, block_ips);
			break;
		case SPIN_CMD_CLEAR_BLOCK:
			ip_store_destroy(block_ips);
			block_ips = ip_store_create();
			break;
		case SPIN_CMD_GET_EXCEPT:
			ip_store_for_each(except_ips, send_config_response_ip_list_callback, &pid);
			break;
		case SPIN_CMD_ADD_EXCEPT:
			cmd_add_ip(cmdbuf+1, except_ips);
			break;
		case SPIN_CMD_REMOVE_EXCEPT:
			cmd_remove_ip(cmdbuf+1, except_ips);
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


    printk("Entering: %s\n",__FUNCTION__);
    //printk("init_net at %p, cfg at %p\n", init_net, &cfg);

    traffic_nl_sk = netlink_kernel_create(&init_net, NETLINK_TRAFFIC_PORT, &netlink_traffic_cfg);
    if(!traffic_nl_sk)
    {
        printk(KERN_ALERT "Error creating socket.\n");
        return -10;
    }
    printk("SPIN traffic port created\n");

    config_nl_sk = netlink_kernel_create(&init_net, NETLINK_CONFIG_PORT, &netlink_config_cfg);
    if(!config_nl_sk)
    {
        printk(KERN_ALERT "Error creating socket.\n");
        return -10;
    }
    printk("SPIN config port created\n");

    return 0;
}


static void close_netfilter(void) {
    printk(KERN_INFO "exiting hello module\n");
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
	printk("[XX] %s\n", sa);
}

void test_ip(void) {
	unsigned char ip1[16];
	unsigned char ip2[16];
	unsigned char ip3[16];
	unsigned char ip4[16];
	ip_store_t* ip_store = ip_store_create();
	memset(ip1, 0, 16);
	memset(ip2, 0, 16);
	memset(ip3, 0, 16);
	memset(ip4, 0, 16);
	ip1[15] = 1;
	ip2[15] = 2;
	ip3[15] = 3;
	ip4[15] = 1;
	ip_store_add_ip(ip_store, 1, ip1);
	ip_store_add_ip(ip_store, 1, ip2);
	ip_store_add_ip(ip_store, 1, ip3);
	ip_store_remove_ip(ip_store, ip4);
	ip_store_remove_ip(ip_store, ip4);
	
	log_ip(ip1, 1, NULL);
	printk("Full store:\n");
	ip_store_for_each(ip_store, log_ip, NULL);
	ip_store_destroy(ip_store);
}

void add_default_test_ips(void) {
	unsigned char ip[16];
	memset(ip, 0, 16);
	ip[15] = 1;
	ip_store_add_ip(ignore_ips, 1, ip);
	ip[15] = 2;
	ip_store_add_ip(ignore_ips, 1, ip);
	ip[12] = 127;
	ip[13] = 0;
	ip[14] = 0;
	ip[15] = 1;
	ip_store_add_ip(ignore_ips, 0, ip);

	ip[12] = 192;
	ip[13] = 168;
	ip[14] = 8;
	ip[15] = 1;
	ip_store_add_ip(block_ips, 0, ip);

}

//Called when module loaded using 'insmod'
int init_module()
{
    init_netfilter();

    printk(KERN_INFO "Hello, world!\n");
    test();

    nfho1.hook = hook_func_new;
    nfho1.hooknum = NF_INET_PRE_ROUTING;
    nfho1.pf = PF_INET;
    nfho1.priority = NF_IP_PRI_FIRST;
    nf_register_hook(&nfho1);

    nfho2.hook = hook_func_new;
    nfho2.hooknum = NF_INET_POST_ROUTING;
    nfho2.pf = PF_INET;
    nfho2.priority = NF_IP_PRI_FIRST;
    nf_register_hook(&nfho2);

    nfho3.hook = hook_func_new;
    nfho3.hooknum = NF_INET_PRE_ROUTING;
    nfho3.pf = PF_INET6;
    nfho3.priority = NF_IP_PRI_FIRST;
    nf_register_hook(&nfho3);

    nfho4.hook = hook_func_new;
    nfho4.hooknum = NF_INET_POST_ROUTING;
    nfho4.pf = PF_INET6;
    nfho4.priority = NF_IP_PRI_FIRST;
    nf_register_hook(&nfho4);

    ignore_ips = ip_store_create();
    block_ips = ip_store_create();
    except_ips = ip_store_create();
    add_default_test_ips();

    return 0;
}

//Called when module unloaded using 'rmmod'
void cleanup_module()
{
    close_netfilter();
    printk(KERN_INFO "Hello World (tm)(c)(patent pending) signing off!\n");
    nf_unregister_hook(&nfho1);                     //cleanup – unregister hook
    nf_unregister_hook(&nfho2);                     //cleanup – unregister hook
    nf_unregister_hook(&nfho3);                     //cleanup – unregister hook
    nf_unregister_hook(&nfho4);                     //cleanup – unregister hook
    
    ip_store_destroy(ignore_ips);
}

MODULE_LICENSE("GPL");
