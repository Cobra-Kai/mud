#ifndef CMD_H
#define CMD_H
int command_register(const char *name, void (*f)(void *p));
int command_run(const char *name, void *p);
#endif
