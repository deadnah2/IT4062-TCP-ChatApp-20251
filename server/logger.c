#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

#define LOG_PATH "data/server.log"

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_event(const char *fmt, ...)
{
    FILE *f;
    time_t now;
    struct tm *tm;
    char ts[32];

    pthread_mutex_lock(&log_mutex);

    f = fopen(LOG_PATH, "a");
    if (!f)
    {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    now = time(NULL);
    tm = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(f, "[%s] ", ts);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fprintf(f, "\n");
    fclose(f);

    pthread_mutex_unlock(&log_mutex);
}
