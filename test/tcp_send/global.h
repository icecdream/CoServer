#ifndef _GLOBAL_H
#define _GLOBAL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#define UINT                unsigned int
#define UCHAR               unsigned char
#define ULONG               unsigned long
#define USHORT              unsigned short
#define ULONGLONG           unsigned long long


/*system error number*/
#define SUCCESS         0
#define FAILURE         -1

/*net socket error number*/
#define ERR_TIMEOUT         -2      //timeout
#define ERR_PROTOCOL        -3      //protocol error
#define ERR_CONNECT         -4      //connect error
#define ERR_IO              -5      //read or write error
#define ERR_OTHER           -6      //other error
#define ERR_OOM             -7      //out of memory

#define INFO(format, args...)   do {    \
    fprintf(stdout, "[INFO] %s %04d: ", __FILE__, __LINE__);    \
    fprintf(stdout, format, ##args);                \
    fprintf(stderr, "\n");                \
} while (0)

#define WARN(format, args...)   do {                \
    fprintf(stdout, "[WARN] %s %04d: ", __FILE__, __LINE__);    \
    fprintf(stdout, format, ##args);                \
    fprintf(stderr, "\n");                \
} while (0)

#define PRINT(format, args...)  do {                \
    fprintf(stdout, format, ##args);                \
    fprintf(stderr, "\n");                \
    fflush(stdout);                         \
} while (0)

#define ERROR(format, args...)  do {                \
    fprintf(stderr, "[ERROR] %s %04d: ", __FILE__, __LINE__);   \
    fprintf(stderr, format, ##args);                \
    fprintf(stderr, "\n");                \
} while (0)

#define DEBUG(format, args...) do {                \
    fprintf(stdout, "[DEBUG] %s %04d: ", __FILE__, __LINE__);   \
    fprintf(stdout, format, ##args);                \
    fprintf(stderr, "\n");                \
    fflush(stdout);                         \
} while (0)


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GLOBAL_H */
