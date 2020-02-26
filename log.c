#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

static enum liftoff_log_importance log_importance = LIFTOFF_ERROR;

static void log_stderr(enum liftoff_log_importance verbosity, bool newline, const char *fmt,
		       va_list args)
{
	vfprintf(stderr, fmt, args);
	if (newline) {
		fprintf(stderr, "\n");
	}
}

static liftoff_log_func log_callback = log_stderr;

void liftoff_log_init(enum liftoff_log_importance verbosity,
		      liftoff_log_func callback) {
	log_importance = verbosity;
	if (callback) {
		log_callback = callback;
	} else {
		log_callback = log_stderr;
	}
}

bool log_has(enum liftoff_log_importance verbosity)
{
	return verbosity <= log_importance;
}

void liftoff_log(enum liftoff_log_importance verbosity, const char *fmt, ...)
{
	if (!log_has(verbosity)) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	log_callback(verbosity, true, fmt, args);
	va_end(args);
}

void liftoff_log_cnt(enum liftoff_log_importance verbosity, const char *fmt, ...)
{
	if (!log_has(verbosity)) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	log_callback(verbosity, false, fmt, args);
	va_end(args);
}

void liftoff_log_errno(enum liftoff_log_importance verbosity, const char *msg)
{
	liftoff_log(verbosity, "%s: %s", msg, strerror(errno));
}
