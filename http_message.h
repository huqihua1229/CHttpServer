#include <string.h>
#include "log.h"
#include <unistd.h>
#include <stdlib.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_LINE_SIZE 1024

enum MessageParseState {    //报文解析的阶段
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

struct HttpMessage{
    enum MessageParseState message_parse_state;
    enum LineReceiveState line_receive_state;

    char buffer[MAX_BUFFER_SIZE];   //存放请求头部数据的缓冲区，保障最后一个字符是 '0'
    char line[MAX_LINE_SIZE];       //存放请求头部每一行的数据，保障最后一个字符是 '0'
    char *content;                  //指向存放报文体的内存区域 仅用于 POST 请求

    int buffer_read_index;     //指向缓冲区 buffer 中第一个未放入 line 的字符(未开始解析)的下标
    int buffer_write_index;    //指向缓冲区中最后一个被写入的字符的下一位(标志 read 函数该从哪里开始写入)
    int line_write_index;     //指向 line 中第一个可以写入的位置的下标
    int line_read_index;

    enum HttpMethod method; //请求行中的请求方法
    char url[256];          //请求行中的 url
    char http_version[10];  //请求行中的 http 按本

    char host[256];         //请求头部中的 host 字段
};

void init_http_message(struct HttpMessage *message){
    message->message_parse_state = PARSE_REQUEST_LINE;
    message->line_receive_state = LINE_RECEIVE_NOT_FINISHED;
    message->buffer_read_index = 0;
    message->buffer_write_index = 0;
    message->line_write_index = 0;
    message->line_read_index = 0;
}

int input_data(struct HttpMessage *message, int fd){
    ssize_t isize = read(fd, message->buffer + message->buffer_write_index, MAX_BUFFER_SIZE - message->buffer_write_index);
    message->buffer_write_index += isize;
    return isize;
}

void line_read_from_buffer(struct HttpMessage *message){
    //从 buffer 中读取一次数据到 line 中
    //检测下 buffer 中是否有 /r/n
    char *end_p = strstr(message->buffer + message->buffer_read_index, "\r\n");
    if(end_p == NULL){
        //当前 buffer 中没有 /r/n，直接全部写入到 line 中(这里未考虑到一行超过 MAX_LINE_SIZE 字节的情况)
        strncpy(message->line + message->line_write_index, message->buffer + message->buffer_read_index, (message->buffer_write_index - message->buffer_read_index)/sizeof(char));
        //清空 buffer
        memset(message->buffer, 0 ,MAX_BUFFER_SIZE);
        //更新 buffer_read_index
        message->buffer_read_index = 0;
        //更新 line_write_index
        message->line_write_index = 0;
    }else{
        //当前 buffer 中有 /r/n，将 /r/n 之前的字符写入到 line 中
        int read_count = (end_p - (message->buffer + message->buffer_read_index))/sizeof(char);
        strncpy(message->line + message->line_write_index, message->buffer + message->buffer_read_index, read_count);
        //更新 buffer_read_index
        message->buffer_read_index += (read_count) + 2; //跳过 /r/n
        //更新 line_write_index
        message->line_write_index += read_count;
        //此时读入了完整的行 更新状态
        message->line_receive_state = LINE_RECEIVE_FINISHED;
    }
}

void parse_request(struct HttpMessage *message){
    while(message->buffer_write_index != 0){
        //检查当前处于什么状态
        if(message->message_parse_state == PARSE_REQUEST_LINE){
            //正处于请求行解析的阶段
            while(1){
                if(message->line_receive_state == LINE_RECEIVE_FINISHED || message->buffer_write_index == 0){
                    break;
                }
                line_read_from_buffer(message);
            }
            if(message->line_receive_state == LINE_RECEIVE_FINISHED){
                LOG("LOG_DEBUG", "读取到请求行: %s", message->line);
                //从请求行中提取信息
                char *blank_position;
                
                //提取请求方法
                blank_position = strchr(message->line, ' ');
                *blank_position = '\0';
                if(strcmp("GET", message->line) == 0){
                    message->method = GET;
                }else if(strcmp("POST", message->line) == 0){
                    message->method = POST;
                }
                //更新 line_read_index
                message->line_read_index = ( (blank_position - message->line) / sizeof(char) ) + 1;
                
                //提取请求路径
                blank_position = strchr(message->line + message->line_read_index, ' ');
                *blank_position = '\0';
                strcpy(message->url, message->line + message->line_read_index);
                //更新 line_read_index
                message->line_read_index = ( (blank_position - message->line) / sizeof(char) ) + 1;

                //提取 http 版本
                strcpy(message->http_version, message->line + message->line_read_index);

                //清空行数据
                memset(message->line, 0, sizeof(MAX_LINE_SIZE));
                //更新 line_read_index
                message->line_read_index = 0;
                //更新 line_write_index
                message->line_write_index = 0;
                //更新请求解析状态
                message->message_parse_state = PARSE_HEADER;
                message->line_receive_state = LINE_RECEIVE_NOT_FINISHED;
            }
        }else if(message->message_parse_state == PARSE_HEADER){
            //正处于请求头部解析的阶段
            while(1){
                if(message->line_receive_state == LINE_RECEIVE_FINISHED || message->buffer_write_index == 0){
                    break;
                }
                line_read_from_buffer(message);
            }
            if(message->line_receive_state == LINE_RECEIVE_FINISHED){
                LOG("LOG_DEBUG", "读取到请求头部: %s", message->line);

                //如果是最后一行 更新请求解析状态
                if(message->line[0] == '\0'){
                    //当前行是请求头的最后一行 "\r\n", 由于 line_read_from_buffer 在将 buffer 中的数据写入 line 时候会抛弃 "\r\n"
                    //所以在读取请求头部最后一行时写入 line 的字符为 0
                    LOG("LOG_DEBUG", "请求头部解析结束");
                    LOG("LOG_DEBUG", "请求方法: %d", message->method);
                    LOG("LOG_DEBUG", "请求路径: %s", message->url);
                    LOG("LOG_DEBUG", "HTTP 版本: %s", message->http_version);
                    message->message_parse_state = PARSE_FINISHED;
                }

                //清空行数据
                memset(message->line, 0, sizeof(MAX_LINE_SIZE));
                //更新 line_write_index
                message->line_write_index = 0;
                //更新行解析状态
                message->line_receive_state = LINE_RECEIVE_NOT_FINISHED;
            }
        }else if(message->message_parse_state == PARSE_CONTENT){
            //正处于 POST 报文请求体解析阶段
            return;
        }else{
            //处于解析完成阶段 message->message_parse_state == PARSE_FINISHED
            return;
        }
    }
};