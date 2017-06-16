
#ifndef SPIN_PKT_INFO_LIST
#define SPIN_PKT_INFO_LIST 1

#include <linux/kernel.h>

#include "pkt_info.h"
#include "spin_util.h"

// stores a collection of packet infos
typedef struct {
    pkt_info_t** pkt_infos;
    unsigned int cur_size;
    unsigned int max_size;
    uint32_t timestamp;
} pkt_info_list_t;

// Creates a new pkt_info_list structure of the given size
pkt_info_list_t* pkt_info_list_create(unsigned int size);
// free the pkt_info_list structure
void pkt_info_list_destroy(pkt_info_list_t* pkt_info_list);
// Get a pointer to the first pkt_info_t element that is not used for the current timestamp
// this increments the counter cur_size
// If we are at max_size, resize the structure (TODO)
pkt_info_t* pkt_info_list_getnew(pkt_info_list_t* pkt_info_list);
// Get a pointer to the pkt_info_t element at the given index
pkt_info_t* pkt_info_list_get(pkt_info_list_t* pkt_info_list, unsigned int index);
// Returns true if the given timestamp is within range of the timestamp of the info list
int pkt_info_list_check_timestamp(pkt_info_list_t* pkt_info_list, uint32_t timestamp);
// Clear the pkt info list
void pkt_info_list_clear(pkt_info_list_t* pkt_info_list, uint32_t timestamp);

void pkt_info_list_add(pkt_info_list_t* pkt_info_list, pkt_info_t* pkt_info);

#endif // SPIN_PKT_INFO_LIST
