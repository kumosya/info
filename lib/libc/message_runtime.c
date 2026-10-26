#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <fmtmsg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* const severities[] = {NULL, "HALT", "ERROR", "WARNING", "INFO"};
static int one(long value) { return !value || !(value & (value - 1)); }
static unsigned verbosity(void) {
    static const char* const fields[] = {"label", "severity", "text", "action", "tag"};
    const char* value = getenv("MSGVERB");
    if (!value || !*value) return 31;
    unsigned mask = 0;
    while (*value) {
        const char* end = strchr(value, ':'); size_t length = end ? (size_t)(end - value) : strlen(value);
        unsigned i;
        for (i = 0; i < 5; ++i) if (strlen(fields[i]) == length && !memcmp(value, fields[i], length)) { mask |= 1U << i; break; }
        if (i == 5) return 31;
        if (!end) break;
        value = end + 1;
        if (!*value) return 31;
    }
    return mask;
}
static char* format(unsigned mask, const char* label, int severity, const char* text, const char* action, const char* tag) {
    const char* fields[] = {label, severities[severity], text, action, tag};
    size_t size = 32;
    for (unsigned i = 0; i < 5; ++i) {
        if (!fields[i] || !(mask & (1U << i))) fields[i] = NULL;
        else { size_t length = strlen(fields[i]); if (length > 1024U * 1024U - size) { errno = EOVERFLOW; return NULL; } size += length; }
    }
    char* result = malloc(size); if (!result) return NULL;
    char* cursor = result; int present = 0;
    for (unsigned i = 0; i < 3; ++i) if (fields[i]) {
        if (present) { *cursor++ = ':'; *cursor++ = ' '; }
        size_t length = strlen(fields[i]); memcpy(cursor, fields[i], length); cursor += length; present = 1;
    }
    if (present) *cursor++ = '\n';
    if (fields[3]) { memcpy(cursor, "TO FIX: ", 8); cursor += 8; size_t length = strlen(fields[3]); memcpy(cursor, fields[3], length); cursor += length; }
    if (fields[4]) { if (fields[3]) *cursor++ = ' '; size_t length = strlen(fields[4]); memcpy(cursor, fields[4], length); cursor += length; }
    if (fields[3] || fields[4]) *cursor++ = '\n';
    *cursor = 0; return result;
}
int fmtmsg(long classification, const char* label, int severity, const char* text, const char* action, const char* tag) {
    if ((classification & ~1023L) || severity < 0 || severity > MM_INFO ||
        !one(classification & 7) || !one(classification & 56) || !one(classification & 192)) return MM_NOTOK;
    if (label) {
        const char* colon = strchr(label, ':');
        if (!colon || colon == label || colon - label > 10 || !colon[1] || strlen(colon + 1) > 14 || strchr(colon + 1, ':')) return MM_NOTOK;
    }
    int result = MM_OK;
    if (classification & MM_PRINT) {
        char* message = format(verbosity(), label, severity, text, action, tag);
        if (!message) result |= MM_NOMSG;
        else { size_t length = strlen(message); if (fwrite(message, 1, length, stderr) != length || fflush(stderr)) result |= MM_NOMSG; free(message); }
    }
    if (classification & MM_CONSOLE) {
        char* message = format(31, label, severity, text, action, tag);
        int fd = message ? open("/dev/console", O_WRONLY | O_NONBLOCK) : -1;
        if (fd < 0) result |= MM_NOCON;
        else {
            size_t size = strlen(message), written = 0;
            while (written < size) { ssize_t bytes = write(fd, message + written, size - written); if (bytes < 0 && errno == EINTR) continue; if (bytes <= 0) { result |= MM_NOCON; break; } written += (size_t)bytes; }
            if (close(fd)) result |= MM_NOCON;
        }
        free(message);
    }
    return result;
}
