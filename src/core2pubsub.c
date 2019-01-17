#include <stdlib.h>
#include <mosquitto.h>

#include "node_cache.h"
#include "tree.h"
#include "jsmn.h"
#include "mainloop.h"
#include "spin_log.h"

#include "spin_cfg.h"		/* Should go here */
#include "handle_command.h"

#define MOSQUITTO_KEEPALIVE_TIME 60	/* Seconds */

#define MQTT_CHANNEL_TRAFFIC "SPIN/traffic"
#define MQTT_CHANNEL_COMMANDS "SPIN/commands"

struct mosquitto* mosq;
extern node_cache_t* node_cache;

void pubsub_publish(int payloadlen, const void* payload) {

    /*
     * There is a result from command, but for now ignored
     */
    mosquitto_publish(mosq, NULL, MQTT_CHANNEL_TRAFFIC,
    				payloadlen, payload,
				0, false);
}

void core2pubsub_publish(buffer_t *buf) {
    int mosq_result;

    pubsub_publish(buffer_size(buf), buffer_str(buf));
}

void send_command_restart() {
    buffer_t* response_json = buffer_create(4096);
    unsigned int response_size = create_mqtt_command(response_json, "serverRestart", NULL, NULL);
    buffer_finish(response_json);
    if (buffer_ok(response_json)) {
        pubsub_publish(response_size, buffer_str(response_json));
    } else {
        spin_log(LOG_ERR, "error: response size too large\n");
    }
    buffer_destroy(response_json);
}

// returns 1 on success, 0 on error
int json_parse_int_arg(int* dest,
                       const char* json_str,
                       jsmntok_t* tokens,
                       int argument_token_i) {
    jsmntok_t token = tokens[argument_token_i];
    if (token.type != JSMN_PRIMITIVE) {
        return 0;
    } else {
        *dest = atoi(json_str + token.start);
        return 1;
    }
}

int json_parse_string_arg(char* dest,
                          size_t dest_size,
                          const char* json_str,
                          jsmntok_t* tokens,
                          int argument_token_i) {
    jsmntok_t token = tokens[argument_token_i];
    size_t size;
    if (token.type != JSMN_STRING) {
        return 0;
    } else {
        size = snprintf(dest, dest_size, "%.*s", token.end - token.start, json_str+token.start);
        return 1;
    }
}

int json_parse_ip_arg(ip_t* dest,
                      const char* json_str,
                      jsmntok_t* tokens,
                      int argument_token_i) {
    char ip_str[INET6_ADDRSTRLEN];
    int result = json_parse_string_arg(ip_str, INET6_ADDRSTRLEN, json_str, tokens, argument_token_i);
    if (!result) {
        return result;
    } else {
        result = spin_pton(dest, ip_str);
        if (!result) {
            return 0;
        } else {
            return 1;
        }
    }
}

// more complex argument, of the form
// { "node_id": <int>, "name": <str> }
int json_parse_node_id_name_arg(int* node_id,
                                char* name,
                                size_t name_size,
                                const char* json_str,
                                jsmntok_t* tokens,
                                int argument_token_i) {
    jsmntok_t token = tokens[argument_token_i];
    int i, i_n, i_v, result, id_found = 0, name_found = 0;

    if (token.type != JSMN_OBJECT ||
        token.size != 2) {
        return 0;
    } else {
        for (i = 0; i < token.size; i++) {
            // i*2 is name, i*2 + 1 is value
            i_n = argument_token_i + i*2 + 1;
            i_v = argument_token_i + i*2 + 2;
            if (strncmp("name", json_str + tokens[i_n].start, tokens[i_n].end-tokens[i_n].start) == 0) {
                result = json_parse_string_arg(name, name_size, json_str, tokens, i_v);
                if (!result) {
                    return result;
                }
                name_found = 1;
            } else if (strncmp("node_id", json_str + tokens[i_n].start, tokens[i_n].end-tokens[i_n].start) == 0) {
                result = json_parse_int_arg(node_id, json_str, tokens, i_v);
                if (!result) {
                    return result;
                }
                id_found = 1;
            }
        }
    }
    return (name_found && id_found);
}

void handle_json_command_2(size_t cmd_name_len,
                           const char* cmd_name,
                           const char* json_str,
                           jsmntok_t* tokens,
                           int argument_token_i) {
    // figure out which command we got; depending on that we'll
    // parse the arguments (if any), and handle directly, or contact
    // the kernel module if necessary
    config_command_t cmd;
    int node_id_arg = 0;
    int result;
    char str_arg[80];
    ip_t ip_arg;
    // in a few cases, we need to update the node cache
    node_t* node;

    if (strncmp(cmd_name, "get_filters", cmd_name_len) == 0) {
        handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    } else if (strncmp(cmd_name, "get_blocks", cmd_name_len) == 0) {
        handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    } else if (strncmp(cmd_name, "get_alloweds", cmd_name_len) == 0) {
        handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
    } else if (strncmp(cmd_name, "add_filter", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_add_filter(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    } else if (strncmp(cmd_name, "remove_filter", cmd_name_len) == 0) {
        if (json_parse_ip_arg(&ip_arg, json_str, tokens, argument_token_i)) {
            handle_command_remove_ip(SPIN_CMD_REMOVE_IGNORE, &ip_arg, "/etc/spin/ignore.list");
        }
        handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    } else if (strncmp(cmd_name, "reset_filters", cmd_name_len) == 0) {
        handle_command_reset_filters();
        handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    //} else if (strncmp(cmd_name, "get_names", cmd_name_len) == 0) {
    //    handle_command_get_names();
    } else if (strncmp(cmd_name, "add_name", cmd_name_len) == 0) {
        if (json_parse_node_id_name_arg(&node_id_arg, str_arg, 24, json_str, tokens, argument_token_i)) {
            handle_command_add_name(node_id_arg, str_arg);
        }
    } else if (strncmp(cmd_name, "add_block_node", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_block_data(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    } else if (strncmp(cmd_name, "remove_block_node", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_stop_block_data(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    } else if (strncmp(cmd_name, "remove_block_ip", cmd_name_len) == 0) {
        if (json_parse_ip_arg(&ip_arg, json_str, tokens, argument_token_i)) {
            handle_command_remove_ip(SPIN_CMD_REMOVE_BLOCK, &ip_arg, "/etc/spin/block.list");
            node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip_arg);
            if (node) {
                node->is_blocked = 0;
            }
        }
        handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    } else if (strncmp(cmd_name, "add_allow_node", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_allow_data(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
    } else if (strncmp(cmd_name, "remove_allow_node", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_stop_allow_data(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
    } else if (strncmp(cmd_name, "remove_allow_ip", cmd_name_len) == 0) {
        if (json_parse_ip_arg(&ip_arg, json_str, tokens, argument_token_i)) {
            handle_command_remove_ip(SPIN_CMD_REMOVE_EXCEPT, &ip_arg, "/etc/spin/allow.list");
            node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip_arg);
            if (node) {
                node->is_excepted = 0;
            }
        }
        handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
    }
}

void handle_json_command(const char* data) {
    jsmn_parser p;
    // todo: alloc these upon global init, realloc when necessary?
    size_t tok_count = 10;
    jsmntok_t tokens[10];
    int result;

    jsmn_init(&p);
    result = jsmn_parse(&p, data, strlen(data), tokens, 10);
    if (result < 0) {
        spin_log(LOG_ERR, "Error: unable to parse json data: %d\n", result);
        return;
    }
    // token should be object, first child should be "command":
    if (tokens[0].type != JSMN_OBJECT) {
        spin_log(LOG_ERR, "Error: unknown json data\n");
        return;
    }
    // token 1 should be "command",
    // token 2 should be the command name (e.g. "get_filters")
    // token 3 should be "arguments",
    // token 4 should be an object with the arguments (possibly empty)
    if (tokens[1].type != JSMN_STRING || strncmp(data+tokens[1].start, "command", 7) != 0) {
        spin_log(LOG_ERR, "Error: json data not command\n");
        return;
    }
    if (tokens[3].type != JSMN_STRING || strncmp(data+tokens[3].start, "argument", 7) != 0) {
        spin_log(LOG_ERR, "Error: json data does not contain argument field\n");
        return;
    }
    handle_json_command_2(tokens[2].end - tokens[2].start, data+tokens[2].start,
                          data, tokens, 4);
}


// Hook from Mosquitto code called with incoming messages

void do_mosq_message(struct mosquitto* mosq, void* user_data, const struct mosquitto_message* msg) {
    if (strcmp(msg->topic, MQTT_CHANNEL_COMMANDS) == 0) {
        handle_json_command(msg->payload);
    }
}

void connect_mosquitto(const char* host, int port) {
    const char* client_name = "asdf";
    int result;

    if (mosq != NULL) {
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
    }

    mosq = mosquitto_new(client_name, 1, NULL);
    spin_log(LOG_INFO, "Connecting to mqtt server on %s:%d\n", host, port);
    result = mosquitto_connect(mosq, host, port, MOSQUITTO_KEEPALIVE_TIME);
    if (result != 0) {
        spin_log(LOG_ERR, "Error connecting to mqtt server on %s:%d, %s\n", host, port, mosquitto_strerror(result));
        exit(1);
    }
    spin_log(LOG_INFO, "Connected to mqtt server on %s:%d with keepalive value %d\n", host, port, MOSQUITTO_KEEPALIVE_TIME);
    result = mosquitto_subscribe(mosq, NULL, MQTT_CHANNEL_COMMANDS, 0);
    if (result != 0) {
        spin_log(LOG_ERR, "Error subscribing to topic %s: %s\n", MQTT_CHANNEL_COMMANDS, mosquitto_strerror(result));
    }

    mosquitto_message_callback_set(mosq, do_mosq_message);

}

void wf_mosquitto(int data, int timeout) {

    if (data) {
	mosquitto_loop_read(mosq, 1);
    }
    if (timeout) {
	mosquitto_loop_write(mosq, 1);
	mosquitto_loop_misc(mosq);
    }
}

void init_mosquitto(const char* host, int port) {
    mosquitto_lib_init();

    connect_mosquitto(host, port);

    mainloop_register(&wf_mosquitto, mosquitto_socket(mosq), MOSQUITTO_KEEPALIVE_TIME*1000/2);
    mosquitto_socket(mosq);
    send_command_restart();
    handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
}

void finish_mosquitto() {

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}
