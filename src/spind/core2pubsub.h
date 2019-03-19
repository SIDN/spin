void pubsub_publish(char *, int, const void*, int);
void core2pubsub_publish(buffer_t *);
void init_mosquitto(const char* , int );
void finish_mosquitto();
