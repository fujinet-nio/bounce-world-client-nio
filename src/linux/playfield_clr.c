#include <conio.h>
#include "data.h"
#include "screen.h"

void playfield_clr(void)
{
    conio_clear_rows(is_showing_info ? SCREEN_HEIGHT - 2 : SCREEN_HEIGHT);
}
