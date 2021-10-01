#include <errno.h>
#include "trace.h"

int
liftoff_tracer_init(struct liftoff_tracer *tracer)
{
	tracer->f = fopen("/sys/kernel/tracing/trace_marker", "w");
	if (tracer->f == NULL) {
		return -errno;
	}
	liftoff_log(LIFTOFF_DEBUG, "Kernel tracing is enabled");
	return 0;
}

void
liftoff_tracer_finish(struct liftoff_tracer *tracer)
{
	if (tracer->f != NULL) {
		fclose(tracer->f);
	}
}

void
liftoff_tracer_mark(struct liftoff_tracer *tracer, const char *format, ...)
{
	if (tracer->f == NULL) {
		return;
	}

	fprintf(tracer->f, "libliftoff: ");

	va_list args;
	va_start(args, format);
	vfprintf(tracer->f, format, args);
	va_end(args);

	fprintf(tracer->f, "\n");
	fflush(tracer->f);
}
