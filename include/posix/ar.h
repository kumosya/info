/*
 * Unix 静态归档文件格式。文件由固定标记和连续的成员头、成员内容组成。
 */

#ifndef _POSIX_AR_H
#define _POSIX_AR_H

#define ARMAG "!<arch>\n"
#define SARMAG 8
#define ARFMAG "`\n"

/* 成员头中的数值以文本保存，整个头部占 60 字节。 */
struct ar_hdr {
        char ar_name[16];
        char ar_date[12];
        char ar_uid[6];
        char ar_gid[6];
        char ar_mode[8];
        char ar_size[10];
        char ar_fmag[2];
};

#endif
