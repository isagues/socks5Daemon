#include <stdbool.h>
#include <string.h>     // memset, memcpy

#include "parser.h"

/* CDT del parser */

struct parser * parser_init(struct parser * p, const unsigned *classes, const struct parser_definition *def) {
    
    if(p == NULL)
        return NULL;
    
    memset(p, 0, sizeof(*p));
    p->classes = classes;
    memcpy(&p->def, def, sizeof(*def));
    p->state   = def->start_state;
    return p;
}

void parser_reset(struct parser *p) {
    p->state   = p->def.start_state;
}

struct parser_event * parser_feed(struct parser *p, uint8_t c) {
    
    const unsigned type = p->classes[c];

    p->e1.next = p->e2.next = 0;

    const struct parser_state_transition *state = p->def.states[p->state];
    const size_t n                              = p->def.states_n[p->state];
    bool matched   = false;

    for(unsigned i = 0; i < n ; i++) {
        const int when = state[i].when;
        if (state[i].when <= 0xFF) {
            matched = (c == when);
        } else if(state[i].when == ANY) {
            matched = true;
        } else { 
            matched = (type & when);
        }

        if(matched) {
            state[i].act1(&p->e1, c);
            if(state[i].act2 != NULL) {
                p->e1.next = &p->e2;
                state[i].act2(&p->e2, c);
            }
            p->state = state[i].dest;
            break;
        }
    }
    return &p->e1;
}

static unsigned classes[0xFF] = {0x00};

unsigned * parser_no_classes(void) {
    return classes;
}
