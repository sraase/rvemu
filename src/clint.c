#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "emu.h"

/* RISC-V Core Local Interrupt Controller */
struct clint {
	struct   cpu **cpus;
	size_t   num_cpus;
	uint64_t time;
	uint64_t timecmp[8];
};

static int clint_read32(void *ctx, uint32_t addr, uint32_t *data)
{
	struct clint *clint = (struct clint *)ctx;
	switch (addr) {
	case 0x0000:
	case 0x0004:
	case 0x0008:
	case 0x000c:
	case 0x0010:
	case 0x0014:
	case 0x0018:
	case 0x001c:
		*data = !!cpu_irq_get(clint->cpus[(addr >> 2) & 0x07], 3);
		break;

	case 0x4000:
	case 0x4008:
	case 0x4010:
	case 0x4018:
	case 0x4020:
	case 0x4028:
	case 0x4030:
	case 0x4038:
		*data = clint->timecmp[(addr >> 3) & 0x07];
		break;

	case 0x4004:
	case 0x400c:
	case 0x4014:
	case 0x401c:
	case 0x4024:
	case 0x402c:
	case 0x4034:
	case 0x403c:
		*data = clint->timecmp[(addr >> 3) & 0x07] >> 32;
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
	case 0x0004:
	case 0x0008:
	case 0x000c:
	case 0x0010:
	case 0x0014:
	case 0x0018:
	case 0x001c:
		if (data & 1) cpu_irq_set  (clint->cpus[(addr >> 2) & 0x07], 3);
		else          cpu_irq_clear(clint->cpus[(addr >> 2) & 0x07], 3);
		break;

	case 0x4000:
	case 0x4008:
	case 0x4010:
	case 0x4018:
	case 0x4020:
	case 0x4028:
	case 0x4030:
	case 0x4038:
		clint->timecmp[(addr >> 3) & 0x07] = (clint->timecmp[(addr >> 3) & 0x07] & ~0xFFFFFFFFUL) | data;
		break;

	case 0x4004:
	case 0x400c:
	case 0x4014:
	case 0x401c:
	case 0x4024:
	case 0x402c:
	case 0x4034:
	case 0x403c:
		clint->timecmp[(addr >> 3) & 0x07] = (clint->timecmp[(addr >> 3) & 0x07] &  0xFFFFFFFFUL) | ((uint64_t)data << 32);
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

	for (size_t i = 0; i < clint->num_cpus; i++) {
		if (clint->time > clint->timecmp[i])
			cpu_irq_set(clint->cpus[i], 7);
		else
			cpu_irq_clear(clint->cpus[i], 7);
	}
}

void clint_free(struct clint *clint)
{
	free(clint);
	return;
}

struct clint *clint_init(struct cpu **cpus, size_t num_cpus)
{
	struct clint *clint = calloc(1, sizeof(struct clint));
	if (clint) {
		clint->cpus     = cpus;
		clint->num_cpus = num_cpus;
		clint->time     = gettime_us();

		if (num_cpus > 8)
			fail("clint: cannot have >8 cpus");
	}
	return clint;
}
