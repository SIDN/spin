//Taken from https://stackoverflow.com/questions/15215865/netlink-sockets-in-c-using-the-3-x-linux-kernel?lq=1

#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <errno.h>

#include "pkt_info.h"
#include "dns_cache.h"
#include "tree.h"

#include <poll.h>

#include <mosquitto.h>

#include <signal.h>

#define NETLINK_TRAFFIC_PORT 31

#define MAX_PAYLOAD 1024 /* maximum payload size*/
struct sockaddr_nl src_addr, dest_addr;
struct nlmsghdr *nlh = NULL;
struct iovec iov;
int sock_fd;
struct msghdr msg;

int ack_counter;

static dns_cache_t* dns_cache;

void hexdump(uint8_t* data, unsigned int size) {
    unsigned int i;
    printf("00: ");
    for (i = 0; i < size; i++) {
        if (i > 0 && i % 10 == 0) {
            printf("\n%u: ", i);
        }
        printf("%02x ", data[i]);
    }
    printf("\n");
}

void send_ack()
{
    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
    memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
    nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;

    strcpy(NLMSG_DATA(nlh), "Hello!");

    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    printf("Sending ACK to kernel\n");
    sendmsg(sock_fd, &msg, 0);

}

#define MESSAGES_BEFORE_PING 50
void check_send_ack() {
    ack_counter++;
    if (ack_counter > MESSAGES_BEFORE_PING) {
        send_ack();
        ack_counter = 0;
    }
}

int init_netlink()
{
    ssize_t c = 0;
    int rs;
    message_type_t type;
    struct timeval tv;
    struct pollfd fds[1];

    ack_counter = 0;

    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_TRAFFIC_PORT);
    if(sock_fd<0) {
        fprintf(stderr, "Error connecting to socket: %s\n", strerror(errno));
        return -1;
    }

    tv.tv_sec = 0;
    tv.tv_usec = 500;
    setsockopt(sock_fd, 270, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid(); /* self pid */

    bind(sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr));

    memset(&dest_addr, 0, sizeof(dest_addr));
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0; /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

    send_ack();

    fds[0].fd = sock_fd;
    fds[0].events = POLLIN;

    /* Read message from kernel */
    while (1) {
        rs = poll(fds, 1, 500);
        if (rs == 0) {
            continue;
        }
        printf("[XX] RECV msg: %p\n", &msg);
        rs = recvmsg(sock_fd, &msg, 0);

        if (rs < 0) {
            continue;
        }
        c++;
        //printf("C: %u RS: %u\n", c, rs);
        //printf("Received message payload: %s\n", (char *)NLMSG_DATA(nlh));
        pkt_info_t pkt;
        dns_pkt_info_t dns_pkt;
        char pkt_str[2048];
        type = wire2pktinfo(&pkt, (unsigned char *)NLMSG_DATA(nlh));
        if (type == SPIN_BLOCKED) {
            //pktinfo2str(pkt_str, &pkt, 2048);
            //printf("[BLOCKED] %s\n", pkt_str);
            check_send_ack();
        } else if (type == SPIN_TRAFFIC_DATA) {
            //pktinfo2str(pkt_str, &pkt, 2048);
            //printf("[TRAFFIC] %s\n", pkt_str);
            check_send_ack();
        } else if (type == SPIN_DNS_ANSWER) {
            // note: bad version would have been caught in wire2pktinfo
            // in this specific case
            wire2dns_pktinfo(&dns_pkt, (unsigned char *)NLMSG_DATA(nlh));
            dns_pktinfo2str(pkt_str, &dns_pkt, 2048);
            printf("[DNS] %s\n", pkt_str);
            printf("[XX] add to cache\n");
            dns_cache_add(dns_cache, &dns_pkt);
            printf("[XX] added to cache\n");
            dns_cache_print(dns_cache);

            check_send_ack();
        } else if (type == SPIN_ERR_BADVERSION) {
            printf("Error: version mismatch between client and kernel module\n");
        } else {
            printf("unknown type? %u\n", type);
        }
    }
    close(sock_fd);
    return 0;
}

static struct mosquitto* mosq;

void init_mosquitto(void) {
    const char* client_name = "asdf";
    const char* host = "127.0.0.1";
    const int port = 1883;
    int result;

    mosquitto_lib_init();
    mosq = mosquitto_new(client_name, 1, NULL);
    // check error
    result = mosquitto_connect(mosq, host, port, 1);
    if (result != 0) {
        fprintf(stderr, "Error connecting to mqtt server on %s:%d, %s\n", host, port, mosquitto_strerror(result));
        exit(1);
    }
}

#if 0
void stop_mosquitto(void) {
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}

int main(int argc, char** argv) {
    init_mosquitto();

    mosquitto_publish(mosq, NULL, "SPIN/test", 5, "asdf", 0, false);
    return init_netlink();
}
#endif

int my_cmp(size_t key_a_size, void* key_a, size_t key_b_size, void* key_b) {
    const char* a = (const char*) key_a;
    const char* b = (const char*) key_b;
    size_t s = key_a_size;
    int result;

    if (s > key_b_size) {
        s = key_b_size;
    }
    result = strncmp(a, b, s);
    if (result == 0) {
        if (key_a_size > key_b_size) {
            return -1;
        } else if (key_a_size < key_b_size) {
            return 1;
        } else {
            return 0;
        }
    }
    return result;
}

void
print_tree(tree_t* tree) {
    tree_entry_t* cur;
    printf("[XX] TREE! depth: %d\n", tree_entry_depth(tree->root));
    cur = tree_first(tree);
    while (cur != NULL) {
        printf("key: %s value: %s\n", (const char*)cur->key, (const char*)cur->data);
        if (cur->parent != NULL) {
            printf("[XX]    par: %s\n", cur->parent->key);
        } else {
            printf("[XX]    par: <nul>\n");
        }
        if (cur->left != NULL) {
            printf("[XX]    lft: %s\n", cur->left->key);
        } else {
            printf("[XX]    lft: <nul>\n");
        }
        if (cur->right != NULL) {
            printf("[XX]    rgt: %s\n", cur->right->key);
        } else {
            printf("[XX]    rgt: <nul>\n");
        }
        cur = tree_next(cur);
    }
    printf("[XX] END OF TREE\n");
}

int test_tree(int argc, char** argv) {
    tree_t* tree = tree_create(my_cmp);
    printf("[XX] 1\n");
    tree_add(tree, 5, "aaaa", 4, "foo", 1);
    tree_add(tree, 5, "bbbb", 4, "foo", 1);
    tree_add(tree, 5, "cccc", 4, "foo", 1);
    tree_add(tree, 5, "dddd", 4, "foo", 1);
    tree_add(tree, 5, "eeee", 4, "foo", 1);
    print_tree(tree);
    tree->root = tree_entry_balance(tree->root);
    print_tree(tree);
    tree_destroy(tree);

    tree = tree_create(my_cmp);
    printf("\n");
    printf("[XX] 2\n");
    tree_add(tree, 5, "iiii", 4, "foo", 1);
    tree_add(tree, 5, "hhhh", 4, "foo", 1);
    tree_add(tree, 5, "gggg", 4, "foo", 1);
    tree_add(tree, 5, "ffff", 4, "foo", 1);
    tree_add(tree, 5, "eeee", 4, "foo", 1);
    tree_add(tree, 5, "dddd", 4, "foo", 1);
    tree_add(tree, 5, "cccc", 4, "foo", 1);
    tree_add(tree, 5, "bbbb", 4, "foo", 1);
    tree_add(tree, 5, "aaaa", 4, "foo", 1);
    print_tree(tree);
    tree_destroy(tree);
    tree = tree_create(my_cmp);
    printf("\n");
    printf("[XX] 3\n");
    tree_add(tree, 5, "aaaa", 4, "foo", 1);
    tree_add(tree, 5, "eeee", 4, "foo", 1);
    tree_add(tree, 5, "bbbb", 4, "foo", 1);
    tree_add(tree, 5, "cccc", 4, "foo", 1);
    tree_add(tree, 5, "dddd", 4, "foo", 1);
    print_tree(tree);
    tree_destroy(tree);
    tree = tree_create(my_cmp);
    printf("\n");
    printf("[XX] 4\n");
    tree_add(tree, 5, "bbbb", 4, "foo", 1);
    tree_add(tree, 5, "aaaa", 4, "foo", 1);
    tree_add(tree, 5, "dddd", 4, "foo", 1);
    tree_add(tree, 5, "cccc", 4, "foo", 1);
    tree_add(tree, 5, "eeee", 4, "foo", 1);
    print_tree(tree);
    tree_destroy(tree);
    tree = tree_create(my_cmp);
    printf("\n");
    printf("[XX] 5\n");
    tree_add(tree, 5, "aaaa", 4, "foo", 1);
    tree_add(tree, 5, "bbbb", 4, "foo", 1);
    tree_add(tree, 5, "cccc", 4, "foo", 1);
    tree_add(tree, 5, "dddd", 4, "foo", 1);
    tree_add(tree, 5, "eeee", 4, "foo", 1);
    print_tree(tree);
    tree_destroy(tree);

    printf("\n");
#if 0
#endif

}

void init_cache() {
    dns_cache = dns_cache_create();
}

void cleanup_cache() {
    dns_cache_destroy(dns_cache);
}

void int_handler(int signal) {
    cleanup_cache();
    free(nlh);
    exit(1);
}

void
cache_test() {
}

int main(int argc, char** argv) {
    int result;

    init_cache();
    signal(SIGINT, int_handler);

    result = init_netlink();

    cleanup_cache();
    return 0;
}
