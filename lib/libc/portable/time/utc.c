/* System local time uses /etc/timezone; wall and monotonic clocks stay UTC. */
#include "time_impl.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

const char __utc[] = "UTC";
char* tzname[2] = {"UTC","UTC"};
long timezone;
int daylight;
int getdate_err;
/* Rules cover the modern named zones provided by Settings, not historical tzdb. */
static long long transition(int year,int month,int occurrence,int hour) {
    struct tm first={0};first.tm_year=year-1900;first.tm_mon=month-1;first.tm_mday=1;
    long long begin=__tm_to_secs(&first);struct tm normalized;
    if(__secs_to_tm(begin,&normalized)<0)return LLONG_MAX;
    int day=1+(7-normalized.tm_wday)%7+(occurrence-1)*7;
    if(occurrence==5) {
        struct tm next=first;++next.tm_mon;
        const int length=(int)((__tm_to_secs(&next)-begin)/86400);
        if(day>length)day-=7;
    }
    return begin+(day-1)*86400LL+hour*3600;
}
struct local_zone { int minutes,rule; };
static struct local_zone read_zone(void) {
    const char* override=getenv("TZ");struct local_zone zone={0,0};
    if(!override || !*override) {
        FILE* input=fopen("/etc/timezone","rb");
        if(input) {
            char line[128];
            while(fgets(line,sizeof(line),input)) {
                if(!strncmp(line,"offset_minutes=",15)) {
                    char* end;const long value=strtol(line+15,&end,10);
                    if(end!=line+15 && (!*end || *end=='\n' || *end=='\r') && value>=-720 && value<=840)zone.minutes=(int)value;
                }else if(!strncmp(line,"dst_rule=",9)) {
                    line[strcspn(line,"\r\n")]=0;
                    if(!strcmp(line+9,"us"))zone.rule=1;
                    else if(!strcmp(line+9,"eu"))zone.rule=2;
                    else if(!strcmp(line+9,"australia"))zone.rule=3;
                    else if(!strcmp(line+9,"new-zealand"))zone.rule=4;
                    else zone.rule=0;
                }
            }
            fclose(input);
        }
    }else if(strcmp(override,"UTC") && strcmp(override,"UTC0") && strcmp(override,"GMT") && strcmp(override,"GMT0"))errno=ENOTSUP;
    return zone;
}
static int zone_offset(const struct local_zone* zone,long long utc,int* summer) {
    const int minutes=zone->minutes,rule=zone->rule;
    *summer=0;
    struct tm date;long long start=0,end=0;int southern=0;
    if(rule && __secs_to_tm(utc,&date)==0) {
        const int year=date.tm_year+1900;
        if(rule==1){start=transition(year,3,2,2)-minutes*60;end=transition(year,11,1,2)-(minutes+60)*60;}
        else if(rule==2){start=transition(year,3,5,1);end=transition(year,10,5,1);}
        else {southern=1;start=transition(year,rule==3?10:9,rule==3?1:5,2)-minutes*60;end=transition(year,4,1,3)-(minutes+60)*60;}
        *summer=southern?(utc>=start || utc<end):(utc>=start && utc<end);
    }
    return (minutes+(*summer?60:0))*60;
}
static struct tm* convert_local(const time_t* value,struct tm* result,const struct local_zone* zone) {
    const long long seconds=(long long)*value;int summer=0;
    const int offset=zone_offset(zone,seconds,&summer);
    if ((offset>0 && seconds>LLONG_MAX-offset) || (offset<0 && seconds<LLONG_MIN-offset)) {errno=EOVERFLOW;return 0;}
    if (__secs_to_tm(seconds+offset,result)<0) {errno=EOVERFLOW;return 0;}
    result->tm_isdst=summer;result->__tm_gmtoff=offset;result->__tm_zone=summer?"LOCAL-DST":offset?"LOCAL":__utc;
    return result;
}
struct tm* localtime_r(const time_t* restrict value,struct tm* restrict result) {
    const struct local_zone zone=read_zone();
    return convert_local(value,result,&zone);
}
struct tm* localtime(const time_t* value) { static struct tm result;return localtime_r(value,&result); }
time_t mktime(struct tm* value) {
    // Read one complete configuration for the entire conversion. In an autumn
    // fold prefer standard time; normalize a spring gap forwards by one hour.
    const struct local_zone zone=read_zone();
    const long long local=__tm_to_secs(value);
    const int standard=zone.minutes*60;
    long long utc=local-standard;
    if(zone.rule) {
        if(value->tm_isdst>0)utc-=3600;
        else if(value->tm_isdst<0) {
            int summer=0;
            if(zone_offset(&zone,utc,&summer)!=standard) {
                const long long daylight=utc-3600;
                if(zone_offset(&zone,daylight,&summer)==standard+3600)utc=daylight;
            }
        }
    }
    time_t result=(time_t)utc;struct tm normalized;
    if(!convert_local(&result,&normalized,&zone))return (time_t)-1;
    *value=normalized;return result;
}
const char* __tm_to_tzname(const struct tm* value) { return value->__tm_zone ? value->__tm_zone : __utc; }
void tzset(void) {
    const struct local_zone zone=read_zone();
    timezone=-zone.minutes*60;daylight=zone.rule!=0;
    tzname[0]=zone.minutes?"LOCAL":"UTC";
    tzname[1]=zone.rule?"LOCAL-DST":tzname[0];
}

int timespec_get(struct timespec* value,int base) {
    if(base!=TIME_UTC || clock_gettime(CLOCK_REALTIME,value)) return 0;
    return TIME_UTC;
}
