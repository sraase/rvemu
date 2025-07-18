#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include "emu.h"

/* forward ^C to UART if 1 */
#define DISABLE_SIGNALS 1

/* Intel 8250/16450-style UART */
struct uart {
	struct   plic *plic; /* interrupt controller */
	unsigned irqnum;     /* interrupt number */

	uint8_t  dll, dlh; /* Divisor Latch */
	uint8_t  ier, iir; /* Interrupt Enable and Identification */
	uint8_t  lcr, lsr; /* Line Control and Status */
	uint8_t  mcr, msr; /* Modem Control and Status */
	uint8_t  scr;      /* Scratch Register */
};

/* original terminal state */
static int fl_restore;
static struct termios tio_restore;

/* restore terminal */
void term_leave(void)
{
	fl_restore = fcntl(STDIN_FILENO, F_GETFL);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio_restore);
}

/* check if terminal has data */
int term_pending(void)
{
	while (1) {
		struct timeval tv;
		fd_set rfds;
		int ret;

		tv.tv_sec = 0;
		tv.tv_usec = 0;

		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);

		ret = select(STDIN_FILENO+1, &rfds, NULL, NULL, &tv);
		if (ret < 0) {
			if ((errno == EAGAIN || errno == EINTR))
				continue;
			warn("uart: select failed: %s", strerror(errno));
		}
		return ret;
	}
}

/* prepare terminal */
void term_enter(void)
{
	struct termios tio_raw;

	/* restore terminal on exit */
	tcgetattr(STDIN_FILENO, &tio_restore);
	atexit(term_leave);

	/* set raw mode */
	tio_raw = tio_restore;
	tio_raw.c_iflag &= ~(ICRNL | INPCK | ISTRIP | IXON);
	tio_raw.c_oflag &= ~(OPOST);
	tio_raw.c_cflag |=  (CS8);
	tio_raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
#if DISABLE_SIGNALS
	tio_raw.c_iflag &= ~(BRKINT);
	tio_raw.c_lflag &= ~(ISIG);
#endif
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio_raw);

	/* set non-blocking */
	fcntl(STDIN_FILENO, F_SETFL, fl_restore | O_NONBLOCK);
}

int uart_read8(struct uart *uart, uint32_t addr, uint8_t *data)
{
	switch (addr) {
	case 0x00:
		if (uart->lcr & 0x80) {
			/* DLL: Divisor Latch Low */
			*data = uart->dll;
		} else {
			/* RBR: Receiver Buffer Register */
			if (read(STDIN_FILENO, data, 1) <= 0)
				*data = 0xFF;
			uart->lsr &= ~0x01;
		}
		break;

	case 0x01:
		if (uart->lcr & 0x80) {
			/* DLH: Divisor Latch High */
			*data = uart->dlh;
		} else {
			/* IER: Interrupt Enable */
			*data = uart->ier & 0x0F;
		}
		break;

	case 0x02:
		/* IIR: Interrupt Identification Register */
		*data = uart->iir & 0x07;
		uart->lsr &= ~0x20;
		break;

	case 0x03:
		/* LCR: Line Control Register */
		*data = uart->lcr & 0xFF;
		break;

	case 0x04:
		/* MCR: Modem Control Register */
		*data = uart->mcr & 0x0F;
		break;

	case 0x05:
		/* LSR: Line Status Register */
		*data = uart->lsr & 0x7F;
		uart->lsr &= ~0x1E;
		break;

	case 0x06:
		/* MSR: Modem Status Register */
		*data = uart->msr & 0xFF;
		uart->msr &= ~0x0F;
		break;

	case 0x07:
		/* SCR: Scratch Register */
		*data = uart->scr & 0xFF;
		break;

	default:
		warn("uart: invalid register %02x", addr);
		return 1;
	}

	uart_tick(uart);
	return 0;
}

int uart_write8(struct uart *uart, uint32_t addr, uint8_t data)
{
	switch (addr) {
	case 0x00:
		if (uart->lcr & 0x80) {
			/* DLL: Divider Latch Low */
			uart->dll = data;
		} else {
			/* THR: Transmitter Holding Register */
			if (write(STDOUT_FILENO, &data, 1) != 1)
				warn("uart: write failed");
			fsync(STDOUT_FILENO);
			uart->lsr &= ~0x20;
		}
		break;

	case 0x01:
		if (uart->lcr & 0x80) {
			/* DLH: Divider Latch High */
			uart->dlh = data & 0xFF;
		} else {
			/* IER: Interrupt Enable Register */
			uart->ier = data & 0x0F;
		}
		break;

	case 0x02:
		/* FCR: FIFO Control Register */
		(void)data; /* no FIFO */
		break;

	case 0x03:
		/* LCR: Line Control Register */
		uart->lcr = data & 0xFF;
		break;

	case 0x04:
		/* MCR: Modem Control Register */
		uart->mcr = data & 0x0F;
		break;

	case 0x05:
		/* LSR: Line Status Register */
		(void)data;
		break;

	case 0x06:
		/* MSR: Modem Status Register */
		(void)data;
		break;

	case 0x07:
		/* SCR: Scratch Register */
		uart->scr = data & 0xFF;
		break;

	default:
		warn("uart: invalid register %02x data %02x", addr, data);
		return 1;
	}

	uart_tick(uart);
	return 0;
}

void uart_tick(struct uart *uart)
{
	uint8_t iir = uart->iir;

	/* update receiver and transmitter state */
	if (term_pending())
		uart->lsr |= 0x01;
	uart->lsr |= 0x60;

	/* update interrupt identification register */
	if      (uart->ier & 0x04 && uart->lsr & 0x1E) uart->iir = 0x06;
	else if (uart->ier & 0x01 && uart->lsr & 0x01) uart->iir = 0x04;
	else if (uart->ier & 0x02 && uart->lsr & 0x20) uart->iir = 0x02;
	else if (uart->ier & 0x08 && uart->msr & 0x0F) uart->iir = 0x00;
	else                                           uart->iir = 0x01;

	/* trigger interrupt if state changed */
	if (uart->plic && uart->iir != iir && !(uart->iir & 0x01))
		plic_irq(uart->plic, uart->irqnum);

	return;
}

void uart_free(struct uart *uart)
{
	/* restore terminal */
	if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
		term_leave();

	free(uart);
	return;
}

struct uart *uart_init(struct plic *plic, unsigned irq)
{
	struct uart *uart = calloc(1, sizeof(struct uart));
	if (uart) {
		uart->plic   = plic;
		uart->irqnum = irq;
		uart->iir    = 0x01;
		uart->lsr    = 0x60;

		/* prepare terminal */
		if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
			term_enter();
	}
	return uart;
}
