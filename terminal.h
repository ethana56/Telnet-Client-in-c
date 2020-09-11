#ifndef TERMINAL_H
#define TERMINAL_H

#include <termios.h>

enum ttystate {default_m, raw_m, reset_m};

typedef struct {
   struct termios last_termios;
   struct termios original_termios;
   int fd;
} terminal_state;

int tty_default_mode(terminal_state *);
int tty_raw_mode(terminal_state *);
int tty_reset(terminal_state *);
int register_tty(int, terminal_state *);
int tty_reset_last_state(terminal_state *);
void tty_atexit(void);

#endif
