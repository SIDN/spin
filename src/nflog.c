#include <nflog.h>

struct nflog_handle
{
        struct nfnl_handle *nfnlh;
        struct nfnl_subsys_handle *nfnlssh;
        struct nflog_g_handle *gh_list;
};

struct nflog_data
{
        struct nfattr **nfa;
};

#define BUFSZ 10000

void
setup_netlogger_loop(int groupnum, nflog_callback *callback, void* callback_data, struct nflog_g_handle** cleanup_handle, struct mosquitto* mqtt)
{
  int sz;
  int fd = -1;
  char buf[BUFSZ];
  /* Setup handle */
  struct nflog_handle *handle = NULL;
  struct nflog_g_handle *group = NULL;

  memset(buf, 0, sizeof(buf));

  /* This opens the relevent netlink socket of the relevent type */
  if ((handle = nflog_open()) == NULL){
    fprintf(stderr, "Could not get netlink handle\n");
    exit(1);
  }

  /* We tell the kernel that we want ipv4 tables not ipv6 */
  /* v6 packets are logged anyway? */
  if (nflog_bind_pf(handle, AF_INET) < 0) {
    fprintf(stderr, "Could not bind netlink handle\n");
    exit(1);
  }
  /* this causes double reports for v6
  if (nflog_bind_pf(handle, AF_INET6) < 0) {
    fprintf(stderr, "Could not bind netlink handle (6)\n");
    exit(1);
  }*/

  /* Setup groups, this binds to the group specified */
  if ((group = nflog_bind_group(handle, groupnum)) == NULL) {
    fprintf(stderr, "Could not bind to group\n");
    exit(1);
  }
  if (nflog_set_mode(group, NFULNL_COPY_PACKET, 0xffff) < 0) {
    fprintf(stderr, "Could not set group mode\n");
    exit(1);
  }
  if (nflog_set_nlbufsiz(group, BUFSZ) < 0) {
    fprintf(stderr, "Could not set group buffer size\n");
    exit(1);
  }
  if (nflog_set_timeout(group, 1) < 0) {
    fprintf(stderr, "Could not set the group timeout\n");
  }

  /* Register the callback */
  //nflog_callback_register(group, &queue_push, (void *)queue);
  nflog_callback_register(group, callback, callback_data);

  /* Get the actual FD for the netlogger entry */
  fd = nflog_fd(handle);

struct timeval tv;
tv.tv_sec = 1;  /* 30 Secs Timeout */
tv.tv_usec = 0;  // Not init'ing this can cause strange errors
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

  /* We continually read from the loop and push the contents into
     nflog_handle_packet (which seperates one entry from the other),
     which will eventually invoke our callback (queue_push) */
  if (cleanup_handle != NULL) {
      *cleanup_handle = group;
  }
  for (;;) {
    if (mqtt != NULL) {
        mosquitto_loop(mqtt, 10, 5);
    }
    sz = recv(fd, buf, BUFSZ, 0);
    if (sz < 0 && (errno == EINTR || errno == EAGAIN)) {
        continue;
    } else if (sz < 0) {
        printf("Error reading from nflog socket\n");
        break;
    }
    nflog_handle_packet(handle, buf, sz);
  }
}

void nflog_cleanup_handle(struct nflog_g_handle* ghandle) {
    struct nflog_handle* handle = ghandle->h;
    nflog_unbind_group(ghandle);
    nflog_close(handle);
}


