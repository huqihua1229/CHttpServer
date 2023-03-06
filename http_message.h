#include <string.h>
#include "log.h"
#include <unistd.h>
#include <stdlib.h>

#define MAX_BUFFER_SIZE 256
#define MAX_LINE_SIZE 256
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

struct HttpRequest{
    enum RequestParseState request_parse_state;
    enum LineReceiveState line_receive_state;

    int clientfd;

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

    char filename[256];     //传输的文件在磁盘中的位置
};

void init_http_request(struct HttpRequest *request, int clientfd){
    request->request_parse_state = PARSE_REQUEST_LINE;
    request->line_receive_state = LINE_RECEIVE_NOT_FINISHED;
    request->buffer_read_index = 0;
    request->buffer_write_index = 0;
    request->line_write_index = 0;
    request->line_read_index = 0;
    request->clientfd = clientfd;
    
    strcpy(request->filename, ROOT);
}

int input_data(struct HttpRequest *request, int fd){
    ssize_t isize = read(fd, request->buffer + request->buffer_write_index, MAX_BUFFER_SIZE - request->buffer_write_index);
    request->buffer_write_index += isize;
    return isize;
}

void line_read_from_buffer(struct HttpRequest *request){
    //从 buffer 中读取一次数据到 line 中
    //检测下 buffer 中是否有 /r/n
    char *end_p = strstr(request->buffer + request->buffer_read_index, "\r\n");
    if(end_p == NULL){
        //当前 buffer 中没有 /r/n，直接全部写入到 line 中(这里未考虑到一行超过 MAX_LINE_SIZE 字节的情况)
        strncpy(request->line + request->line_write_index, request->buffer + request->buffer_read_index, (request->buffer_write_index - request->buffer_read_index)/sizeof(char));
        //更新 line_write_index
        request->line_write_index += (request->buffer_write_index - request->buffer_read_index)/sizeof(char);
        //清空 buffer
        memset(request->buffer, 0 ,MAX_BUFFER_SIZE);
        //更新 buffer_read_index
        request->buffer_read_index = 0;
        //更新 buffer_write_index
        request->buffer_write_index = 0;
    }else{
        //当前 buffer 中有 /r/n，将 /r/n 之前的字符写入到 line 中
        int read_count = (end_p - (request->buffer + request->buffer_read_index))/sizeof(char);
        strncpy(request->line + request->line_write_index, request->buffer + request->buffer_read_index, read_count);
        //更新 buffer_read_index
        request->buffer_read_index += (read_count) + 2; //跳过 /r/n
        //更新 line_write_index
        request->line_write_index += read_count;
        //此时读入了完整的行 更新状态
        request->line_receive_state = LINE_RECEIVE_FINISHED;
    }
}

void parse_request(struct HttpRequest *request){
    while(request->buffer_write_index != 0){
        //检查当前处于什么状态
        if(request->request_parse_state == PARSE_REQUEST_LINE){
            //正处于请求行解析的阶段
            while(1){
                if(request->line_receive_state == LINE_RECEIVE_FINISHED || request->buffer_write_index == 0){
                    break;
                }
                line_read_from_buffer(request);
            }
            if(request->line_receive_state == LINE_RECEIVE_FINISHED){
                LOG("LOG_DEBUG", "读取到请求行: %s", request->line);
                //从请求行中提取信息
                char *blank_position;
                
                //提取请求方法
                blank_position = strchr(request->line, ' ');
                *blank_position = '\0';
                if(strcmp("GET", request->line) == 0){
                    request->method = GET;
                }else if(strcmp("POST", request->line) == 0){
                    request->method = POST;
                }
                //更新 line_read_index
                request->line_read_index = ( (blank_position - request->line) / sizeof(char) ) + 1;
                
                //提取请求路径
                blank_position = strchr(request->line + request->line_read_index, ' ');
                *blank_position = '\0';
                strcpy(request->url, request->line + request->line_read_index);
                //更新 line_read_index
                request->line_read_index = ( (blank_position - request->line) / sizeof(char) ) + 1;

                //提取 http 版本
                strcpy(request->http_version, request->line + request->line_read_index);

                //清空行数据
                memset(request->line, 0, MAX_LINE_SIZE);
                //更新 line_read_index
                request->line_read_index = 0;
                //更新 line_write_index
                request->line_write_index = 0;
                //更新请求解析状态
                request->request_parse_state = PARSE_HEADER;
                request->line_receive_state = LINE_RECEIVE_NOT_FINISHED;
            }
        }else if(request->request_parse_state == PARSE_HEADER){
            //正处于请求头部解析的阶段
            while(1){
                if(request->line_receive_state == LINE_RECEIVE_FINISHED || request->buffer_write_index == 0){
                    break;
                }
                line_read_from_buffer(request);
            }
            if(request->line_receive_state == LINE_RECEIVE_FINISHED){
                LOG("LOG_DEBUG", "读取到请求头部: %s", request->line);

                //如果是最后一行 更新请求解析状态
                if(request->line[0] == '\0'){
                    //当前行是请求头的最后一行 "\r\n", 由于 line_read_from_buffer 在将 buffer 中的数据写入 line 时候会抛弃 "\r\n"
                    //所以在读取请求头部最后一行时写入 line 的字符为 0
                    LOG("LOG_DEBUG", "请求头部解析结束");
                    LOG("LOG_DEBUG", "请求方法: %d", request->method);
                    LOG("LOG_DEBUG", "请求路径: %s", request->url);
                    LOG("LOG_DEBUG", "HTTP 版本: %s", request->http_version);
                    request->request_parse_state = PARSE_FINISHED;
                }

                //清空行数据
                memset(request->line, 0, MAX_LINE_SIZE);
                //更新 line_write_index
                request->line_write_index = 0;
                //更新 line_read_index
                request->line_read_index = 0;
                //更新行解析状态
                request->line_receive_state = LINE_RECEIVE_NOT_FINISHED;
            }
        }else if(request->request_parse_state == PARSE_CONTENT){
            //正处于 POST 报文请求体解析阶段
            return;
        }else{
            //处于解析完成阶段 request->request_parse_state == PARSE_FINISHED
            return;
        }
    }
};

void insert_response_status_line(const struct HttpRequest *request, const char *line){
    if(write(request->clientfd, line, strlen(line)) == -1){
        LOG("LOG_DEBUG", "写入响应报文状态行 %s 失败", line);
        close(request->clientfd);
        return;
    }

    if(write(request->clientfd, "\r\n", 2) == -1){
        LOG("LOG_DEBUG", "写入响应报文状态行 \\r\\n 失败");
        close(request->clientfd);
        return;
    }
}

void insert_response_header(const struct HttpRequest *request, const char *title,const char *value){
    if(write(request->clientfd, title, strlen(title)) == -1){
        LOG("LOG_DEBUG", "写入响应报文头部 %s 失败", title);
        close(request->clientfd);
        return;
    }
    if(write(request->clientfd, ":", 1) == -1){
        LOG("LOG_DEBUG", "写入响应报文头部 %s 失败", ":");
        close(request->clientfd);
        return;
    }
    if(write(request->clientfd, value, strlen(value)) == -1){
        LOG("LOG_DEBUG", "写入响应报文头部 %s 失败", value);
        close(request->clientfd);
        return;
    }
    if(write(request->clientfd, "\r\n", 2) == -1){
        LOG("LOG_DEBUG", "写入响应报文头部 \\r\\n 失败");
        close(request->clientfd);
        return;
    }
}

void insert_blank_line(struct HttpRequest *request){
    if(write(request->clientfd, "\r\n", 2) == -1){
        LOG("LOG_DEBUG", "写入响应报文空行失败");
        close(request->clientfd);
        return;
    }
}

void do_response(struct HttpRequest *request){
    switch (request->method)
    {
    case GET:
        memset(request->filename + strlen(ROOT), 0, strlen(request->url));
        if(strlen(request->url) == 1 && strcmp(request->url, "/") == 0){
            strcat(request->filename, "/index.html");
        }else{
            strcat(request->filename, request->url);
        }
        //写入请求行
        insert_response_status_line(request, "HTTP/1.1 200 OK");

        //写入头部

        //写入空行
        insert_blank_line(request);

        //传输文件
        FILE *fp = NULL;
        char file_buffer[1024];
        if((fp = fopen(request->filename, "r")) == NULL){
            LOG("LOG_DEBUG", "打开文件 %s 失败", request->filename);
            close(request->clientfd);
        }

        while( fgets(file_buffer, sizeof(file_buffer), fp) != NULL){
            int writen = write(request->clientfd, file_buffer, strlen(file_buffer));
            LOG("LOG_DEBUG", "向 fd:%d 写入 %d 字节", request->clientfd, writen);
        }
        fclose(fp);
        close(request->clientfd);
        break;
    default:
        break;
    }
}

void do_request(struct HttpRequest *request){
    if(request->request_parse_state != PARSE_FINISHED){
        parse_request(request);
    }

    if(request->request_parse_state == PARSE_FINISHED){
        do_response(request);
    }
}