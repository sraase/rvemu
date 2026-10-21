#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "emu.h"

/**
 * This is a minimal implementation providing 31 interrupts
 * with fixed hard-wired priorities and one context per CPU.
 */

/* RISC-V Platform Level Interrupt Controller */
struct plic {
	struct   cpu **cpus;
	size_t   num_cpus;
	uint32_t pending;
	uint32_t claimed;
	uint32_t enabled[8];
};

static void plic_update(struct plic *plic)
{
	for (size_t i = 0; i < plic->num_cpus; i++) {
		if (plic->pending & ~plic->claimed & plic->enabled[i])
			cpu_irq_set(plic->cpus[i], 11);
		else
			cpu_irq_clear(plic->cpus[i], 11);
	}
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
	else if (addr <= 0x002400)
	{
		if ((addr & 0x3FFC7F) == 0x002000) {
			*data = plic->enabled[(addr >> 7) & 0x07];
		} else {
			goto invalid;
		}
	}
	else if (addr <= 0x1FFFFC) goto invalid;
	/* threshold/claim */
	else if (addr <= 0x208004)
	{
		uint32_t off = addr & 0x3F8FFF;
		if (off == 0x200000) {
			/* threshold */
			*data = 0;
		} else if (off == 0x200004) {
			/* claim */
			*data = 0;
			for (int i = 1; i < 32; ++i) {
				uint32_t enabled = plic->enabled[(addr >> 12) & 0x07];
				if (enabled & ~plic->claimed & plic->pending & (1 << i)) {
					plic->claimed |= (1 << i);
					*data = i;
					break;
				}
			}
		} else {
			goto invalid;
		}
	}
	else
		goto invalid;

	return 0;

invalid:
	/* bus error */
	warn("plic: invalid addr %08x", addr);
	return 1;
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
	else if (addr <= 0x002400)
	{
		if ((addr & 0x3FFC7F) == 0x002000) {
			plic->enabled[(addr >> 7) & 0x07] = data;
		} else {
			goto invalid;
		}
	}
	else if (addr <= 0x1FFFFC) goto invalid;
	/* threshold/claim */
	else if (addr <= 0x208004)
	{
		uint32_t off = addr & 0x3F8FFF;
		if (off == 0x200000) {
			/* threshold */
			(void)data;
		} else if (off == 0x200004) {
			/* complete */
			if (data < 1 || data > 31) {
				warn("plic: invalid irq %d", data);
				return 1;
			}

			/* acknowledge interrupt */
			if (plic->enabled[(addr >> 12) & 0x07] & (1 << data)) {
				plic->claimed &= ~(1 << data);
				plic_update(plic);
			}

		} else {
			goto invalid;
		}
	}
	else
		goto invalid;

	return 0;

invalid:
	/* bus error */
	warn("plic: invalid addr %08x data %08x", addr, data);
	return 1;
}

struct bus_ops plic_ops = {
	.read32  = plic_read32,
	.write32 = plic_write32,
};

void plic_irq_set(struct plic *plic, unsigned irq)
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

void plic_irq_clear(struct plic *plic, unsigned irq)
{
	/* set interrupt pending */
	if (irq < 1 || irq > 31) {
		warn("plic: invalid irq %d", irq);
		return;
	}

	plic->pending &= ~(1 << irq);
	plic_update(plic);
	return;
}

void plic_free(struct plic *plic)
{
	free(plic);
	return;
}

struct plic *plic_init(struct cpu **cpus, size_t num_cpus)
{
	struct plic *plic = calloc(1, sizeof(struct plic));
	if (plic) {
		plic->cpus     = cpus;
		plic->num_cpus = num_cpus;
	}
	return plic;
}
