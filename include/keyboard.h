#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_init(void);
char keyboard_getchar(void);
int  keyboard_readline(char *buf, int max_len);

#endif
