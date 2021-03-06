#ifndef CLIENT_COMMAND_CONTROLLER_H_00180a6350a1fbe79f133adf0a96eb6685c242b6
#define CLIENT_COMMAND_CONTROLLER_H_00180a6350a1fbe79f133adf0a96eb6685c242b6

#include <stdbool.h>

typedef struct CommandController {
	bool (*sender)(int);
	bool (*receiver)(int);	
} CommandController;

void client_command_controller_init(CommandController controllers[], char *descriptions[]);

#endif
