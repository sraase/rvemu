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

/* Intel 8250/16450-style UART */
struct uart {
	struct   cpu *cpu;
	uint8_t  dll, dlh; /* Divisor Latch */
	uint8_t  ier, isr; /* Interrupt Enable and Status */
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
	tio_raw.c_iflag &= ~(/*BRKINT |*/ ICRNL | INPCK | ISTRIP | IXON);
	tio_raw.c_oflag &= ~(OPOST);
	tio_raw.c_cflag |=  (CS8);
	tio_raw.c_lflag &= ~(ECHO | ICANON | IEXTEN /*| ISIG*/);
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

			/* update Receive Buffer Full */
			if (term_pending()) uart->isr |=  0x01;
			else                uart->isr &= ~0x01;
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
		if (uart->isr & 0x01) {
			*data = 0x04; /* Receive Buffer Full */
		} else if (uart->isr & 0x02) {
			*data = 0x02; /* Transmit Buffer Empty */
		} else if (uart->isr & 0x04) {
			*data = 0x06; /* Line Status Changed */
		} else if (uart->isr & 0x08) {
			*data = 0x00; /* Modem Status Changed */
		} else {
			*data = 0x00; /* no interrupt */
		}
		uart->isr &= ~0x02; /* clear Transmitter Buffer Empty */
		break;

	case 0x03:
		/* LCR: Line Control Register */
		*data = uart->lcr;
		break;

	case 0x04:
		/* MCR: Modem Control Register */
		*data = uart->mcr & 0x0F;
		break;

	case 0x05:
		/* LSR: Line Status Register */
		*data = term_pending() ? 0x61 : 0x60;
		uart->isr &= ~0x04; /* clear Line Status Changed */
		break;

	case 0x06:
		/* MSR: Modem Status Register */
		*data = uart->msr | 0x80;
		uart->msr &= ~0x0F;
		uart->isr &= ~0x08; /* clear Modem Status Changed */
		break;

	case 0x07: /* SCR */
		*data = uart->scr;
		break;

	default:
		warn("uart: invalid register %02x", addr);
		return 1;
	}

//info("uart read %02x   %02x", addr, *data);
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

			/* update Transmitter Buffer Empty */
			uart->isr &= ~0x02;
		}
		break;

	case 0x01:
		if (uart->lcr & 0x80) {
			/* DLH: Divider Latch High */
			uart->dlh = data;
		} else {
			/* IER: Interrupt Enable Register */
			uart->ier = data;
		}
		break;

	case 0x02:
		/* FCR: FIFO Control Register */
		break; /* no FIFO */

	case 0x03:
		/* LCR: Line Control Register */
		uart->lcr = data;
		break;

	case 0x04:
		/* MCR: Modem Control Register */
		uart->mcr = data & 0x0F;
		break;

	case 0x07:
		/* SCR: Scratch Register */
		uart->scr = data;
		break;

	default:
		warn("uart: invalid register %02x data %02x", addr, data);
		return 1;
	}

	return 0;
}

void uart_tick(struct uart *uart)
{
	/* update Receive Buffer Full */
	if (term_pending()) uart->isr |=  0x01;
	else                uart->isr &= ~0x01;

#if 0
	/* trigger interrupts */
	if (uart->ier & uart->isr) cpu_irq_set  (uart->cpu, 11);
	else                       cpu_irq_clear(uart->cpu, 11);
#endif

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

struct uart *uart_init(struct cpu *cpu)
{
	struct uart *uart = calloc(1, sizeof(struct uart));
	if (uart) {
		uart->cpu = cpu;

		/* prepare terminal */
		if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
			term_enter();
	}
	return uart;
}
