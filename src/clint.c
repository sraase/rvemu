#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "emu.h"

/* RISC-V Core Local Interrupt Controller */
struct clint {
	struct   cpu *cpu;
	uint64_t time;
	uint64_t timecmp;
};

static int clint_read32(void *ctx, uint32_t addr, uint32_t *data)
{
	struct clint *clint = (struct clint *)ctx;
	switch (addr) {
	case 0x0000:
		*data = !!cpu_irq_get(clint->cpu, 3);
		break;

	case 0x4000:
		*data = clint->timecmp;
		break;

	case 0x4004:
		*data = clint->timecmp >> 32;
		break;

	case 0xbff8:
		*data = clint->time;
		break;

	case 0xbffc:
		*data = clint->time >> 32;
		break;

	default:
		warn("clint: invalid addr %08x", addr);
		return 1;
	}

	return 0;
}

static int clint_write32(void *ctx, uint32_t addr, uint32_t data)
{
	struct clint *clint = (struct clint *)ctx;
	switch (addr) {
	case 0x0000:
		if (data & 1) cpu_irq_set  (clint->cpu, 3);
		else          cpu_irq_clear(clint->cpu, 3);
		break;

	case 0x4000:
		clint->timecmp = (clint->timecmp & ~0xFFFFFFFFUL) | data;
		break;

	case 0x4004:
		clint->timecmp = (clint->timecmp &  0xFFFFFFFFUL) |
		                 ((uint64_t)data << 32);
		break;

	default:
		warn("clint: invalid addr %08x data %08x", addr, data);
		return 1;
	}

	return 0;
}

struct bus_ops clint_ops = {
	.read32  = clint_read32,
	.write32 = clint_write32,
};

void clint_tick(struct clint *clint)
{
	clint->time = gettime_us();

	if (clint->time > clint->timecmp) cpu_irq_set(clint->cpu, 7);
	else                              cpu_irq_clear(clint->cpu, 7);
}

void clint_free(struct clint *clint)
{
	free(clint);
	return;
}

struct clint *clint_init(struct cpu *cpu)
{
	struct clint *clint = calloc(1, sizeof(struct clint));
	if (clint) {
		clint->cpu  = cpu;
		clint->time = gettime_us();
	}
	return clint;
}
