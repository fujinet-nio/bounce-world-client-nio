#include <stdint.h>
#include "conio.h"

#include "double_buffer.h"

uint8_t is_alt_screen = 0;

void swap_buffer(void)
{
    conio_begin_frame();
    is_alt_screen ^= 1U;
}

void show_other_screen(void)
{
    conio_end_frame();
}
