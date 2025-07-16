#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "emu.h"

#define MEMOFF    0x80000000
#define MEMSIZE   (32 * 1024 * 1024)

#define UARTOFF   0x10000000
#define UARTSIZE  0x00000100

#define CLINTOFF  0x11000000
#define CLINTSIZE 0x00010000

#define SYSCONOFF  0x12000000
#define SYSCONSIZE 0x00000004

/* system devices */
static struct cpu   *cpu;
static uint8_t      *ram;
static struct clint *clint;
static struct uart  *uart;
static uint32_t      syscon;

/* system controller callbacks */
static int syscon_read32(uint32_t addr, uint32_t *data)
{
	*data = 0;
	info("syscon: read %08x", addr);
	return 0;
}

static int syscon_write32(uint32_t addr, uint32_t data)
{
	switch (data) {
	case 0xACDC0FF0: /* AC/DC OFF */
		syscon = 1;
		break;

	case 0xC01DB001: /* COLD BOOT */
		syscon = 2;
		break;

	default:
		warn("syscon: invalid value %08x", data);
		return 1;
	}

	info("syscon: write %08x data %08x", addr, data);
	return 0;
}

/* system bus callbacks */
int bus_read32(uint32_t addr, uint32_t *data)
{
	if (addr >= MEMOFF && addr < MEMOFF + MEMSIZE - 3) {
		*data = *(uint32_t *)&ram[addr - MEMOFF];
		return 0;
	}

	if (addr >= CLINTOFF && addr < CLINTOFF + CLINTSIZE - 3)
		return clint_read32(clint, addr - CLINTOFF, data);

	if (addr >= SYSCONOFF && addr < SYSCONOFF + SYSCONSIZE - 3)
		return syscon_read32(addr - SYSCONOFF, data);

	warn("bus: invalid read32 from %08x", addr);
	return 1;
}

int bus_write32(uint32_t addr, uint32_t data)
{
	if (addr >= MEMOFF && addr < MEMOFF + MEMSIZE - 3) {
		*(uint32_t *)&ram[addr - MEMOFF] = data;
		return 0;
	}

	if (addr >= CLINTOFF && addr < CLINTOFF + CLINTSIZE - 3)
		return clint_write32(clint, addr - CLINTOFF, data);

	if (addr >= SYSCONOFF && addr < SYSCONOFF + SYSCONSIZE - 3)
		return syscon_write32(addr - SYSCONOFF, data);

	warn("bus: invalid write32 to %08x data %08x", addr, data);
	return 1;
}

int bus_read16(uint32_t addr, uint16_t *data)
{
	if (addr >= MEMOFF && addr < MEMOFF + MEMSIZE - 1) {
		*data = *(uint16_t *)&ram[addr - MEMOFF];
		return 0;
	}

	warn("bus: invalid read16 from %08x", addr);
	return 1;
}

int bus_write16(uint32_t addr, uint16_t data)
{
	if (addr >= MEMOFF && addr < MEMOFF + MEMSIZE - 1) {
		*(uint16_t *)&ram[addr - MEMOFF] = data;
		return 0;
	}

	warn("bus: invalid write16 to %08x data %04x", addr, data);
	return 1;
}

int bus_read8(uint32_t addr, uint8_t *data)
{
	if (addr >= MEMOFF && addr < MEMOFF + MEMSIZE) {
		*data = *(uint8_t *)&ram[addr - MEMOFF];
		return 0;
	}

	if (addr >= UARTOFF && addr < UARTOFF + UARTSIZE)
		return uart_read8(uart, addr - UARTOFF, data);

	warn("bus: invalid read8 from %08x", addr);
	return 1;
}

int bus_write8(uint32_t addr, uint8_t data)
{
	if (addr >= MEMOFF && addr < MEMOFF + MEMSIZE) {
		*(uint8_t *)&ram[addr - MEMOFF] = data;
		return 0;
	}

	if (addr >= UARTOFF && addr < UARTOFF + UARTSIZE)
		return uart_write8(uart, addr - UARTOFF, data);

	warn("bus: invalid write8 to %08x data %02x", addr, data);
	return 1;
}

/* load file into physical memory */
static void load(const char *type,
	const char *file, size_t offset, size_t limit)
{
	size_t start = offset - MEMOFF, end = limit - MEMOFF;
	size_t sz, pos;
	FILE *fh;

	if (!(fh = fopen(file, "rb")))
		fail("load: cannot open %s", file);

	pos = start;
	while ((sz = fread(&ram[pos], 1, 2048, fh))) {
		pos += sz;
		if (pos > end)
			fail("load: image too large");
	}

	if (ferror(fh))
		fail("load: failed to load %s", file);

	fclose(fh);

	info("load: loaded %s (%u KiB at 0x%08x) successfully",
		type, (unsigned int)((pos - start + 1023) / 1024), offset);

	return;
}

/* main program */
int main(int argc, char **argv)
{
	static const size_t dtboff = MEMOFF + MEMSIZE - 2048;
	static const size_t rdoff  = MEMOFF + MEMSIZE - 1024*1024;

	/* initialize peripherals */
	if (!(cpu = cpu_init()))
		fail("cpu: cannot initialize");
	if (!(ram = malloc(MEMSIZE)))
		fail("ram: cannot allocate");
	if (!(clint = clint_init(cpu)))
		fail("clint: cannot initialize");
	if (!(uart = uart_init(cpu)))
		fail("uart: cannot initialize");

	/* load memory */
res:	memset(ram, 0, MEMSIZE);
	if (argc > 1) load("image",  argv[1], MEMOFF, rdoff);
	if (argc > 2) load("dtb",    argv[2], dtboff, MEMOFF+MEMSIZE);
	if (argc > 3) load("initrd", argv[3], rdoff,  dtboff);

	/* reset cpu */
	cpu_reset(cpu, MEMOFF);
	cpu_reg_set(cpu, REG_A0, cpu_csr_get(cpu, CSR_MHARTID));
	cpu_reg_set(cpu, REG_A1, dtboff);

	/* main loop */
	info("cpu: start");
	syscon = 0;
	while (syscon == 0) {
		/* tick hardware */
		clint_tick(clint);
		uart_tick(uart);

		/* run cpu */
		switch (cpu_run(cpu, 1024)) {
		case 0: /* nothing */ break;
		case 1: usleep(100);  break;
		}
	}
	if (syscon == 2)
		goto res;

	/* free peripherals */
	info("cpu: shutdown");
	uart_free(uart);
	clint_free(clint);
	free(ram);
	cpu_free(cpu);

	return 0;
}
