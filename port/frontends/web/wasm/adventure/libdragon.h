#ifndef WEB_LIBDRAGON_H
#define WEB_LIBDRAGON_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int joypad_port_t;
#define JOYPAD_PORT_1 0
#define JOYPAD_PORT_2 1
bool joypad_is_connected(joypad_port_t port);
uint64_t get_ticks(void);
void rspq_wait(void);
#define debugf(...) ((void)0)
#endif
