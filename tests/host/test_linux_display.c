#include <stdio.h>
#include <string.h>
#include "conio.h"
#include "data.h"
#include "display.h"

void show_clients(void) {}
void broadcast(void) {}

int main(int argc, char **argv)
{
    unsigned i;
    if (argc > 1 && !strcmp(argv[1], "--input")) {
        cursor(0);
        (void)cgetc();
        return 0;
    }
    strcpy(name, "PILOT");
    name_pad = 4;
    world_width = 80;
    world_height = 24;
    is_showing_info = true;
    for (i = 0; i < 12; ++i) {
        show_screen();
        putchar(30); /* capture boundary, not a terminal operation */
        fflush(stdout);
    }
    is_showing_info = false;
    info_display_count = 0;
    show_screen();
    putchar(30);
    is_showing_info = true;
    info_display_count = 0;
    show_screen();
    putchar(30);
    return 0;
}
