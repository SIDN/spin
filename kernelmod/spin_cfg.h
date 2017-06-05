
#ifndef SPIN_CONFIG_H
#define SPIN_CONFIG_H 1

typedef enum {
	SPIN_CMD_ADD_IGNORE,
	SPIN_CMD_GET_IGNORE,
	SPIN_CMD_ERR,
	SPIN_CMD_END
} config_command_t;

void config_test(void);

#endif
