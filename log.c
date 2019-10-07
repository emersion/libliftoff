#include <stdio.h>
#include "log.h"

static enum liftoff_log_importance log_importance = LIFTOFF_ERROR;

static void log_stderr(enum liftoff_log_importance verbosity, const char *fmt,
		       va_list args)
{
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
}

static liftoff_log_func log_callback = log_stderr;

void liftoff_log_init(enum liftoff_log_importance verbosity,
		      liftoff_log_func callback) {
	log_importance = verbosity;
	if (callback) {
		log_callback = callback;
	}
}

void liftoff_log(enum liftoff_log_importance verbosity, const char *fmt, ...)
{
	if (verbosity > log_importance) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	log_callback(verbosity, fmt, args);
	va_end(args);
}
