#
# rvemu
#

CC      = cc
CFLAGS  = -Wall -Wextra -O3 -g $(shell pkg-config --cflags sdl2)
LDFLAGS = $(shell pkg-config --libs sdl2)
SIZE    = size
DTC     = dtc

RVEMU = rvemu
OBJS  = main.o ui.o utils.o cpu.o clint.o plic.o uart.o
DTB   = rv32.dtb

.PHONY: all
all: $(RVEMU) $(DTB)

.PHONY: clean
clean:
	rm -f $(RVEMU) $(OBJS) $(DTB)

.PHONY:
run: $(RVEMU) $(DTB)
	./$(RVEMU) linux-image $(DTB) linux-rootfs

%.dtb: %.dts
	$(DTC) -I dts -O dtb -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(RVEMU): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)
	$(SIZE) $(RVEMU)
