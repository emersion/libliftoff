#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	log_callback(verbosity, fmt, args);
	va_end(args);
}

void liftoff_log_errno(enum liftoff_log_importance verbosity, const char *msg)
{
	liftoff_log(verbosity, "%s: %s", msg, strerror(errno));
}

void liftoff_log_buffer_append(struct liftoff_log_buffer *buf,
							   const char *fmt, ...)
{
	if (!buf) {
		return;
	}

	va_list args;
	va_start(args, fmt);

	if (buf->data == NULL) {
		buf->data = malloc(4096);
		if (buf->data == NULL) {
			liftoff_log_errno(LIFTOFF_ERROR, "malloc");
			goto cleanup_out;
		}
		buf->len = 4096 / sizeof(char);
	}

	do {
		size_t max_len = buf->len - buf->cap;
		int ret;

		ret = vsnprintf(buf->data + buf->cap, max_len, fmt, args);
		if (ret < 0) {
			goto cleanup_out;
		}

		if (ret - (int)max_len > 0) {
			buf->cap = buf->len;
			buf->len *= 2;

			if (realloc(buf->data, buf->len) == NULL) {
				liftoff_log_errno(LIFTOFF_ERROR, "realloc");
				goto cleanup_out;
			}
		} else {
			buf->cap += ret;
			goto final_out;
		}
	} while (1);

cleanup_out:
	free(buf->data);
	buf->data = NULL;
	buf->cap = 0;
	buf->len = 0;

final_out:
	va_end(args);
}

void liftoff_log_buffer_flush(struct liftoff_log_buffer *buf,
							  enum liftoff_log_importance verbosity)
{
	if (!buf || !buf->data || buf->len == 0) {
		return;
	}
	liftoff_log(verbosity, "%s\n", buf->data);
	free(buf->data);
	buf->data = NULL;
	buf->cap = 0;
	buf->len = 0;
}
