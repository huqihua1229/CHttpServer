#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#define OUTPUT_TO_CMD

void logC(const char *func, const char *file, const int line,
          const char *type, const char *format, ...)
{
    FILE *file_fp;
    time_t loacl_time;
    char time_str[128];

    // 获取本地时间
    time(&loacl_time);
    strftime(time_str, sizeof(time_str), "[%Y.%m.%d %X]", localtime(&loacl_time));
    
    // 日志内容格式转换
    va_list ap;
    va_start(ap, format);
    char fmt_str[2048];
    vsnprintf(fmt_str, sizeof(fmt_str), format, ap);
    va_end(ap);

    // 打开日志文件
    file_fp = fopen("./main.log", "a");
    
    // 写入到日志文件中
    if (file_fp != NULL)
    {
        fprintf(file_fp, "[%s]%s[%s@%s:%d] %s\n", type, time_str, func, 
                file, line, fmt_str);
        fclose(file_fp);
    }
    else
    {
        fprintf(stderr, "[%s]%s[%s@%s:%d] %s\n", type, time_str, func, 
                file, line, fmt_str);
    }

    #ifdef OUTPUT_TO_CMD
    fprintf(stdout, "[%s]%s[%s@%s:%d] %s\n", type, time_str, func, 
            file, line, fmt_str);
    #endif
}

#define LOG(type, format, ...) logC(__func__, __FILE__, __LINE__, type, format, ##__VA_ARGS__)
