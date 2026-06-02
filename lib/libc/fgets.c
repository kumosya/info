#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

char *fgets(char *s, int size, FILE *stream) {
    if (!s || size <= 0 || !stream) {
        return NULL;
    }
    
    int fd = (int)(uintptr_t)stream;
    
    int i = 0;
    while (i < size - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n != 1) {
            break;
        }
        s[i++] = c;
        if (c == '\n') {
            break;
        }
    }
    s[i] = '\0';
    return (i > 0) ? s : NULL;
}
