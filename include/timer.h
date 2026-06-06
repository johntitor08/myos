#ifndef TIMER_H
#define TIMER_H

#include "stdint.h"

void     timer_init(void);
uint32_t timer_get_ticks(void);
void     timer_wait(uint32_t ms);

#endif
