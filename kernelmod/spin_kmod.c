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
#include <linux/inet.h>
#include <linux/kernel.h>

#include <linux/errno.h>

#include "messaging.h"
#include "pkt_info.h"
#include "spin_util.h"

//#include <arpa/inet.h>

// kernel module examples from http://www.paulkiddie.com/2009/11/creating-a-netfilter-kernel-module-which-filters-udp-packets/
// netlink examples from https://gist.github.com/arunk-s/c897bb9d75a6c98733d6

static struct nf_hook_ops nfho1;
static struct nf_hook_ops nfho2;

struct sk_buff *sock_buff;
struct udphdr *udp_header;
struct tcphdr *tcp_header;
struct iphdr *ip_header;            //ip header struct


struct sock *nl_sk = NULL;
uint32_t client_port_id = 0;

#define NETLINK_USER 31

void log_packet(pkt_info_t* pkt_info) {
	char pkt_str[INET6_ADDRSTRLEN];
	pktinfo2str(pkt_str, pkt_info, INET6_ADDRSTRLEN);
	printk("%s\n", pkt_str);
}

int parse_packet(struct sk_buff* sockbuff, pkt_info_t* pkt_info) {
    ip_header = (struct iphdr *)skb_network_header(sock_buff);

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
		return 1;
	} else {
		pkt_info->payload_size = htonl((uint32_t)sockbuff->len - skb_network_header_len(sockbuff));
	}
	//printk("data len: %u header len: %u\n", sockbuff->data_len, skb_network_header_len(sockbuff));
	
	// rest of basic info
	pkt_info->family = ip_header->version;
	pkt_info->protocol = ip_header->protocol;
	memcpy(pkt_info->src_addr, &ip_header->saddr, 4);
	memcpy(pkt_info->dest_addr, &ip_header->daddr, 4);
	return 0;
}

void send_pkt_info(pkt_info_t* pkt_info) {
	struct nlmsghdr *nlh;
	int msg_size;
	struct sk_buff* skb_out;
	int res;
	
	char msg[INET6_ADDRSTRLEN];
	pktinfo2str(msg, pkt_info, INET6_ADDRSTRLEN);
	
	if (client_port_id == 0) {
		printk("Client not connected, not sending\n");
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

    res = nlmsg_unicast(nl_sk, skb_out, client_port_id);

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
		//if (netlink_has_listeners(nl_sk, 0)) {
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
	memset(&pkt_info, 0, sizeof(pkt_info_t));
    sock_buff = skb;
    (void)in;
    (void)out;
    (void)okfn;
    
    if(!sock_buff) { return NF_ACCEPT;}

	if (parse_packet(skb, &pkt_info) == 0) {
		//log_packet(&pkt_info);
		//if (netlink_has_listeners(nl_sk, 0)) {
			send_pkt_info(&pkt_info);
		//} else {
		//	printk("no listeners");
		//}
	}
    return NF_ACCEPT;
}


static void hello_nl_recv_msg(struct sk_buff *skb) {
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

/*
    nlh = nlmsg_put(skb_out,0,0,NLMSG_DONE,msg_size,0);
    NETLINK_CB(skb_out).dst_group = 0;
    strncpy(nlmsg_data(nlh),msg,msg_size);

    res = nlmsg_unicast(nl_sk, skb_out, pid);

    if(res<0) {
        printk(KERN_INFO "Error sending data to client: %u\n", res);
    }
*/
}

//This is for 3.6 kernels and above.
struct netlink_kernel_cfg netlink_cfg = {
    .input = hello_nl_recv_msg,
};

static int __init init_netfilter(void) {


    printk("Entering: %s\n",__FUNCTION__);
    //printk("init_net at %p, cfg at %p\n", init_net, &cfg);
    printk("netlink init done\n");
    nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &netlink_cfg);
    //nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, 0, hello_nl_recv_msg,NULL,THIS_MODULE);
    printk("netlink_kernel_create called\n");
    if(!nl_sk)
    {
        printk(KERN_ALERT "Error creating socket.\n");
        return -10;
    }

    printk(KERN_ALERT "Netlink socket created.\n");
    return 0;
}


static void close_netfilter(void) {
    printk(KERN_INFO "exiting hello module\n");
    netlink_kernel_release(nl_sk);
}

static inline void log_ip(unsigned char ip[16], void* foo) {
	char sa[INET6_ADDRSTRLEN];
	(void) foo;
	ntop(AF_INET6, sa, ip, INET6_ADDRSTRLEN);
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
	ip_store_add_ip(ip_store, ip1);
	ip_store_add_ip(ip_store, ip2);
	ip_store_add_ip(ip_store, ip3);
	ip_store_remove_ip(ip_store, ip4);
	ip_store_remove_ip(ip_store, ip4);
	
	log_ip(ip1, NULL);
	printk("Full store:\n");
	ip_store_for_each(ip_store, log_ip, NULL);
	ip_store_destroy(ip_store);
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

	test_ip();
    return 0;
}

//Called when module unloaded using 'rmmod'
void cleanup_module()
{
    close_netfilter();
    printk(KERN_INFO "Hello World (tm)(c)(patent pending) signing off!\n");
    nf_unregister_hook(&nfho1);                     //cleanup – unregister hook
    nf_unregister_hook(&nfho2);                     //cleanup – unregister hook
}

MODULE_LICENSE("GPL");
