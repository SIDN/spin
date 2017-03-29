/* libnetfilter_log.c: generic library for access to NFLOG
 *
 * (C) 2005 by Harald Welte <laforge@gnumonks.org>
 * (C) 2005, 2008-2010 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <libnetfilter_log/linux_nfnetlink_log.h>

#include <libnfnetlink/libnfnetlink.h>
#include <libnetfilter_log/libnetfilter_log.h>

struct nflog_handle
{
        struct nfnl_handle *nfnlh;
        struct nfnl_subsys_handle *nfnlssh;
        struct nflog_g_handle *gh_list;
};

struct nflog_g_handle
{
        struct nflog_g_handle *next;
        struct nflog_handle *h;
        u_int16_t id;

        nflog_callback *cb;
        void *data;
};

struct nflog_data
{
        struct nfattr **nfa;
};

int nflog_errno;

/***********************************************************************
 * low level stuff
 ***********************************************************************/

static void del_gh(struct nflog_g_handle *gh)
{
        struct nflog_g_handle *cur_gh, *prev_gh = NULL;

        for (cur_gh = gh->h->gh_list; cur_gh; cur_gh = cur_gh->next) {
                if (cur_gh == gh) {
                        if (prev_gh)
                                prev_gh->next = gh->next;
                        else
                                gh->h->gh_list = gh->next;
                        return;
                }
                prev_gh = cur_gh;
        }
}

static void add_gh(struct nflog_g_handle *gh)
{
        gh->next = gh->h->gh_list;
        gh->h->gh_list = gh;
}

static struct nflog_g_handle *find_gh(struct nflog_handle *h, u_int16_t group)
{
        struct nflog_g_handle *gh;

        for (gh = h->gh_list; gh; gh = gh->next) {
                if (gh->id == group)
                        return gh;
        }
        return NULL;
}

/* build a NFULNL_MSG_CONFIG message */
static int
__build_send_cfg_msg(struct nflog_handle *h, u_int8_t command,
                     u_int16_t groupnum, u_int8_t pf)
{
        union {
                char buf[NFNL_HEADER_LEN
                        +NFA_LENGTH(sizeof(struct nfulnl_msg_config_cmd))];
                struct nlmsghdr nmh;
        } u;
        struct nfulnl_msg_config_cmd cmd;

        nfnl_fill_hdr(h->nfnlssh, &u.nmh, 0, pf, groupnum,
                      NFULNL_MSG_CONFIG, NLM_F_REQUEST|NLM_F_ACK);

        cmd.command = command;
        nfnl_addattr_l(&u.nmh, sizeof(u), NFULA_CFG_CMD, &cmd, sizeof(cmd));

        return nfnl_query(h->nfnlh, &u.nmh);
}

static int __nflog_rcv_pkt(struct nlmsghdr *nlh, struct nfattr *nfa[],
                            void *data)
{
        struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
        struct nflog_handle *h = data;
        u_int16_t group = ntohs(nfmsg->res_id);
        struct nflog_g_handle *gh = find_gh(h, group);
        struct nflog_data nfldata;

        if (!gh)
                return -ENODEV;

        if (!gh->cb)
                return -ENODEV;

        nfldata.nfa = nfa;
        return gh->cb(gh, nfmsg, &nfldata, gh->data);
}

static struct nfnl_callback pkt_cb = {
        .call           = &__nflog_rcv_pkt,
        .attr_count     = NFULA_MAX,
};

/* public interface */

struct nfnl_handle *nflog_nfnlh(struct nflog_handle *h)
{
        return h->nfnlh;
}

int nflog_fd(struct nflog_handle *h)
{
        return nfnl_fd(nflog_nfnlh(h));
}

struct nflog_handle *nflog_open_nfnl(struct nfnl_handle *nfnlh)
{
        struct nflog_handle *h;
        int err;

        h = malloc(sizeof(*h));
        if (!h)
                return NULL;

        memset(h, 0, sizeof(*h));
        h->nfnlh = nfnlh;

        h->nfnlssh = nfnl_subsys_open(h->nfnlh, NFNL_SUBSYS_ULOG,
                                      NFULNL_MSG_MAX, 0);
        if (!h->nfnlssh) {
                /* FIXME: nflog_errno */
                goto out_free;
        }

        pkt_cb.data = h;
        err = nfnl_callback_register(h->nfnlssh, NFULNL_MSG_PACKET, &pkt_cb);
        if (err < 0) {
                nflog_errno = err;
                goto out_close;
        }

        return h;
out_close:
        nfnl_close(h->nfnlh);
out_free:
        free(h);
        return NULL;
}

struct nflog_handle *nflog_open(void)
{
        struct nfnl_handle *nfnlh;
        struct nflog_handle *lh;

        nfnlh = nfnl_open();
        if (!nfnlh) {
                /* FIXME: nflog_errno */
                return NULL;
        }

        /* disable netlink sequence tracking by default */
        nfnl_unset_sequence_tracking(nfnlh);

        lh = nflog_open_nfnl(nfnlh);
        if (!lh)
                nfnl_close(nfnlh);

        return lh;
}

int nflog_callback_register(struct nflog_g_handle *gh, nflog_callback *cb,
                             void *data)
{
        gh->data = data;
        gh->cb = cb;

        return 0;
}

int nflog_handle_packet(struct nflog_handle *h, char *buf, int len)
{
        return nfnl_handle_packet(h->nfnlh, buf, len);
}

int nflog_close(struct nflog_handle *h)
{
        int ret = nfnl_close(h->nfnlh);
        free(h);
        return ret;
}

int nflog_bind_pf(struct nflog_handle *h, u_int16_t pf)
{
        return __build_send_cfg_msg(h, NFULNL_CFG_CMD_PF_BIND, 0, pf);
}


int nflog_unbind_pf(struct nflog_handle *h, u_int16_t pf)
{
        return __build_send_cfg_msg(h, NFULNL_CFG_CMD_PF_UNBIND, 0, pf);
}

struct nflog_g_handle *
nflog_bind_group(struct nflog_handle *h, u_int16_t num)
{
        struct nflog_g_handle *gh;

        if (find_gh(h, num))
                return NULL;

        gh = malloc(sizeof(*gh));
        if (!gh)
                return NULL;

        memset(gh, 0, sizeof(*gh));
        gh->h = h;
        gh->id = num;

        if (__build_send_cfg_msg(h, NFULNL_CFG_CMD_BIND, num, 0) < 0) {
                free(gh);
                return NULL;
        }

        add_gh(gh);
        return gh;
}

int nflog_unbind_group(struct nflog_g_handle *gh)
{
        int ret = __build_send_cfg_msg(gh->h, NFULNL_CFG_CMD_UNBIND, gh->id, 0);
        if (ret == 0) {
                del_gh(gh);
                free(gh);
        }

        return ret;
}

int nflog_set_mode(struct nflog_g_handle *gh,
                   u_int8_t mode, u_int32_t range)
{
        union {
                char buf[NFNL_HEADER_LEN
                        +NFA_LENGTH(sizeof(struct nfulnl_msg_config_mode))];
                struct nlmsghdr nmh;
        } u;
        struct nfulnl_msg_config_mode params;

        nfnl_fill_hdr(gh->h->nfnlssh, &u.nmh, 0, AF_UNSPEC, gh->id,
                      NFULNL_MSG_CONFIG, NLM_F_REQUEST|NLM_F_ACK);

        params.copy_range = htonl(range);       /* copy_range is short */
        params.copy_mode = mode;
        nfnl_addattr_l(&u.nmh, sizeof(u), NFULA_CFG_MODE, &params,
                       sizeof(params));

        return nfnl_query(gh->h->nfnlh, &u.nmh);
}

int nflog_set_timeout(struct nflog_g_handle *gh, u_int32_t timeout)
{
        union {
                char buf[NFNL_HEADER_LEN+NFA_LENGTH(sizeof(u_int32_t))];
                struct nlmsghdr nmh;
        } u;

        nfnl_fill_hdr(gh->h->nfnlssh, &u.nmh, 0, AF_UNSPEC, gh->id,
                      NFULNL_MSG_CONFIG, NLM_F_REQUEST|NLM_F_ACK);

        nfnl_addattr32(&u.nmh, sizeof(u), NFULA_CFG_TIMEOUT, htonl(timeout));

        return nfnl_query(gh->h->nfnlh, &u.nmh);
}

int nflog_set_qthresh(struct nflog_g_handle *gh, u_int32_t qthresh)
{
        union {
                char buf[NFNL_HEADER_LEN+NFA_LENGTH(sizeof(u_int32_t))];
                struct nlmsghdr nmh;
        } u;

        nfnl_fill_hdr(gh->h->nfnlssh, &u.nmh, 0, AF_UNSPEC, gh->id,
                      NFULNL_MSG_CONFIG, NLM_F_REQUEST|NLM_F_ACK);

        nfnl_addattr32(&u.nmh, sizeof(u), NFULA_CFG_QTHRESH, htonl(qthresh));

        return nfnl_query(gh->h->nfnlh, &u.nmh);
}

int nflog_set_nlbufsiz(struct nflog_g_handle *gh, u_int32_t nlbufsiz)
{
        union {
                char buf[NFNL_HEADER_LEN+NFA_LENGTH(sizeof(u_int32_t))];
                struct nlmsghdr nmh;
        } u;
        int status;

        nfnl_fill_hdr(gh->h->nfnlssh, &u.nmh, 0, AF_UNSPEC, gh->id,
                      NFULNL_MSG_CONFIG, NLM_F_REQUEST|NLM_F_ACK);

        nfnl_addattr32(&u.nmh, sizeof(u), NFULA_CFG_NLBUFSIZ, htonl(nlbufsiz));

        status = nfnl_query(gh->h->nfnlh, &u.nmh);

        /* we try to have space for at least 10 messages in the socket buffer */
        if (status >= 0)
                nfnl_rcvbufsiz(gh->h->nfnlh, 10*nlbufsiz);

        return status;
}

int nflog_set_flags(struct nflog_g_handle *gh, u_int16_t flags)
{
        union {
                char buf[NFNL_HEADER_LEN+NFA_LENGTH(sizeof(u_int16_t))];
                struct nlmsghdr nmh;
        } u;

        nfnl_fill_hdr(gh->h->nfnlssh, &u.nmh, 0, AF_UNSPEC, gh->id,
                      NFULNL_MSG_CONFIG, NLM_F_REQUEST|NLM_F_ACK);

        nfnl_addattr16(&u.nmh, sizeof(u), NFULA_CFG_FLAGS, htons(flags));

        return nfnl_query(gh->h->nfnlh, &u.nmh);
}

struct nfulnl_msg_packet_hdr *nflog_get_msg_packet_hdr(struct nflog_data *nfad)
{
        return nfnl_get_pointer_to_data(nfad->nfa, NFULA_PACKET_HDR,
                                         struct nfulnl_msg_packet_hdr);
}

u_int16_t nflog_get_hwtype(struct nflog_data *nfad)
{
        return ntohs(nfnl_get_data(nfad->nfa, NFULA_HWTYPE, u_int16_t));
}

u_int16_t nflog_get_msg_packet_hwhdrlen(struct nflog_data *nfad)
{
        return ntohs(nfnl_get_data(nfad->nfa, NFULA_HWLEN, u_int16_t));
}

char *nflog_get_msg_packet_hwhdr(struct nflog_data *nfad)
{
        return nfnl_get_pointer_to_data(nfad->nfa, NFULA_HWHEADER, char);
}

u_int32_t nflog_get_nfmark(struct nflog_data *nfad)
{
        return ntohl(nfnl_get_data(nfad->nfa, NFULA_MARK, u_int32_t));
}

int nflog_get_timestamp(struct nflog_data *nfad, struct timeval *tv)
{
        struct nfulnl_msg_packet_timestamp *uts;

        uts = nfnl_get_pointer_to_data(nfad->nfa, NFULA_TIMESTAMP,
                                        struct nfulnl_msg_packet_timestamp);
        if (!uts)
                return -1;

        tv->tv_sec = __be64_to_cpu(uts->sec);
        tv->tv_usec = __be64_to_cpu(uts->usec);

        return 0;
}

u_int32_t nflog_get_indev(struct nflog_data *nfad)
{
        return ntohl(nfnl_get_data(nfad->nfa, NFULA_IFINDEX_INDEV, u_int32_t));
}

u_int32_t nflog_get_physindev(struct nflog_data *nfad)
{
        return ntohl(nfnl_get_data(nfad->nfa, NFULA_IFINDEX_PHYSINDEV, u_int32_t));
}

u_int32_t nflog_get_outdev(struct nflog_data *nfad)
{
        return ntohl(nfnl_get_data(nfad->nfa, NFULA_IFINDEX_OUTDEV, u_int32_t));
}

u_int32_t nflog_get_physoutdev(struct nflog_data *nfad)
{
        return ntohl(nfnl_get_data(nfad->nfa, NFULA_IFINDEX_PHYSOUTDEV, u_int32_t));
}

struct nfulnl_msg_packet_hw *nflog_get_packet_hw(struct nflog_data *nfad)
{
        return nfnl_get_pointer_to_data(nfad->nfa, NFULA_HWADDR,
                                        struct nfulnl_msg_packet_hw);
}

int nflog_get_payload(struct nflog_data *nfad, char **data)
{
        *data = nfnl_get_pointer_to_data(nfad->nfa, NFULA_PAYLOAD, char);
        if (*data)
                return NFA_PAYLOAD(nfad->nfa[NFULA_PAYLOAD-1]);

        return -1;
}

char *nflog_get_prefix(struct nflog_data *nfad)
{
        return nfnl_get_pointer_to_data(nfad->nfa, NFULA_PREFIX, char);
}

int nflog_get_uid(struct nflog_data *nfad, u_int32_t *uid)
{
        if (!nfnl_attr_present(nfad->nfa, NFULA_UID))
                return -1;

        *uid = ntohl(nfnl_get_data(nfad->nfa, NFULA_UID, u_int32_t));
        return 0;
}

int nflog_get_gid(struct nflog_data *nfad, u_int32_t *gid)
{
        if (!nfnl_attr_present(nfad->nfa, NFULA_GID))
                return -1;

        *gid = ntohl(nfnl_get_data(nfad->nfa, NFULA_GID, u_int32_t));
        return 0;
}

int nflog_get_seq(struct nflog_data *nfad, u_int32_t *seq)
{
        if (!nfnl_attr_present(nfad->nfa, NFULA_SEQ))
                return -1;

        *seq = ntohl(nfnl_get_data(nfad->nfa, NFULA_SEQ, u_int32_t));
        return 0;
}

int nflog_get_seq_global(struct nflog_data *nfad, u_int32_t *seq)
{
        if (!nfnl_attr_present(nfad->nfa, NFULA_SEQ_GLOBAL))
                return -1;

        *seq = ntohl(nfnl_get_data(nfad->nfa, NFULA_SEQ_GLOBAL, u_int32_t));
        return 0;
}

#define SNPRINTF_FAILURE(ret, rem, offset, len)                 \
do {                                                            \
        if (ret < 0)                                            \
                return ret;                                     \
        len += ret;                                             \
        if (ret > rem)                                          \
                ret = rem;                                      \
        offset += ret;                                          \
        rem -= ret;                                             \
} while (0)

int nflog_snprintf_xml(char *buf, size_t rem, struct nflog_data *tb, int flags)
{
        struct nfulnl_msg_packet_hdr *ph;
        struct nfulnl_msg_packet_hw *hwph;
        u_int32_t mark, ifi;
        int size, offset = 0, len = 0, ret;
        char *data;

        size = snprintf(buf + offset, rem, "<log>");
        SNPRINTF_FAILURE(size, rem, offset, len);

        if (flags & NFLOG_XML_TIME) {
                time_t t;
                struct tm tm;

                t = time(NULL);
                if (localtime_r(&t, &tm) == NULL)
                        return -1;

                size = snprintf(buf + offset, rem, "<when>");
                SNPRINTF_FAILURE(size, rem, offset, len);

                size = snprintf(buf + offset, rem,
                                "<hour>%d</hour>", tm.tm_hour);
                SNPRINTF_FAILURE(size, rem, offset, len);

                size = snprintf(buf + offset,
                                rem, "<min>%02d</min>", tm.tm_min);
                SNPRINTF_FAILURE(size, rem, offset, len);

                size = snprintf(buf + offset,
                                rem, "<sec>%02d</sec>", tm.tm_sec);
                SNPRINTF_FAILURE(size, rem, offset, len);

                size = snprintf(buf + offset, rem, "<wday>%d</wday>",
                                tm.tm_wday + 1);
                SNPRINTF_FAILURE(size, rem, offset, len);

                size = snprintf(buf + offset, rem, "<day>%d</day>", tm.tm_mday);
                SNPRINTF_FAILURE(size, rem, offset, len);

                size = snprintf(buf + offset, rem, "<month>%d</month>",
                                tm.tm_mon + 1);
                SNPRINTF_FAILURE(size, rem, offset, len);

                size = snprintf(buf + offset, rem, "<year>%d</year>",
                                1900 + tm.tm_year);
                SNPRINTF_FAILURE(size, rem, offset, len);

                size = snprintf(buf + offset, rem, "</when>");
                SNPRINTF_FAILURE(size, rem, offset, len);
        }

        data = nflog_get_prefix(tb);
        if (data && (flags & NFLOG_XML_PREFIX)) {
                size = snprintf(buf + offset, rem, "<prefix>%s</prefix>", data);
                SNPRINTF_FAILURE(size, rem, offset, len);
        }

        ph = nflog_get_msg_packet_hdr(tb);
        if (ph) {
                size = snprintf(buf + offset, rem, "<hook>%u</hook>", ph->hook);
                SNPRINTF_FAILURE(size, rem, offset, len);

                hwph = nflog_get_packet_hw(tb);
                if (hwph && (flags & NFLOG_XML_HW)) {
                        int i, hlen = ntohs(hwph->hw_addrlen);

                        size = snprintf(buf + offset, rem, "<hw><proto>%04x"
                                                           "</proto>",
                                        ntohs(ph->hw_protocol));
                        SNPRINTF_FAILURE(size, rem, offset, len);

                        size = snprintf(buf + offset, rem, "<src>");
                        SNPRINTF_FAILURE(size, rem, offset, len);

                        for (i=0; i<hlen; i++) {
                                size = snprintf(buf + offset, rem, "%02x",
                                                hwph->hw_addr[i]);
                                SNPRINTF_FAILURE(size, rem, offset, len);
                        }

                        size = snprintf(buf + offset, rem, "</src></hw>");
                        SNPRINTF_FAILURE(size, rem, offset, len);
                } else if (flags & NFLOG_XML_HW) {
                        size = snprintf(buf + offset, rem, "<hw><proto>%04x"
                                                    "</proto></hw>",
                                 ntohs(ph->hw_protocol));
                        SNPRINTF_FAILURE(size, rem, offset, len);
                }
        }

        mark = nflog_get_nfmark(tb);
        if (mark && (flags & NFLOG_XML_MARK)) {
                size = snprintf(buf + offset, rem, "<mark>%u</mark>", mark);
                SNPRINTF_FAILURE(size, rem, offset, len);
        }

        ifi = nflog_get_indev(tb);
        if (ifi && (flags & NFLOG_XML_DEV)) {
                size = snprintf(buf + offset, rem, "<indev>%u</indev>", ifi);
                SNPRINTF_FAILURE(size, rem, offset, len);
        }

        ifi = nflog_get_outdev(tb);
        if (ifi && (flags & NFLOG_XML_DEV)) {
                size = snprintf(buf + offset, rem, "<outdev>%u</outdev>", ifi);
                SNPRINTF_FAILURE(size, rem, offset, len);
        }

        ifi = nflog_get_physindev(tb);
        if (ifi && (flags & NFLOG_XML_PHYSDEV)) {
                size = snprintf(buf + offset, rem,
                                "<physindev>%u</physindev>", ifi);
                SNPRINTF_FAILURE(size, rem, offset, len);
        }

        ifi = nflog_get_physoutdev(tb);
        if (ifi && (flags & NFLOG_XML_PHYSDEV)) {
                size = snprintf(buf + offset, rem,
                                "<physoutdev>%u</physoutdev>", ifi);
                SNPRINTF_FAILURE(size, rem, offset, len);
        }

        ret = nflog_get_payload(tb, &data);
        if (ret >= 0 && (flags & NFLOG_XML_PAYLOAD)) {
                int i;

                size = snprintf(buf + offset, rem, "<payload>");
                SNPRINTF_FAILURE(size, rem, offset, len);

                for (i=0; i<ret; i++) {
                        size = snprintf(buf + offset, rem, "%02x",
                                        data[i] & 0xff);
                        SNPRINTF_FAILURE(size, rem, offset, len);
                }

                size = snprintf(buf + offset, rem, "</payload>");
                SNPRINTF_FAILURE(size, rem, offset, len);
        }

        size = snprintf(buf + offset, rem, "</log>");
        SNPRINTF_FAILURE(size, rem, offset, len);

        return len;
}

#define MAX 2048

void
hexprint(char* d, size_t s) {
    size_t i = 0, j = 0;
    printf("00:\t");
    for (i = 0; i < s; i++) {
        if ((i > 0) && (i%10 == 0)) {
            printf("\n%u:\t", i);
        }
        printf("%02x ", (uint8_t) d[i]);
    }
    printf("\n");
}

void printxml(struct nflog_data* nfldata) {
    char buf[4096];
    int size;

    size = nflog_snprintf_xml(buf, 4096, nfldata, NFLOG_XML_ALL);
    printf("XML Size: %d\n", size);
    printf("%s\n", buf);
}

int global_count = 0;
void stop_and_roll(struct nflog_g_handle* ghandle) {
    struct nflog_handle* handle = ghandle->h;
    nflog_unbind_group(ghandle);
    nflog_close(handle);
    exit(0);
}


int queue_push(struct nflog_g_handle *handle, struct nfgenmsg *msg, struct nflog_data *nfldata, void *foo)
{
    char* data;
    size_t datalen;
    struct nfulnl_msg_packet_hdr* phdr = nflog_get_msg_packet_hdr(nfldata);
    printf("%02x\n", phdr->hw_protocol);

    //data = (char*)malloc(MAX);
    data = NULL;
    datalen = nflog_get_payload(nfldata, &data);

    printf("NFLOG event, payload size: %u, data:\n", datalen);
    hexprint(data, datalen);
    fflush(stdout);
    if (phdr->hw_protocol == 0x0800) {
        printf("From: %d.%d.%d.%d\n", (uint8_t)data[12], (uint8_t)data[13], (uint8_t)data[14], (uint8_t)data[15]);
        printf("To:   %d.%d.%d.%d\n", (uint8_t)data[16], (uint8_t)data[17], (uint8_t)data[18], (uint8_t)data[19]);
    } else {
        printf("From: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n", (uint8_t)data[8],
                                                                                                (uint8_t)data[9],
                                                                                                (uint8_t)data[10],
                                                                                                (uint8_t)data[11],
                                                                                                (uint8_t)data[12],
                                                                                                (uint8_t)data[13],
                                                                                                (uint8_t)data[14],
                                                                                                (uint8_t)data[15],
                                                                                                (uint8_t)data[16],
                                                                                                (uint8_t)data[17],
                                                                                                (uint8_t)data[18],
                                                                                                (uint8_t)data[19],
                                                                                                (uint8_t)data[20],
                                                                                                (uint8_t)data[21],
                                                                                                (uint8_t)data[22],
                                                                                                (uint8_t)data[23]);
        printf("To:   %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n", (uint8_t)data[24],
                                                                                                  (uint8_t)data[25],
                                                                                                  (uint8_t)data[26],
                                                                                                  (uint8_t)data[27],
                                                                                                  (uint8_t)data[28],
                                                                                                  (uint8_t)data[29],
                                                                                                  (uint8_t)data[30],
                                                                                                  (uint8_t)data[31],
                                                                                                  (uint8_t)data[32],
                                                                                                  (uint8_t)data[33],
                                                                                                  (uint8_t)data[34],
                                                                                                  (uint8_t)data[35],
                                                                                                  (uint8_t)data[36],
                                                                                                  (uint8_t)data[37],
                                                                                                  (uint8_t)data[38],
                                                                                                  (uint8_t)data[39]);
    }
    printxml(nfldata);
    printf("%02x\n", nflog_get_hwtype(nfldata));
    //free(data);

    if (global_count > 5) {
        stop_and_roll(handle);
    }
    global_count++;
}


#define BUFSZ 10000

void setup_netlogger_loop(
    int groupnum)
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
  if (nflog_set_timeout(group, 1500) < 0) {
    fprintf(stderr, "Could not set the group timeout\n");
  }

  /* Register the callback */
  //nflog_callback_register(group, &queue_push, (void *)queue);
  nflog_callback_register(group, &queue_push, (void *)NULL);

  /* Get the actual FD for the netlogger entry */
  fd = nflog_fd(handle);

  /* We continually read from the loop and push the contents into
     nflog_handle_packet (which seperates one entry from the other),
     which will eventually invoke our callback (queue_push) */
  for (;;) {
    sz = recv(fd, buf, BUFSZ, 0);
    if (sz < 0 && errno == EINTR)
      continue;
    else if (sz < 0)
      break;

    nflog_handle_packet(handle, buf, sz);
  }
}

int main(int argc, char** argv) {
    setup_netlogger_loop(1);
    printf("Hello, world!\n");
    return 0;
}
