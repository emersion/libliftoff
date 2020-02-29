#ifndef LOG_H
#define LOG_H

#include <libliftoff.h>

#ifdef __GNUC__
#define _LIFTOFF_ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _LIFTOFF_ATTRIB_PRINTF(start, end)
#endif

bool log_has(enum liftoff_log_importance verbosity);

void liftoff_log(enum liftoff_log_importance verbosity,
		 const char *format, ...) _LIFTOFF_ATTRIB_PRINTF(2, 3);
void liftoff_log_errno(enum liftoff_log_importance verbosity, const char *msg);

void debug_cnt(struct liftoff_device *device, const char *format, ...)
_LIFTOFF_ATTRIB_PRINTF(2, 3);

#endif
