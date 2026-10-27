#ifndef LOG_H
#define LOG_H

/* temporary debug log (memory, stack, timings): log.txt on the SD card,
   stdout over USB. Built with LOG=1 (fpdoom/apps/Makefile); with LOG=0
   every call below compiles to nothing. */
#if APP_LOG
#include "gfx.h"
void log_start(void);
void log_end(void);
/* name of the app being started, NULL when back to the menu */
void log_app(const char *name);
void log_msg(const char *fmt, ...);
#define LOG_T0(t) uint32_t t = sys_timer_ms()
#define LOG_OP(t, fmt, ...) log_msg(fmt " %ums", __VA_ARGS__, (unsigned)(sys_timer_ms() - (t)))
#else
#define log_start() ((void)0)
#define log_end() ((void)0)
#define log_app(name) ((void)0)
#define LOG_T0(t) ((void)0)
#define LOG_OP(t, ...) ((void)0)
#endif

#endif
