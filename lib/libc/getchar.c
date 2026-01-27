#include <stdio.h>
#include <kernel/syscall.h>

int getchar() {
    MESSAGE msg;
    msgSend(SYS_CHAR, SYS_CHAR_GETCHAR, &msg);
    msgRecv(NULL, SYS_CHAR, &msg);
    putchar(msg.num[0]);

    return msg.num[0];
}

char *gets(char *buf) {
    int i = 0;
    while (1) {
        char c = getchar();
        if (c == '\n') {
            break;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return buf;
}