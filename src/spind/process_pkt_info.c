#include <time.h>

#include "ipl.h"
#include "mainloop.h"
#include "process_pkt_info.h"
#include "spind.h"
#include "spin_log.h"
#include "statistics.h"

STAT_MODULE(processpktinfo)

void
process_pkt_info(node_cache_t* node_cache, flow_list_t* flow_list, trafficfunc traffic_hook, int local_mode, pkt_info_t* pkt_info) {
    // TODO: remove time() calls, use the single one at caller
    uint32_t now = time(NULL);
    STAT_COUNTER(ctrsf, sendflow, STAT_TOTAL);
    STAT_COUNTER(ctrlocal, cb-ignore-local, STAT_TOTAL);
    STAT_COUNTER(ctrignore, ignore-ip, STAT_TOTAL);
    ip_t ip;
    node_t* src_node;
    node_t* dest_node;

    if (pkt_info->packet_count > 0 || pkt_info->payload_size > 0) {
        node_cache_add_pkt_info(node_cache, pkt_info, now);

        copy_ip_data(&ip, pkt_info->family, 0, pkt_info->src_addr);
        src_node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
        copy_ip_data(&ip, pkt_info->family, 0, pkt_info->dest_addr);
        dest_node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);

        if (src_node != NULL && dest_node != NULL) {
            // Inform flow accounting layer
            (*traffic_hook)(node_cache, src_node, dest_node, pkt_info->packet_count, pkt_info->payload_size, now, pkt_info->dest_port, pkt_info->icmp_type);

            // small experiment, try to ignore messages from and to
            // this device, unless local_mode is set
            if (!local_mode && src_node->mac==NULL && dest_node->mac==NULL) {
                STAT_VALUE(ctrlocal, 1);
                return;
            }
        }

        // check for configured ignores as well
        // do we need to cache it or should we do this check earlier?
        // do we need to check both source and reply?
        if (addr_in_ignore_list(pkt_info->family, pkt_info->src_addr) ||
            addr_in_ignore_list(pkt_info->family, pkt_info->dest_addr)
           ) {
            STAT_VALUE(ctrignore, 1);
            return;
        }

        STAT_VALUE(ctrsf, 1);
        flow_list_add_pktinfo(flow_list, pkt_info);
    }
}
