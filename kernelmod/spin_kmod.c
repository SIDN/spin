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
#include "spin_config.h"

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
		pkt_info->payload_size = htonl((uint32_t)ntohs(udp_header->len));
	} else if (ipv6_header->nexthdr == 6) {
		tcp_header = (struct tcphdr *)skb_transport_header(sock_buff);
		pkt_info->payload_size = htonl((uint32_t)sockbuff->len - skb_network_header_len(sockbuff) - (4*tcp_header->doff));
		// if size is zero, ignore tcp packet
		if (pkt_info->payload_size == 0) {
			return 1;
		}
		pkt_info->src_port = tcp_header->source;
		pkt_info->dest_port = tcp_header->dest;
	} else if (ipv6_header->nexthdr != 58) {
		if (ipv6_header->nexthdr == 0) {
			// ignore hop-by-hop option header
			return 1;
		}
		printk("[XX] unsupported IPv6 next header: %u\n", ipv6_header->nexthdr);
		return -1;
	} else {
		pkt_info->payload_size = htonl((uint32_t)sockbuff->len - skb_network_header_len(sockbuff));
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
		pkt_info->payload_size = htonl((uint32_t)ntohs(udp_header->len));
	} else if (ip_header->protocol == 6) {
		tcp_header = (struct tcphdr *)skb_transport_header(sock_buff);
		pkt_info->payload_size = htonl((uint32_t)sockbuff->len - skb_network_header_len(sockbuff) - (4*tcp_header->doff));
		// if size is zero, ignore tcp packet
		if (pkt_info->payload_size == 0) {
			return 1;
		}
		pkt_info->src_port = tcp_header->source;
		pkt_info->dest_port = tcp_header->dest;
	} else if (ip_header->protocol != 1) {
		printk("[XX] unsupported IPv4 protocol: %u\n", ip_header->protocol);
		return -1;
	} else {
		pkt_info->payload_size = htonl((uint32_t)sockbuff->len - skb_network_header_len(sockbuff));
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

void send_pkt_info(pkt_info_t* pkt_info) {
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
    pktinfo_msg2wire(nlmsg_data(nlh), pkt_info);

    res = nlmsg_unicast(traffic_nl_sk, skb_out, client_port_id);

    if(res<0) {
        printk(KERN_INFO "Error sending data to client: %d\n", res);
		if (res == -111) {
			printk(KERN_INFO "Client disappeared\n");
			client_port_id = 0;
		}
    }
}


unsigned int hook_func(void* priv,
                       struct sk_buff* skb,
//                       const struct nf_hook_state *state)
                       void* state, void* a, void* b)
{
	pkt_info_t pkt_info;
	memset(&pkt_info, 0, sizeof(pkt_info_t));
    sock_buff = skb;
    (void) state;
    
    if(!sock_buff) { return NF_ACCEPT;}

	if (parse_packet(skb, &pkt_info) == 0) {
		//log_packet(&pkt_info);
		//if (netlink_has_listeners(traffic_nl_sk, 0)) {
			send_pkt_info(&pkt_info);
		//} else {
		//	printk("no listeners");
		//}
	}
    return NF_ACCEPT;
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
		send_pkt_info(&pkt_info);
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

void send_config_response_ignore_list_callback(unsigned char ip[16], int is_ipv6, void* port_id_p) {
	//struct nlmsghdr* nlh = (struct nlmsghdr*) nlh_p;
	unsigned char msg[17];
	int port_id = *(int*)port_id_p;
	//printk("should send ip address now\n");
	if (is_ipv6) {
		msg[0] = (uint8_t)AF_INET6;
		memcpy(msg + 1, ip, 16);
		send_config_response(port_id, SPIN_CMD_GET_IGNORE, 17, msg);
	} else {
		msg[0] = (uint8_t)AF_INET;
		memcpy(msg + 1, ip + 12, 4);
		send_config_response(port_id, SPIN_CMD_GET_IGNORE, 5, msg);
	}
}

static void config_client_connect(struct sk_buff *skb) {
	struct nlmsghdr *nlh;
	int pid;
	int msg_size;
	//char *msg="Hello from kernel";
	char *msg = "Config command received";
	char error_msg[1024];
	
	printk(KERN_INFO "Entering: %s\n", __FUNCTION__);
	
	msg_size = strlen(msg);
	
	nlh = (struct nlmsghdr *) skb->data;
	
	/* pid of sending process */
	pid = nlh->nlmsg_pid;
	
	printk(KERN_INFO "Netlink received configuration command: %s\n", (char *) nlmsg_data(nlh));
	
	snprintf(error_msg, 1024, "Unknown command: %u\n", 123);
	//send_config_response(pid, SPIN_CMD_ERR, strlen(error_msg), error_msg);
	printk("[XX] CLIENT PORT ID: %u\n", pid);
	ip_store_for_each(ignore_ips, send_config_response_ignore_list_callback, &pid);
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

void add_default_ignore(void) {
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
    add_default_ignore();

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
