#ifndef TRANSFER_MOCK_LIBDRAGON_H
#define TRANSFER_MOCK_LIBDRAGON_H
#include <stdint.h>
#define JOYPAD_PORT_1 0
#define JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK 1
#define JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK 2
#define TICKS_PER_SECOND 1000000
void joypad_poll(void);
int joypad_is_connected(int);
int joypad_get_accessory_type(int);
int joybus_accessory_read(int,uint16_t,uint8_t *);
int joybus_accessory_write(int,uint16_t,const uint8_t *);
uint64_t get_ticks(void);
#endif
