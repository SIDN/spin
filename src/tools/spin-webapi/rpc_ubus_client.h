/**
 * Synchronous ubus message request
 *
 * request is a string containing a JSON RPC request
 *
 * Returns the response as a string containing JSON data
 * Return data must be freed by called
 */
char* send_ubus_message_raw(const char* request);
