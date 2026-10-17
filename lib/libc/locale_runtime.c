/* C and UTF-8 locales use immutable objects; thread selection uses pthread TLS. */
#include <locale.h>
#include <langinfo.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <ctype.h>

struct __locale_struct { int utf8; };
static struct __locale_struct c_locale, utf8_locale = {1};
static locale_t global_locale = &c_locale;
static pthread_once_t locale_once = PTHREAD_ONCE_INIT;
static pthread_key_t locale_key;
static int locale_key_error;
static void make_locale_key(void) { locale_key_error = pthread_key_create(&locale_key, NULL); }
static locale_t current_locale(void) {
    pthread_once(&locale_once, make_locale_key);
    locale_t value = locale_key_error ? NULL : pthread_getspecific(locale_key);
    return value ? value : global_locale;
}
__attribute__((visibility("hidden"))) locale_t __libc_current_locale(void) { return current_locale(); }
size_t __ctype_get_mb_cur_max(void) { return current_locale()->utf8 ? 4 : 1; }
locale_t newlocale(int mask, const char* name, locale_t base) {
    if (!name || (mask & ~LC_ALL_MASK)) { errno = EINVAL; return NULL; }
    if (!mask) return base ? base : &c_locale;
    if (!*name || !strcmp(name,"C.UTF-8") || !strcmp(name,"C.utf8")) return &utf8_locale;
    if (!strcmp(name,"C") || !strcmp(name,"POSIX")) return &c_locale;
    errno = ENOENT; return NULL;
}
locale_t duplocale(locale_t value) {
    if (value == LC_GLOBAL_LOCALE) value = global_locale;
    if (value != &c_locale && value != &utf8_locale) { errno = EINVAL; return NULL; }
    return value;
}
void freelocale(locale_t value) { (void)value; }
locale_t uselocale(locale_t value) {
    pthread_once(&locale_once,make_locale_key);
    if (locale_key_error) { errno = locale_key_error; return NULL; }
    locale_t old = pthread_getspecific(locale_key);
    if (value) {
        if (value != LC_GLOBAL_LOCALE && value != &c_locale && value != &utf8_locale) { errno = EINVAL; return NULL; }
        int error = pthread_setspecific(locale_key,value == LC_GLOBAL_LOCALE ? NULL : value);
        if (error) { errno = error; return NULL; }
    }
    return old ? old : LC_GLOBAL_LOCALE;
}
char* setlocale(int category, const char* name) {
    if (category < LC_CTYPE || category > LC_ALL) { errno = EINVAL; return NULL; }
    if (name) {
        locale_t value = newlocale(LC_ALL_MASK,name,NULL);
        if (!value) return NULL;
        global_locale = value;
    }
    return global_locale->utf8 ? "C.UTF-8" : "C";
}
const char* getlocalename_l(int category, locale_t value) {
    if (category < LC_CTYPE || category > LC_ALL) { errno = EINVAL; return NULL; }
    if (value == LC_GLOBAL_LOCALE) value = global_locale;
    if (!duplocale(value)) return NULL;
    return value->utf8 ? "C.UTF-8" : "C";
}
struct lconv* localeconv(void) {
    static struct lconv value;
    value.decimal_point = ".";
    value.thousands_sep = value.grouping = value.int_curr_symbol = value.currency_symbol = "";
    value.mon_decimal_point = value.mon_thousands_sep = value.mon_grouping = "";
    value.positive_sign = value.negative_sign = "";
    value.int_frac_digits = value.frac_digits = value.p_cs_precedes = value.p_sep_by_space = CHAR_MAX;
    value.n_cs_precedes = value.n_sep_by_space = value.p_sign_posn = value.n_sign_posn = CHAR_MAX;
    value.int_p_cs_precedes = value.int_p_sep_by_space = value.int_n_cs_precedes = CHAR_MAX;
    value.int_n_sep_by_space = value.int_p_sign_posn = value.int_n_sign_posn = CHAR_MAX;
    return &value;
}
char* nl_langinfo_l(nl_item item, locale_t value) {
    static const char* days[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    static const char* short_days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
    static const char* short_months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    if (item >= DAY_1 && item <= DAY_7) return (char*)days[item-DAY_1];
    if (item >= ABDAY_1 && item <= ABDAY_7) return (char*)short_days[item-ABDAY_1];
    if (item >= MON_1 && item <= MON_12) return (char*)months[item-MON_1];
    if (item >= ABMON_1 && item <= ABMON_12) return (char*)short_months[item-ABMON_1];
    if (item == CODESET) return value == &c_locale ? "ASCII" : "UTF-8";
    if (item == RADIXCHAR) return ".";
    if (item == AM_STR) return "AM";
    if (item == PM_STR) return "PM";
    if (item == D_FMT) return "%m/%d/%y";
    if (item == T_FMT) return "%H:%M:%S";
    if (item == T_FMT_AMPM) return "%I:%M:%S %p";
    if (item == D_T_FMT) return "%a %b %e %H:%M:%S %Y";
    if (item == YESEXPR) return "^[yY]";
    if (item == NOEXPR) return "^[nN]";
    return "";
}
char* nl_langinfo(nl_item item) { return nl_langinfo_l(item,current_locale()); }
#define CLASSIFY(name) int name##_l(int c, locale_t locale) { (void)locale; return name(c); }
CLASSIFY(isalnum) CLASSIFY(isalpha) CLASSIFY(isblank) CLASSIFY(iscntrl)
CLASSIFY(isdigit) CLASSIFY(isgraph) CLASSIFY(islower) CLASSIFY(isprint)
CLASSIFY(ispunct) CLASSIFY(isspace) CLASSIFY(isupper) CLASSIFY(isxdigit)
CLASSIFY(tolower) CLASSIFY(toupper)
int strcoll(const char* a,const char* b) { return strcmp(a,b); }
int strcoll_l(const char* a,const char* b,locale_t l) { (void)l; return strcmp(a,b); }
size_t strxfrm(char* restrict out,const char* restrict in,size_t size) {
    size_t length = strlen(in);
    if (size) { size_t n = length < size ? length+1 : size; memcpy(out,in,n); }
    return length;
}
size_t strxfrm_l(char* restrict out,const char* restrict in,size_t size,locale_t l) { (void)l; return strxfrm(out,in,size); }
