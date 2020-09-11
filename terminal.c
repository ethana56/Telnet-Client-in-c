#include <stdlib.h>
#include <termios.h>
#include <errno.h>
#include <stdio.h>
#include "terminal.h"

#define EXPAND 2

int tty_default_mode(terminal_state *state) {
   struct termios buf; 
   if (tty_reset(state) < 0) {
      return -1;
   }
   if (tcgetattr(state->fd, &buf) < 0) {
      return -1;
   }
   buf.c_lflag &= ~ISIG;
   if (tcsetattr(state->fd, TCSAFLUSH, &buf) < 0) {
      return -1;
   }
   return 0;
}

int tty_raw_mode(terminal_state *state) {
   int err;
   struct termios buf;
 
   if (tty_reset(state) < 0) {
      return -1;
   }
   if (tcgetattr(state->fd, &buf) < 0) {
      return -1;
   }
   buf.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
   buf.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
   buf.c_cflag &= ~(CSIZE | PARENB);
   buf.c_cflag |= CS8;
   buf.c_oflag &= ~(OPOST);

   buf.c_cc[VMIN] = 1;
   buf.c_cc[VTIME] = 0;
   if (tcsetattr(state->fd, TCSAFLUSH, &buf) < 0) {
      return -1;
   }
   if (tcgetattr(state->fd, &buf) < 0) {
      err = errno;
      tcsetattr(state->fd, TCSAFLUSH, &state->original_termios);
      errno = err;
      return -1;
   }
   if ((buf.c_lflag & (ECHO | ICANON | IEXTEN | ISIG)) ||
       (buf.c_iflag & (BRKINT | ICRNL | INPCK | ISTRIP | IXON)) ||
       (buf.c_cflag & (CSIZE | PARENB | CS8)) != CS8 ||
       (buf.c_oflag & OPOST) || buf.c_cc[VMIN] != 1 ||
       buf.c_cc[VTIME] != 0) {
      
      tcsetattr(state->fd, TCSAFLUSH, &(state->original_termios));
      errno = EINVAL;
      return -1;
   }
   return 0;
}

int tty_reset(terminal_state *state) {
   if (tcgetattr(state->fd, &state->last_termios) < 0) {
      return -1;
   }
   if (tcsetattr(state->fd, TCSAFLUSH, &(state->original_termios)) < 0) {
      return -1;
   }
   return 0;
}

int tty_reset_last_state(terminal_state *state) {
      fprintf(stderr, "ECHO: %d\n", state->last_termios.c_lflag & ECHO);
   struct termios buf;
   if (tcsetattr(state->fd, TCSAFLUSH, &state->last_termios) < 0) {
      return -1;
   }
   tcgetattr(state->fd, &buf);
   fprintf(stderr, "ECHO: %d\n", buf.c_lflag & ECHO);
   return 0;
}

/*int tty_reset_last_state(terminal_state *state) {
   struct termios last_termios = state->last_termios;
   if (tty_reset(state) < 0) {
      return -1;
   }
   if (tcsetattr(state->fd, TCSAFLUSH, &last_termios) < 0) {
      return -1;
   }
   return 0; 
}*/

int register_tty(int fd, terminal_state *state) {
   if (tcgetattr(fd, &(state->original_termios)) < 0) {
      return -1;
   }
   state->fd = fd;
   state->last_termios = state->original_termios;
   return 0;
}

































