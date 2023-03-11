#include <string.h>
#include "log.h"
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdarg.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_LINE_SIZE 1024
#define MAX_PARAM_BUFFER_SIZE 1024
#define ROOT "root"       //能被客户端请求的文件的根目录

enum RequestParseState {    //报文解析的阶段
    PARSE_REQUEST_LINE,     //正在解析请求行
    PARSE_HEADER,           //正在解析请求头
    PARSE_CONTENT,          //正在解析请求体
    PARSE_PARAM,            //正在解析请求参数
    PARSE_FINISHED,         //完成报文解析
    PARSE_FAILED            //请求报文格式有问题，无法解析
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
    enum ResponseType response_type;

    int has_param;      //0:没有请求参数; 1:有请求参数

    int clientfd;

    char request_data_buffer[MAX_BUFFER_SIZE];      //存放请求数据的缓冲区，保障最后一个字符是 '0'
    char response_data_buffer[MAX_BUFFER_SIZE];     //存放响应数据的缓冲区
    char line[MAX_LINE_SIZE];                       //存放请求头部每一行的数据，保障最后一个字符是 '0'
    char param_buffer[MAX_PARAM_BUFFER_SIZE];       //解析 GET 和 POST 请求参数时暂存参数内容的缓冲区
    char *content;                                  //指向存放报文体的内存区域 仅用于 POST 请求

    int request_buffer_read_index;      //指向缓冲区 buffer 中第一个未放入 line 的字符(未开始解析)的下标
    int request_buffer_write_index;     //指向缓冲区中最后一个被写入的字符的下一位(标志 read 函数该从哪里开始写入)
    int line_write_index;               //指向 line 中第一个可以写入的位置的下标
    int line_read_index;

    enum HttpMethod method; //请求行中的请求方法
    char url[256];          //请求行中的 url
    char http_version[10];  //请求行中的 http 按本

    char host[256];                             //请求头部中的 host 字段

    char filename[256];     //传输的文件在磁盘中的位置
    char file_buffer[1024]; // 文件传输的缓冲区
    FILE *fp;               //打开的文件
};

//初始化连接
void init_http_connection(struct HttpConnection *connection, int clientfd);

//将客户端传输的数据读取到 HttpConnection 的请求数据缓冲区 request_data_buffer
int input_data(struct HttpConnection *connection, int fd);

//从请求数据缓冲区 request_data_buffer 中取出数据到行缓冲区 line 中
void line_read_from_buffer(struct HttpConnection *connection);

//解析请求报文请求行
void parse_request_line(struct HttpConnection *connection);

//解析请求报文头部
void parse_header(struct HttpConnection *connection);

//解析请求报文
void parse_request(struct HttpConnection *connection);

//向客户端输出格式化的数据
void output_format_data(struct HttpConnection *connection, const char *format, ...);

//输出响应报文的状态行
void output_repsonse_status_line(struct HttpConnection *connection, const char *http_version, const char *status_code,  const char *message);

//输出响应报文的头部字段
void output_response_header(struct HttpConnection *connnection, const char *title, const char *value);

//输出响应报文的报文体长度
void output_response_content_length(struct HttpConnection *connnection, long int length);

//输出响应报文的响应内容类型
void output_response_content_type(struct HttpConnection *connection, const char *type);

//输出空行
void output_response_blankline(struct HttpConnection *connection);

//输出文件数据
void output_file_data(struct HttpConnection *connection);

//执行响应操作
void do_response(struct HttpConnection *connection);

//执行解析操作
void process(struct HttpConnection *connection);