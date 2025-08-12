#include <signal.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include "emu.h"

#define FPS    10
#define WIDTH  800
#define HEIGHT 600

/* SDL2 User Interface */
struct ui {
	/* keyboard */
	void  *kb_ctx;
	void (*kb_cb)(void *ctx, uint32_t key);

	/* display */
	SDL_Window   *window;
	SDL_Renderer *renderer;
	SDL_Texture  *texture;
	uint32_t     *buffer;
	uint32_t     timestamp;
	int          damaged;
};

void ui_kb_setcb(struct ui *ui, void *ctx, void (*cb)(void *ctx, uint32_t key))
{
	if (ui) {
		ui->kb_ctx = ctx;
		ui->kb_cb  = cb;
	}
}

void ui_fb_update(struct ui *ui)
{
	if (ui) {
		ui->damaged = 1;
	}
}

void ui_fb_setbuf(struct ui *ui, uint32_t *buf)
{
	if (ui) {
		if (buf) {
			ui->buffer  = buf;
			ui->damaged = 1;
		} else {
			SDL_RenderClear(ui->renderer);
			SDL_RenderPresent(ui->renderer);
		}
	}
}

void ui_fb_getsize(struct ui *ui, uint32_t *width, uint32_t *height)
{
	if (ui) {
		*width  = WIDTH;
		*height = HEIGHT;
	}
}

void ui_tick(struct ui *ui)
{
	if (ui) {
		uint32_t timestamp = gettime_us();
		SDL_Event ev;

		/* handle events */
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
			case SDL_QUIT:
				/* power off system */
				bus_write32(0x12000000, 0xACDC0FF0);
				break;

			case SDL_KEYDOWN:
				/* send keystroke */
				if (ui->kb_cb) {
					ui->kb_cb(ui->kb_ctx,
					          ev.key.keysym.sym);
				}
				break;
			}
		}

		/* handle display */
		if (ui->damaged && ui->buffer &&
		    timestamp - ui->timestamp > 1000000 / FPS) {
			void *pixels;
			int pitch;

			/* update texture */
			SDL_LockTexture(ui->texture, NULL, &pixels, &pitch);
			memcpy(pixels, ui->buffer, WIDTH * HEIGHT * 4);
			SDL_UnlockTexture(ui->texture);

			/* update window */
			SDL_RenderClear(ui->renderer);
			SDL_RenderCopy(ui->renderer, ui->texture, NULL, NULL);
			SDL_RenderPresent(ui->renderer);

			/* update state */
			ui->timestamp = timestamp;
			ui->damaged   = 0;
		}
	}
}

void ui_free(struct ui *ui)
{
	if (ui) {
		SDL_DestroyTexture(ui->texture);
		SDL_DestroyRenderer(ui->renderer);
		SDL_DestroyWindow(ui->window);
		free(ui);
	}
	return;
}

struct ui *ui_init(void)
{
	struct ui *ui = calloc(1, sizeof(struct ui));
	if (ui) {
		if (SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_VIDEO))
			goto fail;

		if (!(ui->window = SDL_CreateWindow("rvemu",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			WIDTH, HEIGHT, 0)))
			goto fail;

		if (!(ui->renderer = SDL_CreateRenderer(ui->window, -1, 0)))
			goto fail;

		if (!(ui->texture = SDL_CreateTexture(ui->renderer,
			SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
			WIDTH, HEIGHT)))
			goto fail;

		/* initial state */
		ui->timestamp = gettime_us();
		SDL_SetRenderDrawColor(ui->renderer,
			0x80, 0x80, 0x80, SDL_ALPHA_OPAQUE);
		SDL_RenderClear(ui->renderer);
		SDL_RenderPresent(ui->renderer);

		/* SDL overrides SIGINT and SIGTERM, restore them */
		signal(SIGINT,  SIG_DFL);
		signal(SIGTERM, SIG_DFL);
	}
	return ui;

fail:
	ui_free(ui);
	return NULL;
}
