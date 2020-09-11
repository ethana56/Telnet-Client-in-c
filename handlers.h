#ifndef HANDLERS_H
#define HANDLERS_H

#include "terminal.h"

#define TELOPT_BINTRANS    0
#define TELOPT_ECHO        1
#define TELOPT_SUPPGA      3
#define TELOPT_TERMTYPE   24
#define TELOPT_NAWS       31
#define TELOPT_TERMSPEED  32
#define TELOPT_NEWENVIRON 39

#define IS 0

struct opt_handler *new_echo_opt_handler(terminal_state *);
struct opt_handler *new_naws_opt_handler(void(*)(int));
struct opt_handler *new_termtype_opt_handler(void);
#endif
