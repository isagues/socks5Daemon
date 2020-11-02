#include "helloError.h"

static int hello_error_marshall(Buffer *b, uint8_t *bytes){

        while(*bytes < HELLO_ERROR_RESPONSE_SIZE && buffer_can_write(b)){
            if(*bytes == 0){
                buffer_write(b, SOCKS_VERSION);
            }
            if(*bytes == 1){
                buffer_write(b, NO_ACCEPTABLE_METHODS);
            }
            *bytes++;
        }
    }

unsigned hello_error_on_pre_write(struct selector_key *key){
    
    Socks5HandlerP socks5_p = (Socks5HandlerP) key->data;

    hello_error_marshall(&socks5_p->output, socks5_p->socksHeader.helloHeader.bytes);  
    
    return socks5_p->stm.current; 

}

unsigned auth_successful_on_post_write(struct selector_key *key){

    Socks5HandlerP socks5_p = (Socks5HandlerP) key->data;

    if (socks5_p->socksHeader.helloHeader.bytes == HELLO_ERROR_RESPONSE_SIZE && buffer_can_read(&socks5_p->output))
    {
        selector_unregister_fd(key->s, key->fd);
        return FINNISH;
    }
    return socks5_p->stm.current;

}