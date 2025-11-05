CROSS   = arm-linux-gnueabihf-
CFLAGS  = -I/opt/mini/arm-buildroot-linux-gnueabihf/sysroot/usr/include/SDL2
LDFLAGS = -lSDL2 -lSDL2_gfx -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lGLESv2
SRCS    = $(wildcard *.c)
OBJS    = $(SRCS:c=o)
BINS    = $(SRCS:.c=)

export PATH=/opt/mini/bin:$(shell echo $$PATH)

.PHONY: all
all: $(BINS)

$(BINS): % : %.c
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf $(BINS) $(OBJS)

