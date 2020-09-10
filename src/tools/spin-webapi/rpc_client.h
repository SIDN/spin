
// Sends the given string to the rpc domain socket
// returns the response
// caller must free response data
char* send_jsonrpc_message(const char* request);
