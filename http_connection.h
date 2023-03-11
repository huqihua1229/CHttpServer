#include <string.h>
#include "log.h"
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdarg.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_LINE_SIZE 1024
#define ROOT "root"       //能被客户端请求的文件的根目录

enum RequestParseState {    //报文解析的阶段
    PARSE_REQUEST_LINE,     //正在解析请求行
    PARSE_HEADER,           //正在解析请求头
    PARSE_CONTENT,          //正在解析请求体
    PARSE_FINISHED          //完成报文解析
};

enum LineReceiveState {         //是否接收到完整的一行 用于解析请求行和请求体阶段
    LINE_RECEIVE_FINISHED,      //一行接收完成
    LINE_RECEIVE_NOT_FINISHED   //一行接收未完成
};

enum HttpMethod {
    GET,
    POST
};

enum ResponseType {
    FileRequest             //返回文件
};

struct HttpConnection{
    enum RequestParseState request_parse_state;
    enum LineReceiveState line_receive_state;

    int clientfd;

    char request_data_buffer[MAX_BUFFER_SIZE];  //存放请求数据的缓冲区，保障最后一个字符是 '0'
    char response_data_buffer[MAX_BUFFER_SIZE];  //存放响应数据的缓冲区
    char line[MAX_LINE_SIZE];       //存放请求头部每一行的数据，保障最后一个字符是 '0'
    char *content;                  //指向存放报文体的内存区域 仅用于 POST 请求

    int request_buffer_read_index;     //指向缓冲区 buffer 中第一个未放入 line 的字符(未开始解析)的下标
    int request_buffer_write_index;    //指向缓冲区中最后一个被写入的字符的下一位(标志 read 函数该从哪里开始写入)
    int response_buffer_read_index;
    int response_buffer_write_index;
    int line_write_index;     //指向 line 中第一个可以写入的位置的下标
    int line_read_index;

    enum HttpMethod method; //请求行中的请求方法
    char url[256];          //请求行中的 url
    char http_version[10];  //请求行中的 http 按本

    char host[256];         //请求头部中的 host 字段

    enum ResponseType response_type;

    char filename[256];     //传输的文件在磁盘中的位置
    char file_buffer[1024]; // 文件传输的缓冲区
    FILE *fp;               //打开的文件
};

void init_http_request(struct HttpConnection *connection, int clientfd);

int input_data(struct HttpConnection *connection, int fd);

void line_read_from_buffer(struct HttpConnection *connection);

void parse_request(struct HttpConnection *connection);

void output_format_data(struct HttpConnection *connection, const char *format, ...);

void output_repsonse_status_line(struct HttpConnection *connection, const char *http_version, const char *status_code,  const char *message);

void output_response_header(struct HttpConnection *connnection, const char *title, const char *value);

void output_response_content_length(struct HttpConnection *connnection, long int length);

void output_response_content_type(struct HttpConnection *connection, const char *type);

void output_response_blankline(struct HttpConnection *connection);

void output_file_data(struct HttpConnection *connection);

void do_response(struct HttpConnection *connection);

void do_request(struct HttpConnection *connection);