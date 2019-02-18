#ifdef notdef

#ifndef SPIN_CONFIG_H
#define SPIN_CONFIG_H 1

typedef enum {
	// commands from client to kernelmod
	SPIN_CMD_GET_IGNORE = 1,
	SPIN_CMD_ADD_IGNORE = 2,
	SPIN_CMD_REMOVE_IGNORE = 3,
	SPIN_CMD_CLEAR_IGNORE = 4,
	SPIN_CMD_GET_BLOCK = 5,
	SPIN_CMD_ADD_BLOCK = 6,
	SPIN_CMD_REMOVE_BLOCK = 7,
	SPIN_CMD_CLEAR_BLOCK = 8,
	SPIN_CMD_GET_EXCEPT = 9,
	SPIN_CMD_ADD_EXCEPT = 10,
	SPIN_CMD_REMOVE_EXCEPT = 11,
	SPIN_CMD_CLEAR_EXCEPT = 12,
	// commands from kernelmod to client
	SPIN_CMD_IP = 100,
	SPIN_CMD_END = 200,
	SPIN_CMD_ERR = 250
} config_command_t;

void config_test(void);

#endif

#endif
