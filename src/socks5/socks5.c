
#include <stdio.h>
#include <stdlib.h> // calloc
#include <string.h> // memcpy

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>   // socket
#include <sys/socket.h>  // socket
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "socks5.h"
#include "netutils/netutils.h"
#include "stateMachineBuilder/stateMachineBuilder.h"
#include "statistics/statistics.h"
#include "administration/administration.h"

#define DEFAULT_INPUT_BUFFER_SIZE 4096
#define DEFAULT_OUTPUT_BUFFER_SIZE 4096
#define DEFAULT_DNS_BUFFER_SIZE 4096

#define MIN_IO_BUFFER_SIZE 50
#define MAX_IO_BUFFER_SIZE (1*1024*1024)

#define N(x) (sizeof(x)/sizeof((x)[0]))

static Socks5Args * args;

static FdSelector selector;

static FdHandler clientHandler;
static FdHandler serverHandler;
static FdHandler DNSHandler;

static uint32_t sessionInputBufferSize;
static uint32_t sessionOutputBufferSize;
static uint32_t dnsBufferSize;

static double maxSessionInactivity;

static SessionHandlerP socks5_session_init(void);
static void socks5_server_read(SelectorEvent *event);
static void socks5_server_write(SelectorEvent *event);
static void socks5_server_close(SelectorEvent *event);
static void socks5_dns_read(SelectorEvent *event);
static void socks5_dns_write(SelectorEvent *event);
static void socks5_dns_close(SelectorEvent *event);
static void socks5_client_read(SelectorEvent *event);
static void socks5_client_write(SelectorEvent *event);
static void socks5_client_close(SelectorEvent *event);
static void socks5_close_session(SelectorEvent *event);
static int socks5_accept_connection(int passiveFd, struct sockaddr *cli_addr, socklen_t *clilen);
static void socks5_cleanup_session(SelectorEvent *event, void *maxSessionInactivityParam);
static void socks5_close_user_sessions_util(SelectorEvent *event, void *userParam);

void socks5_init(Socks5Args *argsParam, double maxSessionInactivityParam, FdSelector selectorParam) {

    args = argsParam;
    maxSessionInactivity = maxSessionInactivityParam;
    selector = selectorParam;

    sessionInputBufferSize = DEFAULT_INPUT_BUFFER_SIZE;
    sessionOutputBufferSize = DEFAULT_OUTPUT_BUFFER_SIZE;
    dnsBufferSize = DEFAULT_DNS_BUFFER_SIZE;

    clientHandler.handle_read = socks5_client_read;
    clientHandler.handle_write = socks5_client_write;
    clientHandler.handle_close = socks5_client_close;
    clientHandler.handle_block = NULL;

    serverHandler.handle_read = socks5_server_read;
    serverHandler.handle_write = socks5_server_write;
    serverHandler.handle_close = socks5_server_close;
    serverHandler.handle_block = NULL;

    DNSHandler.handle_read = socks5_dns_read;
    DNSHandler.handle_write = socks5_dns_write;
    DNSHandler.handle_close = socks5_dns_close;
    DNSHandler.handle_block = NULL;

    socks5_session_state_machine_builder_init();
}

uint32_t socks5_get_io_buffer_size(void) {
    return sessionInputBufferSize;
}

bool socks5_set_io_buffer_size(uint32_t size) {

    if(size < MIN_IO_BUFFER_SIZE || size > MAX_IO_BUFFER_SIZE) {
        return false;
    }

    sessionInputBufferSize = size;
    sessionOutputBufferSize = size;

    return true;
}

uint8_t socks5_get_max_session_inactivity(void) {
    return maxSessionInactivity;
}

bool socks5_set_max_session_inactivity(uint8_t seconds) {

    if(seconds < 1) {
        return false;
    }

    maxSessionInactivity = seconds;

    return true;
}

uint8_t socks5_get_selector_timeout(void) {
    return selector_get_timeout(selector);
}

bool socks5_update_selector_timeout(time_t timeout) {

    if(timeout < 1 || timeout > 255) {
        return false;
    }

    selector_update_timeout(selector, timeout);

    return true;
}

static int socks5_accept_connection(int passiveFd, struct sockaddr *cli_addr, socklen_t *clilen) {

    int fd;

    do {
        fd = accept(passiveFd, cli_addr, clilen);
    } while(fd < 0 && (errno == EINTR));

    return fd;
}

void socks5_passive_accept_ipv4(SelectorEvent *event){
    
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    int fd = socks5_accept_connection(event->fd, (struct sockaddr *)&cli_addr, &clilen);

    if(fd < 0) {
        return;
    }

    SessionHandlerP session = socks5_session_init();
    if(session == NULL) {
        close(fd);
        return;
    }

    session->clientConnection.fd = fd;

    memcpy(&session->clientConnection.addr, (struct sockaddr *)&cli_addr, clilen);

    selector_register(event->s, session->clientConnection.fd, &clientHandler, OP_READ, session);
}

void socks5_passive_accept_ipv6(SelectorEvent *event){
    
    struct sockaddr_in6 cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    int fd = socks5_accept_connection(event->fd, (struct sockaddr *)&cli_addr, &clilen);

    if(fd < 0) {
        return;
    }

    SessionHandlerP session = socks5_session_init();
    if(session == NULL) {
        close(fd);
        return;
    }

    session->clientConnection.fd = fd;

    memcpy(&session->clientConnection.addr, (struct sockaddr *)&cli_addr, clilen);

    selector_register(event->s, session->clientConnection.fd, &clientHandler, OP_READ, session);
}

void socks5_register_server(SessionHandlerP session){

    session->serverConnection.state = OPEN;
    statistics_inc_current_connection();

    selector_register(selector, session->serverConnection.fd, &serverHandler, OP_WRITE, session);
}

void socks5_register_dns(SessionHandlerP session){

    if(session->dnsHeaderContainer->ipv4.dnsConnection.state == OPEN) {
        statistics_inc_current_connection();
        selector_register(selector, session->dnsHeaderContainer->ipv4.dnsConnection.fd, &DNSHandler, OP_WRITE, session);
    }

    if(session->dnsHeaderContainer->ipv6.dnsConnection.state == OPEN) {
        statistics_inc_current_connection();
        selector_register(selector, session->dnsHeaderContainer->ipv6.dnsConnection.fd, &DNSHandler, OP_WRITE, session);
    }
}

Socks5Args *socks5_get_args(void){
    return args;
}

static void socks5_server_read(SelectorEvent *event){
    SessionHandlerP session = (SessionHandlerP) event->data;

    session->lastInteraction = time(NULL);

    Buffer * buffer = &session->output;
    unsigned state;

    if(!buffer_can_write(buffer)) {

        socks5_close_session(event);
        return;
    }

    ssize_t readBytes;
    size_t nbytes;
    uint8_t * writePtr = buffer_write_ptr(buffer, &nbytes);


    if(readBytes = recv(event->fd, writePtr, nbytes, MSG_NOSIGNAL), readBytes >= 0) {
        buffer_write_adv(buffer, readBytes);

        if(readBytes == 0) {

            if(selector_state_machine_state(&session->sessionStateMachine) < FORWARDING) {
                socks5_close_session(event);
                return;
            }

            session->serverConnection.state = CLOSING;
        }

        statistics_add_bytes_received(readBytes);

        if(selector_state_machine_state(&session->sessionStateMachine) == FORWARDING) {
            session->socksHeader.spoofingHeader.bytesRead = (size_t) readBytes;
        }
            
        if(state = selector_state_machine_proccess_read(&session->sessionStateMachine, event), state == FINISH)
            socks5_close_session(event);
    }

    else {
        if(errno != EINTR) {
            socks5_close_session(event);
        }
    }
}

static void socks5_server_write(SelectorEvent *event){
    SessionHandlerP session = (SessionHandlerP) event->data;

    session->lastInteraction = time(NULL);

    Buffer * buffer = &session->input;
    unsigned state;

    if(!buffer_can_read(buffer)) {

        if(state = selector_state_machine_proccess_write(&session->sessionStateMachine, event), state == FINISH)
            socks5_close_session(event);

        return;
    }
    
    ssize_t writeBytes;
    size_t nbytes;
    uint8_t * readPtr = buffer_read_ptr(buffer, &nbytes);
    
    if(writeBytes = send(event->fd, readPtr, nbytes, MSG_NOSIGNAL), writeBytes > 0) {
        buffer_read_adv(buffer, writeBytes);

        statistics_add_bytes_sent(writeBytes);

        if(state = selector_state_machine_proccess_write(&session->sessionStateMachine, event), state == FINISH)
            socks5_close_session(event);
    }

    else {
        if(errno != EINTR) {
            socks5_close_session(event);
        }
    }
}

static void socks5_client_read(SelectorEvent *event){
    SessionHandlerP session = (SessionHandlerP) event->data;

    session->lastInteraction = time(NULL);

    Buffer * buffer = &session->input;
    unsigned state;

    if(!buffer_can_write(buffer)) {
        socks5_close_session(event);
        return;
    }

    ssize_t readBytes;
    size_t nbytes;
    uint8_t * writePtr = buffer_write_ptr(buffer, &nbytes);

    if(readBytes = recv(event->fd, writePtr, nbytes, MSG_NOSIGNAL), readBytes >= 0) {
        buffer_write_adv(buffer, readBytes);

        if(readBytes == 0) {

            if(selector_state_machine_state(&session->sessionStateMachine) < FORWARDING) {
                socks5_close_session(event);
                return;
            }

            session->clientConnection.state = CLOSING;
        }

        statistics_add_bytes_received(readBytes);

        if(selector_state_machine_state(&session->sessionStateMachine) == FORWARDING) {
            session->socksHeader.spoofingHeader.bytesRead = (size_t) readBytes;
        }

        if(state = selector_state_machine_proccess_read(&session->sessionStateMachine, event), state == FINISH)
            socks5_close_session(event);
    }

    else {
        if(errno != EINTR) {
            socks5_close_session(event);
        }
    }
   
}

static void socks5_client_write(SelectorEvent *event){
    SessionHandlerP session = (SessionHandlerP) event->data;

    session->lastInteraction = time(NULL);

    Buffer * buffer = &session->output;
    unsigned state;

    if(!buffer_can_read(buffer)) {

        if(state = selector_state_machine_proccess_write(&session->sessionStateMachine, event), state == FINISH){
            socks5_close_session(event);
        }

        return;
    }
    
    ssize_t writeBytes;
    size_t nbytes;
    uint8_t * readPtr = buffer_read_ptr(buffer, &nbytes);
    
    if(writeBytes = send(event->fd, readPtr, nbytes, MSG_NOSIGNAL), writeBytes > 0){
        buffer_read_adv(buffer, writeBytes);

        statistics_add_bytes_sent(writeBytes);

        if(state = selector_state_machine_proccess_write(&session->sessionStateMachine, event), state == FINISH){
            socks5_close_session(event);
        }
    }

    else {
        if(errno != EINTR) {
            socks5_close_session(event);
        }
    }
}

static void socks5_dns_read(SelectorEvent *event){
    
    SessionHandlerP session = (SessionHandlerP) event->data;

    session->lastInteraction = time(NULL);

    DnsHeader *header;

    if(event->fd == session->dnsHeaderContainer->ipv4.dnsConnection.fd) {
        header = &session->dnsHeaderContainer->ipv4;
    }

    else {
        header = &session->dnsHeaderContainer->ipv6;
    }

    Buffer * buffer = &header->buffer;

    if(!buffer_can_write(buffer)) {
        socks5_close_session(event);
        return;
    }

    ssize_t readBytes;
    size_t nbytes;
    uint8_t * writePtr = buffer_write_ptr(buffer, &nbytes);

    readBytes = recv(event->fd, writePtr, nbytes, MSG_NOSIGNAL);

    if(readBytes > 0) {
        buffer_write_adv(buffer, readBytes);
        statistics_add_bytes_received(readBytes);
    }

    if(readBytes == 0 || (readBytes == -1 && errno != EINTR)) {

        // Unexpected DNS Close
        selector_unregister_fd(event->s, event->fd);
    }

    selector_state_machine_proccess_read(&session->sessionStateMachine, event);
}

static void socks5_dns_write(SelectorEvent *event){
    
    SessionHandlerP session = (SessionHandlerP) event->data;

    session->lastInteraction = time(NULL);

    DnsHeader *header;

    if(event->fd == session->dnsHeaderContainer->ipv4.dnsConnection.fd) {
        header = &session->dnsHeaderContainer->ipv4;
    }

    else {
        header = &session->dnsHeaderContainer->ipv6;
    }

    Buffer * buffer = &header->buffer;

    // ! TODO no me gusta este estado
    if(!buffer_can_read(buffer)) {
        
        if(!header->connected){
            selector_state_machine_proccess_write(&session->sessionStateMachine, event);
            return;
        }
        // Podria no ser necesario
        socks5_close_session(event);
        return;
    }
    
    ssize_t writeBytes;
    size_t nbytes;
    uint8_t * readPtr = buffer_read_ptr(buffer, &nbytes);
    
    if(writeBytes = send(event->fd, readPtr, nbytes, MSG_NOSIGNAL), writeBytes > 0) {
        buffer_read_adv(buffer, writeBytes);

        statistics_add_bytes_sent(writeBytes);

        selector_state_machine_proccess_write(&session->sessionStateMachine, event);
    }

    else {
        if(errno != EINTR) {

            // Unexpected DNS Close
            selector_unregister_fd(event->s, event->fd);

            selector_state_machine_proccess_write(&session->sessionStateMachine, event);
        }
    }
}

static SessionHandlerP socks5_session_init(void) {

    SessionHandlerP session = calloc(1, sizeof(*session));
    if(session == NULL){
        return NULL;
    }

    uint8_t *inputBuffer = malloc(sessionInputBufferSize*sizeof(*inputBuffer));
    if(inputBuffer == NULL){
        free(session);
        return NULL;
    }
        
    uint8_t *outputBuffer = malloc(sessionOutputBufferSize*sizeof(*outputBuffer));
    if(outputBuffer == NULL){
        free(session);
        free(inputBuffer);
        return NULL;
    }

    session->sessionType = SOCKS5_CLIENT_SESSION;

    buffer_init(&session->input, sessionInputBufferSize, inputBuffer);
    buffer_init(&session->output, sessionOutputBufferSize, outputBuffer);

    build_socks_session_state_machine(&session->sessionStateMachine);

    session->clientInfo.connectedDomain = NULL;
    session->dnsHeaderContainer = NULL;

    session->clientConnection.state = OPEN;
    session->serverConnection.state = INVALID;

    session->lastInteraction = time(NULL);

    statistics_inc_current_connection();

    return session;
}

static void socks5_client_close(SelectorEvent *event){
    
     SessionHandlerP session = (SessionHandlerP) event->data;
    
    if(session->serverConnection.state != INVALID) {
        selector_unregister_fd(event->s, session->serverConnection.fd);
    }

    if(session->dnsHeaderContainer != NULL) {
        
        if(session->dnsHeaderContainer->ipv4.dnsConnection.state != INVALID) {
            selector_unregister_fd(event->s, session->dnsHeaderContainer->ipv4.dnsConnection.fd);
        }
        
        if(session->dnsHeaderContainer->ipv6.dnsConnection.state != INVALID) {
            selector_unregister_fd(event->s, session->dnsHeaderContainer->ipv6.dnsConnection.fd);
        }

        if(session->dnsHeaderContainer->ipv4.buffer.data != NULL) {
            free(session->dnsHeaderContainer->ipv4.buffer.data);
            session->dnsHeaderContainer->ipv4.buffer.data = NULL;
        }

        if(session->dnsHeaderContainer->ipv4.responseParser.addresses != NULL) {
            free(session->dnsHeaderContainer->ipv4.responseParser.addresses);
            session->dnsHeaderContainer->ipv4.responseParser.addresses = NULL;
        }

        if(session->dnsHeaderContainer->ipv6.buffer.data != NULL) {
            free(session->dnsHeaderContainer->ipv6.buffer.data);
            session->dnsHeaderContainer->ipv6.buffer.data = NULL;
        }

        if(session->dnsHeaderContainer->ipv6.responseParser.addresses != NULL) {
            free(session->dnsHeaderContainer->ipv6.responseParser.addresses);
            session->dnsHeaderContainer->ipv6.responseParser.addresses = NULL;
        }

        free(session->dnsHeaderContainer);
        session->dnsHeaderContainer = NULL;
    }

    selector_state_machine_close(&session->sessionStateMachine, event);

    if(session->clientInfo.user != NULL) {
        session->clientInfo.user->connectionCount--;

        if(session->clientInfo.user->connectionCount == 0) {
            statistics_dec_current_user_count();
        }

        free(session->clientInfo.connectedDomain);
    } 

    close(session->clientConnection.fd);
    statistics_dec_current_connection();

    free(session->input.data);
    free(session->output.data);
    free(session);
}

static void socks5_server_close(SelectorEvent *event) {
    
    SessionHandlerP session = (SessionHandlerP) event->data;
    session->serverConnection.state = INVALID;

    close(event->fd);

    statistics_dec_current_connection();
}

static void socks5_dns_close(SelectorEvent *event) {
    
    SessionHandlerP session = (SessionHandlerP) event->data;

    close(event->fd);
    
    statistics_dec_current_connection();
    
    if(session->dnsHeaderContainer->ipv4.dnsConnection.fd == event->fd) {
        session->dnsHeaderContainer->ipv4.dnsConnection.state = INVALID;
    }

    else {
        session->dnsHeaderContainer->ipv6.dnsConnection.state = INVALID;
    }
    
}

static void socks5_close_session(SelectorEvent *event) {
    
    SessionHandlerP session = (SessionHandlerP) event->data;
    
    selector_unregister_fd(event->s, session->clientConnection.fd);
}

void socks5_selector_cleanup(void) {
    selector_fd_cleanup(selector, socks5_cleanup_session, (void*) &maxSessionInactivity);
}

static void socks5_cleanup_session(SelectorEvent *event, void *maxSessionInactivityParam) {

    // Socket pasivo
    if(event->data == NULL) {
        return;
    }

    double sessionInactivityThreshold = *((double*)maxSessionInactivityParam);

    AbstractSession *absSession = (AbstractSession*) event->data;

    if(absSession->sessionType == SOCKS5_CLIENT_SESSION) {

        SessionHandlerP session = (SessionHandlerP) absSession;

        if(event->fd == session->clientConnection.fd && difftime(time(NULL), session->lastInteraction) >= sessionInactivityThreshold) {
        
            socks5_close_session(event);
        }
    }

    // If absSession is SOCKS5_ADMINISTRATIVE_SESSION
        // Don't Clean Up
}

void socks5_close_user_sessions(UserInfoP user) {
    selector_fd_cleanup(selector, socks5_close_user_sessions_util, (void*) user);
}

static void socks5_close_user_sessions_util(SelectorEvent *event, void *userParam) {

    // Socket pasivo
    if(event->data == NULL) {
        return;
    }

    UserInfoP user = (UserInfoP) userParam;

    AbstractSession *absSession = (AbstractSession*) event->data;

    if(absSession->sessionType == SOCKS5_CLIENT_SESSION) {

        SessionHandlerP session = (SessionHandlerP) absSession;
    
        if(event->fd == session->clientConnection.fd && session->clientInfo.user == user) {
        
            socks5_close_session(event);
        }
    }

    else if(absSession->sessionType == SOCKS5_ADMINISTRATION_SESSION) {

        AdministrationSessionP session = (AdministrationSessionP) absSession;

        if(session->user == user) {

            admin_close_session(event);
        }
    }
}
