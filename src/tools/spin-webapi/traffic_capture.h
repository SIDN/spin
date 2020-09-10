int tc_answer_direct_capture_request(struct MHD_Connection* connection, const char* url);
int tc_answer_mqtt_capture_request(struct MHD_Connection* connection, const char* url);
void tc_stop_all_captures();
int tc_captures_running();
int tc_capture_running_for(const char* device_mac);
int tc_get_bytes_sent_for(const char* device_mac);
int tc_start_mqtt_capture_for(const char* device_mac);
void tc_stop_capture_for(const char* device_mac);

