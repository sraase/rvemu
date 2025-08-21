#include <stdint.h>
#include <stdlib.h>
#include "emu.h"

#define ARRAY_SIZE(ar) (sizeof(ar) / sizeof(ar[0]))
#define ARRAY_END(ar)  (&ar[ARRAY_SIZE(ar)])

struct bus_device {
	uint32_t start;
	uint32_t end;
	struct   bus_ops *ops;
	void     *ctx;
};

static struct bus_device bus_devices[16];

void bus_attach(uint32_t start, uint32_t size, struct bus_ops *ops, void *ctx)
{
	size_t num = sizeof(bus_devices) / sizeof(bus_devices[0]);
	for (size_t i = 0; i < num; i++) {
		struct bus_device *dev = &bus_devices[i];

		/* skip if in use */
		if (dev->ops)
			continue;

		/* attach device */
		info("bus: attach device at %08x size %08x", start, size);
		dev->start   = start;
		dev->end     = start + size;
		dev->ops     = ops;
		dev->ctx     = ctx;
		return;
	}
	fail("bus: cannot attach device at %08x size %08x", start, size);
}

int bus_read32(uint32_t addr, uint32_t *data)
{
	struct bus_device *dev;

	for (dev = bus_devices; dev < ARRAY_END(bus_devices); dev++) {
		if (addr >= dev->start && addr < dev->end &&
		    dev->ops && dev->ops->read32) {
			return dev->ops->read32(dev->ctx,
				addr - dev->start, data);
		}
	}

	warn("bus: invalid read32 from %08x", addr);
	return 1;
}

int bus_write32(uint32_t addr, uint32_t data)
{
	struct bus_device *dev;

	for (dev = bus_devices; dev < ARRAY_END(bus_devices); dev++) {
		if (addr >= dev->start && addr < dev->end &&
		    dev->ops && dev->ops->write32) {
			return dev->ops->write32(dev->ctx,
				addr - dev->start, data);
		}
	}

	warn("bus: invalid write32 to %08x data %08x", addr, data);
	return 1;
}

int bus_read16(uint32_t addr, uint16_t *data)
{
	struct bus_device *dev;

	for (dev = bus_devices; dev < ARRAY_END(bus_devices); dev++) {
		if (addr >= dev->start && addr < dev->end &&
		    dev->ops && dev->ops->read16) {
			return dev->ops->read16(dev->ctx,
				addr - dev->start, data);
		}
	}

	warn("bus: invalid read16 from %08x", addr);
	return 1;
}

int bus_write16(uint32_t addr, uint16_t data)
{
	struct bus_device *dev;

	for (dev = bus_devices; dev < ARRAY_END(bus_devices); dev++) {
		if (addr >= dev->start && addr < dev->end &&
		    dev->ops && dev->ops->write16) {
			return dev->ops->write16(dev->ctx,
				addr - dev->start, data);
		}
	}

	warn("bus: invalid write16 to %08x data %04x", addr, data);
	return 1;
}

int bus_read8(uint32_t addr, uint8_t *data)
{
	struct bus_device *dev;

	for (dev = bus_devices; dev < ARRAY_END(bus_devices); dev++) {
		if (addr >= dev->start && addr < dev->end &&
		    dev->ops && dev->ops->read8) {
			return dev->ops->read8(dev->ctx,
				addr - dev->start, data);
		}
	}

	warn("bus: invalid read8 from %08x", addr);
	return 1;
}

int bus_write8(uint32_t addr, uint8_t data)
{
	struct bus_device *dev;

	for (dev = bus_devices; dev < ARRAY_END(bus_devices); dev++) {
		if (addr >= dev->start && addr < dev->end &&
		    dev->ops && dev->ops->write8) {
			return dev->ops->write8(dev->ctx,
				addr - dev->start, data);
		}
	}

	warn("bus: invalid write8 to %08x data %02x", addr, data);
	return 1;
}
