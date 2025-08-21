#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "emu.h"

/**
 * This is a minimal implementation providing 31 interrupts
 * with fixed hard-wired priorities on a single context.
 */

/* RISC-V Platform Level Interrupt Controller */
struct plic {
	struct   cpu *cpu;
	uint32_t pending;
	uint32_t enabled;
};

static void plic_update(struct plic *plic)
{
	if (plic->pending & plic->enabled) cpu_irq_set  (plic->cpu, 11);
	else                               cpu_irq_clear(plic->cpu, 11);
}

static int plic_read32(void *ctx, uint32_t addr, uint32_t *data)
{
	struct plic *plic = (struct plic *)ctx;

	/* interrupt source priority */
	if      (addr <= 0x000000) *data = 0;
	else if (addr <= 0x00007C) *data = 1;
	else if (addr <= 0x000FFC) goto invalid;
	/* interrupt pending bits */
	else if (addr <= 0x001000) *data = plic->pending;
	else if (addr <= 0x001FFC) goto invalid;
	/* interrupt enable bits */
	else if (addr <= 0x002000) *data = plic->enabled;
	else if (addr <= 0x1FFFFC) goto invalid;
	/* threshold/claim */
	else if (addr <= 0x200000) *data = 0;
	else if (addr <= 0x200004) goto claim;
	else                       goto invalid;

	return 0;

invalid:
	/* bus error */
	warn("plic: invalid addr %08x", addr);
	return 1;

claim:
	/* return lowest active interrupt */
	*data = 0;
	for (int i = 1; i < 32; ++i) {
		if (plic->enabled & plic->pending & (1 << i)) {
			*data = i;
			break;
		}
	}
	return 0;
}

static int plic_write32(void *ctx, uint32_t addr, uint32_t data)
{
	struct plic *plic = (struct plic *)ctx;

	/* interrupt source priority */
	if      (addr <= 0x000000) (void)data;
	else if (addr <= 0x00007C) (void)data;
	else if (addr <= 0x000FFC) goto invalid;
	/* interrupt pending bits */
	else if (addr <= 0x001000) (void)data;
	else if (addr <= 0x001FFC) goto invalid;
	/* interrupt enable bits */
	else if (addr <= 0x002000) plic->enabled = data;
	else if (addr <= 0x1FFFFC) goto invalid;
	/* threshold/claim */
	else if (addr <= 0x200000) (void)data;
	else if (addr <= 0x200004) goto complete;
	else                       goto invalid;

	return 0;

invalid:
	/* bus error */
	warn("plic: invalid addr %08x data %08x", addr, data);
	return 1;

complete:
	/* acknowledge specified interrupt */
	if (data < 1 || data > 31) {
		warn("plic: invalid irq %d", data);
		return 1;
	}

	if (plic->enabled & (1 << data)) {
		plic->pending &= ~(1 << data);
		plic_update(plic);
	}
	return 0;
}

struct bus_ops plic_ops = {
	.read32  = plic_read32,
	.write32 = plic_write32,
};

void plic_irq(struct plic *plic, unsigned irq)
{
	/* set interrupt pending */
	if (irq < 1 || irq > 31) {
		warn("plic: invalid irq %d", irq);
		return;
	}

	plic->pending |= (1 << irq);
	plic_update(plic);
	return;
}

void plic_free(struct plic *plic)
{
	free(plic);
	return;
}

struct plic *plic_init(struct cpu *cpu)
{
	struct plic *plic = calloc(1, sizeof(struct plic));
	if (plic) {
		plic->cpu = cpu;
	}
	return plic;
}
