
// callback code when there is nfq data


/*
            //printf("[XX] check nfq\n");
            if ((fds[1].revents & POLLIN)) {
                //printf("[XX] nfq has data\n");
                if ((dns_q_rv = recv(dns_q_fd, dns_q_buf, sizeof(dns_q_buf), 0)) >= 0) {
                    // TODO we should add this one to the poll part
                    // now this is slow and once per loop is not enough
                    nfq_handle_packet(dns_qh, dns_q_buf, dns_q_rv); // send packet to callback
                }
                //printf("[XX] rest of loop\n");
            }
*/

void init_core2nfq_dns() {
    struct nfq_handle* dns_qh = nfq_open();
    if (!dns_qh) {
        fprintf(stderr, "error during nfq_open()\n");
        exit(1);
    }
    struct nfq_q_handle* dns_q_qh = nfq_create_queue(dns_qh,  0, &dns_cap_cb, NULL);
    if (dns_q_qh == NULL) {
        fprintf(stderr, "unable to create Netfilter Queue: %s\n", strerror(errno));
        exit(1);
    }
    printf("[XX] DNS_Q_QH: %p\n", dns_q_qh);
    if (nfq_set_mode(dns_q_qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
        fprintf(stderr, "can't set packet_copy mode\n");
        exit(1);
    }
    // for register
    int dns_q_fd = nfq_fd(dns_qh);
    char dns_q_buf[4096] __attribute__ ((aligned));
    int dns_q_rv;
    
}

void cleanup_core2nfq_dns() {
    nfq_destroy_queue(dns_q_qh);
    nfq_close(dns_qh);
}
