
#include "pkt_info_list.h"
#include "spin_util.h"

pkt_info_list_t* pkt_info_list_create(unsigned int size) {
    unsigned int i;
    pkt_info_list_t* pkt_info_list = (pkt_info_list_t*) kmalloc(sizeof(pkt_info_list_t), __GFP_WAIT);

    pkt_info_list->cur_size = 0;
    pkt_info_list->max_size = size;
    pkt_info_list->timestamp = 0;

    // pre-allocate all pkt_infos
    pkt_info_list->pkt_infos = (pkt_info_t**)kmalloc(sizeof(pkt_info_t*) * size, __GFP_WAIT);
    for (i = 0; i < size; i++) {
        pkt_info_list->pkt_infos[i] = (pkt_info_t*)kmalloc(sizeof(pkt_info_t), __GFP_WAIT);
    }
    return pkt_info_list;
}

void pkt_info_list_destroy(pkt_info_list_t* pkt_info_list) {
    unsigned int i;
    for (i = 0; i < pkt_info_list->max_size; i++) {
        kfree(pkt_info_list->pkt_infos[i]);
    }
    kfree(pkt_info_list->pkt_infos);
    kfree(pkt_info_list);

}

pkt_info_t* pkt_info_list_getnew(pkt_info_list_t* pkt_info_list) {
    if (pkt_info_list->cur_size < pkt_info_list->max_size) {
        return pkt_info_list->pkt_infos[pkt_info_list->cur_size++];
    } else {
        printk(KERN_ERR "Out of storage size for pkt_info_list (TODO)\n");
        return NULL;
    }
}

pkt_info_t* pkt_info_list_get(pkt_info_list_t* pkt_info_list, unsigned int index) {
    if (index <= pkt_info_list->cur_size) {
        return pkt_info_list->pkt_infos[index];
    } else {
        return NULL;
    }
}

int pkt_info_list_check_timestamp(pkt_info_list_t* pkt_info_list, uint32_t timestamp) {
    return timestamp == pkt_info_list->timestamp;
}

void pkt_info_list_clear(pkt_info_list_t* pkt_info_list, uint32_t timestamp) {
    pkt_info_list->cur_size = 0;
    pkt_info_list->timestamp = timestamp;
}

void pkt_info_list_resize(pkt_info_list_t* pkt_info_list, unsigned int new_size) {
	unsigned int i;
	if (new_size < pkt_info_list->max_size) {
		return;
	}
	pkt_info_list->pkt_infos = (pkt_info_t**)krealloc(pkt_info_list->pkt_infos, sizeof(pkt_info_t*) * new_size, __GFP_WAIT);
    for (i = pkt_info_list->max_size; i < new_size; i++) {
        pkt_info_list->pkt_infos[i] = (pkt_info_t*)kmalloc(sizeof(pkt_info_t), __GFP_WAIT);
    }
    pkt_info_list->max_size = new_size;
}

void pkt_info_list_add(pkt_info_list_t* pkt_info_list, pkt_info_t* pkt_info) {
    unsigned int i;
    pkt_info_t* cur_pkt_info = NULL;
    if (pkt_info_list->cur_size == pkt_info_list->max_size) {
		pkt_info_list_resize(pkt_info_list, pkt_info_list->max_size + 1024);
	}

    for (i=0; i<pkt_info_list->cur_size; i++) {
        if (pkt_info_equal(pkt_info_list->pkt_infos[i], pkt_info)) {
            cur_pkt_info = pkt_info_list->pkt_infos[i];
            break;
        }
    }
    if (cur_pkt_info == NULL) {
        //printk("[XX] new pkt_info at ts %u (size %u)\n", pkt_info_list->timestamp, pkt_info->payload_size);
        cur_pkt_info = pkt_info_list_getnew(pkt_info_list);
        memcpy(cur_pkt_info, pkt_info, sizeof(pkt_info_t));
    } else {
        //printk("[XX] add size %u to existing\n", pkt_info->payload_size);
        cur_pkt_info->packet_count += 1;
        cur_pkt_info->payload_size += pkt_info->payload_size;
    }
}
