/* The currently supported local timezone is UTC. */
#include "time_impl.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

const char __utc[] = "UTC";
char* tzname[2] = {"UTC","UTC"};
long timezone;
int daylight;
int getdate_err;
struct tm* localtime_r(const time_t* restrict value,struct tm* restrict result) { return gmtime_r(value,result); }
struct tm* localtime(const time_t* value) { return gmtime(value); }
time_t mktime(struct tm* value) {
    long long seconds=__tm_to_secs(value);
    struct tm normalized;
    if (__secs_to_tm(seconds,&normalized)<0) { errno=EOVERFLOW; return (time_t)-1; }
    normalized.tm_isdst=0; normalized.__tm_gmtoff=0; normalized.__tm_zone=__utc;
    *value=normalized; return seconds;
}
const char* __tm_to_tzname(const struct tm* value) { return value->__tm_zone ? value->__tm_zone : __utc; }
void tzset(void) {
    const char* value=getenv("TZ");
    if (value && *value && strcmp(value,"UTC") && strcmp(value,"UTC0") && strcmp(value,"GMT") && strcmp(value,"GMT0")) errno=ENOTSUP;
}
int timespec_get(struct timespec* value,int base) {
    if(base!=TIME_UTC || clock_gettime(CLOCK_REALTIME,value)) return 0;
    return TIME_UTC;
}
