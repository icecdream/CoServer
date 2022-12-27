#ifndef _CO_LOG_H_
#define _CO_LOG_H_

#include <string>
#include <string.h>
#include <stdio.h>


namespace coserver
{

extern int32_t g_logLevel;
extern thread_local int32_t g_innerThreadId;
static const std::string g_logLevelString[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

#define filename(x) strrchr(x,'/')?strrchr(x,'/')+1:x

#define STDOUT_LOG(level, fmt, args...) \
    do { \
        if (level >= g_logLevel) { \
            struct timeval tv;    \
            struct tm tm;    \
            gettimeofday(&tv, NULL);  \
            localtime_r(&tv.tv_sec, &tm); \
            fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%02d.%03d [%u] [%s] (%s:%d) " fmt "\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int32_t) (tv.tv_usec / 1000), g_innerThreadId, g_logLevelString[level - 1].c_str(), filename(__FILE__), __LINE__, ##args); \
        } \
    } while (0); \


// debug
#if (CO_DEBUG)

#define CO_SERVER_LOG_DEBUG(fmt, args...) \
    do { \
        STDOUT_LOG(1, fmt, ##args); \
    } while(0); \

#else   /* !CO_DEBUG */

#define CO_SERVER_LOG_DEBUG(fmt, args...)

#endif

// info
#define CO_SERVER_LOG_INFO(fmt, args...) \
    do { \
        STDOUT_LOG(2, fmt, ##args); \
    } while(0); \

// warn
#define CO_SERVER_LOG_WARN(fmt, args...) \
    do { \
        STDOUT_LOG(3, fmt, ##args); \
    } while(0); \

// error
#define CO_SERVER_LOG_ERROR(fmt, args...) \
    do { \
        STDOUT_LOG(4, fmt, ##args); \
    } while(0); \

// fatal
#define CO_SERVER_LOG_FATAL(fmt, args...) \
    do { \
        STDOUT_LOG(5, fmt, ##args); \
    } while(0); \

}

#endif //_CO_LOG_H_

