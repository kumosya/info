#ifndef LIBC_TIME_H
#define LIBC_TIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Types */

typedef long time_t;

struct tm {
    int tm_sec;   /* seconds after the minute [0-60] */
    int tm_min;   /* minutes after the hour [0-59] */
    int tm_hour;  /* hours since midnight [0-23] */
    int tm_mday;  /* day of the month [1-31] */
    int tm_mon;   /* months since January [0-11] */
    int tm_year;  /* years since 1900 */
    int tm_wday;  /* days since Sunday [0-6] */
    int tm_yday;  /* days since January 1 [0-365] */
    int tm_isdst; /* Daylight Saving Time flag */
};

/* Functions */

double difftime(time_t time1, time_t time0);
time_t mktime(struct tm *timeptr);
time_t time(time_t *timer);
char *asctime(const struct tm *timeptr);
char *ctime(const time_t *timer);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
size_t strftime(char *s, size_t maxsize, const char *format,
                const struct tm *timeptr);

#ifdef __cplusplus
}
#endif

#endif /* LIBC_TIME_H */
