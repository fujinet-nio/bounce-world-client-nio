# Independent desktop frontend; no SDL dependency on the terminal target.
CC ?= gcc
PROGRAM ?= bwcn
ifeq ($(FUJINET_NIO_LIB),)
$(error Set FUJINET_NIO_LIB to the fujinet-nio-lib checkout)
endif
ifeq ($(shell pkg-config --exists 'sdl2 >= 2.0.18' SDL2_ttf && echo yes),)
$(error linux-gfx needs pkg-config, SDL2 >= 2.0.18 and SDL2_ttf development packages)
endif
CPPFLAGS += -Isrc/linux-gfx -Isrc/include -Isrc/amiga -I$(FUJINET_NIO_LIB)/include $(shell pkg-config --cflags sdl2 SDL2_ttf)
CFLAGS += -Wall -Wextra -Werror -O2 -std=c99
LDLIBS += $(shell pkg-config --libs sdl2 SDL2_ttf) -lm
SHARED := src/common/add_client_csv.c src/common/appstore_settings.c src/common/shape_decode.c src/common/bwc_interpolation_math.c src/common/embedded_shapes.c src/amiga/vector_outline.c
SOURCES := $(wildcard src/linux-gfx/*.c) $(SHARED)
OBJECTS := $(patsubst src/%.c,obj/linux-gfx/%.o,$(SOURCES))
LIBRARY := $(FUJINET_NIO_LIB)/build/fujinet-nio-linux.a
.DEFAULT_GOAL := all
.PHONY: all test-linux-gfx test-linux-network
all: build/$(PROGRAM).linux-gfx
build/$(PROGRAM).linux-gfx: $(OBJECTS) $(LIBRARY)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LIBRARY) $(LDLIBS)
obj/linux-gfx/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(LIBRARY):
	$(MAKE) -C $(FUJINET_NIO_LIB) linux
-include $(OBJECTS:.o=.d)

test-linux-gfx:
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/host/test_linux_gfx.c src/linux-gfx/ui.c src/linux-gfx/model.c src/linux-gfx/diagnostics.c \
	  src/common/bwc_interpolation_math.c src/common/embedded_shapes.c src/amiga/vector_outline.c \
	  -o build/test_linux_gfx.host $(LDLIBS)
	SDL_VIDEODRIVER=dummy build/test_linux_gfx.host

test-linux-network:
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/host/test_linux_protocol.c src/linux-gfx/protocol.c src/linux-gfx/diagnostics.c \
	  -o build/test_linux_protocol.host $(LDLIBS)
	build/test_linux_protocol.host
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/host/test_linux_network.c \
	  src/linux-gfx/network.c src/linux-gfx/protocol.c src/linux-gfx/model.c src/linux-gfx/diagnostics.c \
	  src/common/add_client_csv.c src/common/shape_decode.c src/common/bwc_interpolation_math.c \
	  -o build/test_linux_network.host $(LDLIBS)
	build/test_linux_network.host
