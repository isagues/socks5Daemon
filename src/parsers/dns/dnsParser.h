#ifndef DNS_PARSER_H_237cf11b918a97e16402b064e7e5af5cd7f70661
#define DNS_PARSER_H_237cf11b918a97e16402b064e7e5af5cd7f70661

#include <stdbool.h>
#include <netinet/in.h>     // in_addr

#include "buffer/buffer.h"
#include "dnsDefs.h"

typedef enum ResponseDnsParserState {
    RESPONSE_DNS_TRANSACTION_STATE,
    RESPONSE_DNS_FLAGS_STATE,
    RESPONSE_DNS_QUESTIONS_HIGH,
    RESPONSE_DNS_QUESTIONS_LOW,
    RESPONSE_DNS_ANSWERS_HIGH,
    RESPONSE_DNS_ANSWERS_LOW,
    RESPONSE_DNS_AUTHORITY,
    RESPONSE_DNS_ADITIONAL,
    RESPONSE_DNS_QUERIES_NAME_FIRST_BYTE,
    RESPONSE_DNS_QUERIES_NAME_REFERENCE_SECOND_BYTE,
    RESPONSE_DNS_QUERIES_NAME_OTHER_BYTES,
    RESPONSE_DNS_QUERIES_TYPE,
    RESPONSE_DNS_QUERIES_CLASS,
    RESPONSE_DNS_ANSWERS_NAME_FIRST_BYTE,
    RESPONSE_DNS_REFERENCE_SECOND_BYTE,
    RESPONSE_DNS_ANSWERS_NAME_OTHER_BYTES,
    RESPONSE_DNS_ANSWERS_TYPE_LOW,
    RESPONSE_DNS_ANSWERS_TYPE_HIGH,
    RESPONSE_DNS_ANSWERS_CLASS,
    RESPONSE_DNS_ANSWERS_TTL,
    RESPONSE_DNS_ANSWERS_DATA_LENGTH_HIGH,
    RESPONSE_DNS_ANSWERS_DATA_LENGTH_LOW,
    RESPONSE_DNS_IPV4_ADDRESS,
    RESPONSE_DNS_IPV6_ADDRESS,
    RESPONSE_DNS_CNAME,
    RESPONSE_DNS_DONE,
    RESPONSE_DNS_ERROR,

} ResponseDnsParserState;

typedef struct IpAddress{
    uint8_t ipType;
    union {
        struct in6_addr ipv6;
        struct in_addr ipv4;
    } addr;
}IpAddress;

typedef struct ResponseDnsParser {
    
    struct IpAddress * addresses;   

    uint8_t totalQuestions;

    uint8_t totalAnswers;

    uint8_t currentAnswers;

    uint8_t bytesWritten;

    uint8_t currentType;

    uint8_t dataLenght;

    uint8_t counter;
    
    enum ResponseDnsParserState currentState;

}ResponseDnsParser;

void response_dns_parser_init(ResponseDnsParser *p);

enum ResponseDnsParserState response_dns_parser_feed(ResponseDnsParser *p, uint8_t b);

bool response_dns_parser_consume(Buffer *buffer, ResponseDnsParser *p, bool *errored);

bool response_dns_parser_is_done(enum ResponseDnsParserState state, bool *errored);

char * response_dns_parser_error_message(enum ResponseDnsParserState state);

#endif
