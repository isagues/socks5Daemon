#ifndef USER_HANDLER_H_a7f0b7011d5bc49decb646a7852c4531c07e17b5
#define USER_HANDLER_H_a7f0b7011d5bc49decb646a7852c4531c07e17b5

#include <stdbool.h>
#include <stdint.h>

#define ANONYMOUS_USER_CREDENTIALS "anonymous"
#define MAX_USER_COUNT 255
#define MAX_CREDENTIAL_SIZE 255
typedef struct UserInfo {

    char username[MAX_CREDENTIAL_SIZE];
    char password[MAX_CREDENTIAL_SIZE];

    bool admin;

    uint16_t connectionCount; 

} UserInfo;

typedef UserInfo * UserInfoP;

void user_handler_init(void);

bool user_handler_user_exists(char *username, bool *admin);

UserInfoP user_handler_get_user_by_username(char *username);

UserInfoP user_handler_add_user(char *username, char *password, bool admin);

bool user_handler_delete_user(char *username);

void user_handler_destroy(void);

uint8_t user_handler_get_total_users(void);

uint8_t user_handler_get_all_users(UserInfo output[]);

#endif
