#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "private.h"

static enum liftoff_log_importance log_importance = LIFTOFF_ERROR;

static void log_stderr(enum liftoff_log_importance verbosity,
					   enum liftoff_log_flags flags,
					   const char *fmt, va_list args)
{
	if (flags & LIFTOFF_LOG_SECTION_START) {
		fprintf(stderr, "\n");
	}
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	if (flags & LIFTOFF_LOG_SECTION_END) {
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
	log_callback(verbosity, 0, fmt, args);
	va_end(args);
}

void liftoff_log_formatted(enum liftoff_log_importance verbosity,
						   enum liftoff_log_flags flags,
						   const char *fmt, ...)
{
	if (!log_has(verbosity)) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	log_callback(verbosity, flags, fmt, args);
	va_end(args);
}

void liftoff_log_errno(enum liftoff_log_importance verbosity, const char *msg)
{
	liftoff_log(verbosity, "%s: %s", msg, strerror(errno));
}


void debug_cnt(struct liftoff_device *device, const char *fmt, ...)
{
	if (!log_has(LIFTOFF_DEBUG)) {
		return;
	}

	va_list args;
	va_start(args, fmt);

	if (device->log_buf == NULL) {
		device->log_buf = malloc(4096);
		if (device->log_buf == NULL) {
			liftoff_log_errno(LIFTOFF_ERROR, "malloc");
			goto cleanup_out;
		}
		device->log_buf_len = 4096 / sizeof(char);
	}

	do {
		size_t max_len = device->log_buf_len - device->log_buf_index;
		int ret;

		ret = vsnprintf(device->log_buf + device->log_buf_index,
						max_len, fmt, args);
		if (ret < 0) {
			goto cleanup_out;
		}

		if (ret - (int)max_len > 0) {
			device->log_buf_index = device->log_buf_len;
			device->log_buf_len *= 2;

			if (realloc(device->log_buf, device->log_buf_len) == NULL) {
				liftoff_log_errno(LIFTOFF_ERROR, "realloc");
				goto cleanup_out;
			}
		} else {
			device->log_buf_index += ret;
			goto final_out;
		}
	} while (1);

cleanup_out:
	free(device->log_buf);
	device->log_buf = NULL;
	device->log_buf_index = 0;
	device->log_buf_len = 0;

final_out:
	va_end(args);
}

void debug_end(struct liftoff_device *device, enum liftoff_log_flags flags)
{
	if (!log_has(LIFTOFF_DEBUG)) {
		return;
	}
	if (device->log_buf == NULL) {
		return;
	}
	liftoff_log_formatted(LIFTOFF_DEBUG, flags, "%s", device->log_buf);
	free(device->log_buf);
	device->log_buf = NULL;
	device->log_buf_index = 0;
	device->log_buf_len = 0;
}
