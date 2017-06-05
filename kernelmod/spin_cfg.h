
#ifndef SPIN_CONFIG_H
#define SPIN_CONFIG_H 1

typedef enum {
	// commands from client to kernelmod
	SPIN_CMD_GET_IGNORE,
	SPIN_CMD_ADD_IGNORE,
	SPIN_CMD_REMOVE_IGNORE,
	SPIN_CMD_CLEAR_IGNORE,
	SPIN_CMD_GET_BLOCK,
	SPIN_CMD_ADD_BLOCK,
	SPIN_CMD_REMOVE_BLOCK,
	SPIN_CMD_CLEAR_BLOCK,
	SPIN_CMD_GET_EXCEPT,
	SPIN_CMD_ADD_EXCEPT,
	SPIN_CMD_REMOVE_EXCEPT,
	SPIN_CMD_CLEAR_EXCEPT,
	// commands from kernelmod to client
	SPIN_CMD_IP,
	SPIN_CMD_ERR,
	SPIN_CMD_END
} config_command_t;

void config_test(void);

#endif
