//'Hello World' netfilter hooks example
//For any packet, we drop it, and log fact to /var/log/messages

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>

#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/inet.h>
#include <linux/kernel.h>
//#include <arpa/inet.h>

// kernel module examples from http://www.paulkiddie.com/2009/11/creating-a-netfilter-kernel-module-which-filters-udp-packets/
// netlink examples from https://gist.github.com/arunk-s/c897bb9d75a6c98733d6

static struct nf_hook_ops nfho;         //struct holding set of hook function options

struct sk_buff *sock_buff;
struct udphdr *udp_header;          //udp header struct (not used)
struct iphdr *ip_header;            //ip header struct


struct sock *nl_sk = NULL;

#define NETLINK_USER 53

typedef struct {
	uint8_t family; // 4, 6, etc
	uint8_t protocol; // value for tcp/udp/icmp/etc.
	uint8_t src_addr[16]; // v4 just uses first 4 bytes
	uint8_t dest_addr[16]; // v4 just uses first 4 bytes
	uint16_t src_port;
	uint16_t dest_port;
	uint16_t payload_size;
} packet_info;

void parse_packet_v4(struct iphdr* ip_header, packet_info* pkt_info) {
	pkt_info->family = ip_header->version;
	pkt_info->protocol = ip_header->protocol;
	memcpy(pkt_info->src_addr, &ip_header->saddr, 4);
	memcpy(pkt_info->dest_addr, &ip_header->daddr, 4);
	if (ip_header->protocol == 6 || ip_header->protocol == 17) {
		//pkt_info->src_port = ip_header
		// how to find ports?
	}
}

void ntop(int fam, char* dest, const uint8_t* src, size_t max) {
	snprintf(dest, max, "%d.%d.%d.%d", src[0], src[1], src[2], src[3]);
}

void log_packet(packet_info* pkt_info) {
	char sa[INET6_ADDRSTRLEN];
	char da[INET6_ADDRSTRLEN];
	ntop(AF_INET, sa, pkt_info->src_addr, INET6_ADDRSTRLEN);
	ntop(AF_INET, da, pkt_info->dest_addr, INET6_ADDRSTRLEN);
	printk(KERN_INFO "got packet ipv%d protocol %d from %s to %s\n",
	       pkt_info->family,
		   pkt_info->protocol,
		   sa,
		   da);
}

unsigned int hook_func(void* priv,
                       struct sk_buff *skb,
                       const struct nf_hook_state *state)
{
	packet_info pkt_info;
	memset(&pkt_info, 0, sizeof(packet_info));
    sock_buff = skb;
    ip_header = (struct iphdr *)skb_network_header(sock_buff);
    if(!sock_buff) { return NF_ACCEPT;}
    if (ip_header->protocol == 17 || ip_header->protocol == 1 || ip_header->protocol == 6) {
		parse_packet_v4(ip_header, &pkt_info);
		log_packet(&pkt_info);

        udp_header = (struct udphdr *)skb_transport_header(sock_buff);  //grab transport header
        //printk(KERN_INFO "got udp packet \n");     //log we’ve got udp packet to /var/log/messages
        return NF_ACCEPT;
    }
    //printk(KERN_INFO "packet allowed\n");                                             //log to var/log/messages
    return NF_ACCEPT;
}


static void hello_nl_recv_msg(struct sk_buff *skb) {
    struct nlmsghdr *nlh;
    int pid;
    struct sk_buff *skb_out;
    int msg_size;
    char *msg="Hello from kernel";
    int res;

    printk(KERN_INFO "Entering: %s\n", __FUNCTION__);

    msg_size=strlen(msg);

    nlh=(struct nlmsghdr*)skb->data;
    printk(KERN_INFO "Netlink received msg payload:%s\n",(char*)nlmsg_data(nlh));
    pid = nlh->nlmsg_pid; /*pid of sending process */

    skb_out = nlmsg_new(msg_size,0);

    if(!skb_out) {
        printk(KERN_ERR "Failed to allocate new skb\n");
        return;
    }

    nlh=nlmsg_put(skb_out,0,0,NLMSG_DONE,msg_size,0);
    NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */
    strncpy(nlmsg_data(nlh),msg,msg_size);

    res=nlmsg_unicast(nl_sk,skb_out,pid);

    if(res<0) {
        printk(KERN_INFO "Error while sending bak to user\n");
    }
}

//This is for 3.6 kernels and above.
struct netlink_kernel_cfg cfg = {
    .input = hello_nl_recv_msg,
};

static int __init init_netfilter(void) {


    printk("Entering: %s\n",__FUNCTION__);
    printk("init_net at %p, cfg at %p\n", init_net, &cfg);
    printk("netlink init done\n");
    nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
    printk("netlink_kernel_create called\n");
    //nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, 0, hello_nl_recv_msg,NULL,THIS_MODULE);
    if(!nl_sk)
    {
        printk(KERN_ALERT "Error creating socket.\n");
        return -10;
    }

    printk(KERN_ALERT "Netlink socket created.\n");
    return 0;
}


static void __exit exit_netfilter(void) {
    printk(KERN_INFO "exiting hello module\n");
/*
    netlink_kernel_release(nl_sk);
*/
}


//Called when module loaded using 'insmod'
int init_module()
{
    init_netfilter();

    printk(KERN_INFO "Hello, world!\n");

    nfho.hook = hook_func;                       //function to call when conditions below met
    nfho.hooknum = NF_INET_PRE_ROUTING;            //called right after packet recieved, first hook in Netfilter
    nfho.pf = PF_INET;                           //IPV4 packets
    nfho.priority = NF_IP_PRI_FIRST;             //set to highest priority over all other hook functions
    nf_register_hook(&nfho);                     //register hook

    return 0;                                    //return 0 for success
}

//Called when module unloaded using 'rmmod'
void cleanup_module()
{
    exit_netfilter();
    printk(KERN_INFO "Hello World (tm)(c)(patent pending) signing off!\n");
    nf_unregister_hook(&nfho);                     //cleanup – unregister hook
}

MODULE_LICENSE("GPL");
