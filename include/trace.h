#ifndef TRACE_H
#define TRACE_H

#include <stdio.h>
#include "log.h"

struct liftoff_tracer {
	FILE *f;
};

int
liftoff_tracer_init(struct liftoff_tracer *tracer);

void
liftoff_tracer_finish(struct liftoff_tracer *tracer);

void
liftoff_tracer_mark(struct liftoff_tracer *tracer, const char *format, ...)
_LIFTOFF_ATTRIB_PRINTF(2, 3);

#endif
