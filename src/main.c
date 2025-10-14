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

#define DISPLAYOFF  0x13000000
#define DISPLAYSIZE 0x02000000

#define PLICOFF  0x20000000
#define PLICSIZE 0x04000000

/* user interface */
static struct ui *ui;

/* system devices */
static struct cpu   *cpus[4];
static uint8_t      *ram;
static struct clint *clint;
static struct uart  *uart;
static uint32_t      syscon;
static struct plic  *plic;
static struct display *display;

/* system controller callbacks */
static int syscon_read32(void *ctx, uint32_t addr, uint32_t *data)
{
	uint32_t *syscon = (uint32_t *)ctx;

	*data = *syscon;
	info("syscon: read %08x", addr);
	return 0;
}

static int syscon_write32(void *ctx, uint32_t addr, uint32_t data)
{
	uint32_t *syscon = (uint32_t *)ctx;

	switch (data) {
	case 0xACDC0FF0: /* AC/DC OFF */
		*syscon = 1;
		break;

	case 0xC01DB001: /* COLD BOOT */
		*syscon = 2;
		break;

	default:
		warn("syscon: invalid value %08x", data);
		return 1;
	}

	info("syscon: write %08x data %08x", addr, data);
	return 0;
}

static struct bus_ops syscon_ops = {
	.read32  = syscon_read32,
	.write32 = syscon_write32,
};

/* memory operations */
static uint32_t ram_reserved_addr[8];

static int ram_read32(void *ctx, uint32_t addr, uint32_t *data)
{
	uint8_t *ram = (uint8_t *)ctx;
	*data = *(uint32_t *)&ram[addr];
	return 0;
}

static int ram_write32(void *ctx, uint32_t addr, uint32_t data)
{
	uint8_t *ram = (uint8_t *)ctx;
	*(uint32_t *)&ram[addr] = data;
	return 0;
}

static int ram_read16(void *ctx, uint32_t addr, uint16_t *data)
{
	uint8_t *ram = (uint8_t *)ctx;
	*data = *(uint16_t *)&ram[addr];
	return 0;
}

static int ram_write16(void *ctx, uint32_t addr, uint16_t data)
{
	uint8_t *ram = (uint8_t *)ctx;
	*(uint16_t *)&ram[addr] = data;
	return 0;
}

static int ram_read8(void *ctx, uint32_t addr, uint8_t *data)
{
	uint8_t *ram = (uint8_t *)ctx;
	*data = *(uint8_t *)&ram[addr];
	return 0;
}

static int ram_write8(void *ctx, uint32_t addr, uint8_t data)
{
	uint8_t *ram = (uint8_t *)ctx;
	*(uint8_t *)&ram[addr] = data;
	return 0;
}

static int ram_read_reserved32(void *ctx,
	uint32_t addr, uint32_t *data, uint32_t id)
{
	uint8_t *ram = (uint8_t *)ctx;

	/* reserve and read */
	ram_reserved_addr[id] = addr & ~0x3;
	*data = *(uint32_t *)&ram[addr];
	return 0;
}

static int ram_write_conditional32(void *ctx,
	uint32_t addr, uint32_t data, uint32_t id, uint32_t *success)
{
	if (ram_reserved_addr[id] == (addr & ~0x3)) {
		uint8_t *ram = (uint8_t *)ctx;

		/* reservation valid, success */
		*(uint32_t *)&ram[addr] = data;
		*success = 0;

		/* make sure anyone else fails */
		for (size_t i = 0; i < sizeof(ram_reserved_addr)/sizeof(ram_reserved_addr[0]); i++) {
			if (ram_reserved_addr[i] == (addr & ~0x03)) {
				ram_reserved_addr[i] = 0;
			}
		}

	} else {
		/* reservation invalid, fail */
		*success = 1;

		/* prevent stale reservations */
		ram_reserved_addr[id] = 0;
	}

	return 0;
}

static struct bus_ops ram_ops = {
	.read32              = ram_read32,
	.write32             = ram_write32,
	.read16              = ram_read16,
	.write16             = ram_write16,
	.read8               = ram_read8,
	.write8              = ram_write8,
	.read_reserved32     = ram_read_reserved32,
	.write_conditional32 = ram_write_conditional32,
};

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
	while ((sz = fread(&ram[pos], 1, 4096, fh))) {
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
	static const size_t dtboff = MEMOFF + MEMSIZE - 16384;
	static const size_t rdoff  = MEMOFF + MEMSIZE - 1024*1024;

	/* initialize ui */
	if (!(ui = ui_init()))
		warn("ui: failed to initialize");

	/* initialize devices */
	for (size_t i = 0; i < sizeof(cpus)/sizeof(cpus[0]); i++) {
		if (!(cpus[i] = cpu_init(i)))
			fail("cpu%d: cannot initialize", i);
	}
	if (!(ram = malloc(MEMSIZE)))
		fail("ram: cannot allocate");
	if (!(clint = clint_init(cpus, sizeof(cpus)/sizeof(cpus[0]))))
		fail("clint: cannot initialize");
	if (!(plic = plic_init(cpus[0])))
		fail("plic: cannot initialize");
	if (!(uart = uart_init(plic, 1)))
		fail("uart: cannot initialize");
	if (!(display = display_init(ui)))
		fail("display: cannot initialize");

	/* attach devices */
	bus_attach(MEMOFF,     MEMSIZE,     &ram_ops,     ram);
	bus_attach(CLINTOFF,   CLINTSIZE,   &clint_ops,   clint);
	bus_attach(PLICOFF,    PLICSIZE,    &plic_ops,    plic);
	bus_attach(UARTOFF,    UARTSIZE,    &uart_ops,    uart);
	bus_attach(DISPLAYOFF, DISPLAYSIZE, &display_ops, display);
	bus_attach(SYSCONOFF,  SYSCONSIZE,  &syscon_ops,  &syscon);

	/* load memory */
res:	memset(ram, 0, MEMSIZE);
	if (argc > 1) load("image",  argv[1], MEMOFF, rdoff);
	if (argc > 2) load("dtb",    argv[2], dtboff, MEMOFF+MEMSIZE);
	if (argc > 3) load("initrd", argv[3], rdoff,  dtboff);

	/* reset cpus */
	for (size_t i = 0; i < sizeof(cpus)/sizeof(cpus[0]); i++) {
		cpu_reset(cpus[i], MEMOFF);
		cpu_reg_set(cpus[i], REG_A0, cpu_csr_get(cpus[i], CSR_MHARTID));
		cpu_reg_set(cpus[i], REG_A1, dtboff);
	}

	/* main loop */
	info("cpus: start");
	syscon = 0;
	while (syscon == 0) {
		/* tick user interface */
		ui_tick(ui);

		/* tick hardware */
		clint_tick(clint);
		uart_tick(uart);

		/* run cpus */
		int ret = 1;

		for (size_t i = 0; i < sizeof(cpus)/sizeof(cpus[0]); i++)
			ret &= cpu_run(cpus[i], 1024);

		switch (ret) {
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
	for (size_t i = 0; i < sizeof(cpus)/sizeof(cpus[0]); i++)
		cpu_free(cpus[i]);

	return 0;
}
