#ifndef SOCKS5_H_wL7YxN65ZHqKGvCPrNbPtMJgL8B
#define SOCKS5_H_wL7YxN65ZHqKGvCPrNbPtMJgL8B

#include "selector/selector.h"
#include "buffer/buffer.h"
#include "stateMachine/selectorStateMachine.h"
#include "socks5/socks5SessionDefinition.h"

void socks5_init(char *dnsServerIp);

void socks5_passive_accept(SelectorEvent *event);

void socks5_register_server(FdSelector s, SessionHandlerP socks5_p);

#endif