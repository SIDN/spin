
#include <spindata.h>

// Sends the given string to the rpc domain socket
// returns the response
// caller must free response data
char* send_jsonrpc_message_raw(const char* request);

spin_data rpcc_list_devices();
spin_data rpcc_get_device_by_mac(const char* device_mac);


// tmp?
char* rpcc_get_device_name(spin_data device);
char* rpcc_get_device_ips_as_string(spin_data device);
