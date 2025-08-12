#include <stdint.h>
#include <stdlib.h>
#include "emu.h"

/* Simple Framebuffer Display */
struct display {
	struct   ui *ui;
	uint32_t *buffer;
	uint32_t bufsize;
};

int display_write32(struct display *display, uint32_t addr, uint32_t data)
{
	if (addr < display->bufsize) {
		display->buffer[addr / 4] = data;
		ui_fb_update(display->ui);
		return 0;
	}
	return 1;
}

int display_read32(struct display *display, uint32_t addr, uint32_t *data)
{
	if (addr < display->bufsize) {
		*data = display->buffer[addr / 4];
		return 0;
	}
	return 1;
}

void display_free(struct display *display)
{
	if (display) {
		free(display->buffer);
		free(display);
	}
}

struct display *display_init(struct ui *ui)
{
	struct display *display = calloc(1, sizeof(struct display));
	if (display) {
		uint32_t width, height;

		display->ui = ui;
		ui_fb_getsize(display->ui, &width, &height);
		debug("display: %ux%u", width, height);

		display->bufsize = width * height * 4;
		display->buffer = malloc(display->bufsize);
		if (!display->buffer)
			goto fail;

		ui_fb_setbuf(display->ui, display->buffer);
	}
	return display;

fail:
	display_free(display);
	return NULL;
}
