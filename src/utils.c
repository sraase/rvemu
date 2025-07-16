#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

/* get monotonic time */
uint32_t gettime_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}

/* write log message */
void logmsg(const char *prefix, const char *postfix,
	int fatal, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs(prefix, stderr);
	fputs(": ", stderr);
	vfprintf(stderr, fmt, ap);
	fputs(postfix, stderr);
	fputs("\n", stderr);
	va_end(ap);

	if (fatal)
		exit(1);
}
