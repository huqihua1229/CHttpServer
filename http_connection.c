#include "http_connection.h"

void init_http_request(struct HttpConnection *connection, int clientfd){
    connection->request_parse_state = PARSE_REQUEST_LINE;
    connection->line_receive_state = LINE_RECEIVE_NOT_FINISHED;
    connection->request_buffer_read_index = 0;
    connection->request_buffer_write_index = 0;
    connection->response_buffer_read_index = 0;
    connection->response_buffer_write_index = 0;
    connection->line_write_index = 0;
    connection->line_read_index = 0;
    connection->clientfd = clientfd;
    connection->fp = NULL;
    
    strcpy(connection->filename, ROOT);
}

int input_data(struct HttpConnection *connection, int fd){
    ssize_t isize = read(fd, connection->request_data_buffer + connection->request_buffer_write_index, MAX_BUFFER_SIZE - connection->request_buffer_write_index);
    connection->request_buffer_write_index += isize;
    return isize;
}

void line_read_from_buffer(struct HttpConnection *connection){
    //从 buffer 中复制到 line 的字符数量
    int copy_count;
    //从 buffer 中读取一次数据到 line 中
    //检测下 buffer 中是否有 /r/n
    char *end_p = strstr(connection->request_data_buffer + connection->request_buffer_read_index, "\r\n");
    if(end_p == NULL){
        //2023-03-08 这里出现 bug，如果当前 buffer 的结尾只有一个 \r，若只把 \r 写入 line 中会导致无法识别到行的结尾

        //先判断一下当前 buffer 中的结尾是不是 \r
        if(connection->request_data_buffer[connection->request_buffer_write_index - 1] == '\r'){
            //结尾是 \r，将这个字符保留在 buffer 中，其余的字符写入到 line 中
            copy_count = ((connection->request_buffer_write_index - 1)- connection->request_buffer_read_index)/sizeof(char);
            strncpy(connection->line + connection->line_write_index, connection->request_data_buffer + connection->request_buffer_read_index, copy_count);

            //清空 buffer
            memset(connection->request_data_buffer, 0 ,MAX_BUFFER_SIZE);
            //保留 \r
            connection->request_data_buffer[0] = '\r';
            //更新 request_buffer_read_index
            connection->request_buffer_read_index = 0;
            //更新 request_buffer_write_index
            connection->request_buffer_write_index = 1;
            //更新 line_write_index
            connection->line_write_index += copy_count;
        }else{
            //当前 buffer 中没有 /r/n，直接全部写入到 line 中
            strncpy(connection->line + connection->line_write_index, connection->request_data_buffer + connection->request_buffer_read_index, (connection->request_buffer_write_index - connection->request_buffer_read_index)/sizeof(char));
            //更新 line_write_index
            connection->line_write_index += (connection->request_buffer_write_index - connection->request_buffer_read_index)/sizeof(char);
            //清空 buffer
            memset(connection->request_data_buffer, 0 ,MAX_BUFFER_SIZE);
            //更新 request_buffer_read_index
            connection->request_buffer_read_index = 0;
            //更新 request_buffer_write_index
            connection->request_buffer_write_index = 0;
        }
    }else{
        //当前 buffer 中有 /r/n，将 /r/n 之前的字符写入到 line 中
        int read_count = (end_p - (connection->request_data_buffer + connection->request_buffer_read_index))/sizeof(char);
        strncpy(connection->line + connection->line_write_index, connection->request_data_buffer + connection->request_buffer_read_index, read_count);
        //更新 request_buffer_read_index
        connection->request_buffer_read_index += (read_count) + 2; //跳过 /r/n
        //更新 line_write_index
        connection->line_write_index += read_count;
        //此时读入了完整的行 更新状态
        connection->line_receive_state = LINE_RECEIVE_FINISHED;
    }
}

void parse_request(struct HttpConnection *connection){
    while(connection->request_buffer_write_index != 0){
        //检查当前处于什么状态
        if(connection->request_parse_state == PARSE_REQUEST_LINE){
            //正处于请求行解析的阶段
            while(1){
                if(connection->line_receive_state == LINE_RECEIVE_FINISHED || connection->request_buffer_write_index == 0){
                    break;
                }
                line_read_from_buffer(connection);
            }
            if(connection->line_receive_state == LINE_RECEIVE_FINISHED){
                LOG("LOG_DEBUG", "读取到请求行: %s", connection->line);
                //从请求行中提取信息
                char *blank_position;
                
                //提取请求方法
                blank_position = strchr(connection->line, ' ');
                *blank_position = '\0';
                if(strcmp("GET", connection->line) == 0){
                    connection->method = GET;
                }else if(strcmp("POST", connection->line) == 0){
                    connection->method = POST;
                }
                //更新 line_read_index
                connection->line_read_index = ( (blank_position - connection->line) / sizeof(char) ) + 1;
                
                //提取请求路径
                blank_position = strchr(connection->line + connection->line_read_index, ' ');
                *blank_position = '\0';
                strcpy(connection->url, connection->line + connection->line_read_index);
                //更新 line_read_index
                connection->line_read_index = ( (blank_position - connection->line) / sizeof(char) ) + 1;

                //提取 http 版本
                strcpy(connection->http_version, connection->line + connection->line_read_index);

                //清空行数据
                memset(connection->line, 0, MAX_LINE_SIZE);
                //更新 line_read_index
                connection->line_read_index = 0;
                //更新 line_write_index
                connection->line_write_index = 0;
                //更新请求解析状态
                connection->request_parse_state = PARSE_HEADER;
                connection->line_receive_state = LINE_RECEIVE_NOT_FINISHED;
            }
        }else if(connection->request_parse_state == PARSE_HEADER){
            //正处于请求头部解析的阶段
            while(1){
                if(connection->line_receive_state == LINE_RECEIVE_FINISHED || connection->request_buffer_write_index == 0){
                    break;
                }
                line_read_from_buffer(connection);
            }
            if(connection->line_receive_state == LINE_RECEIVE_FINISHED){
                LOG("LOG_DEBUG", "读取到请求头部: %s", connection->line);

                //如果是最后一行 更新请求解析状态
                if(connection->line[0] == '\0'){
                    //当前行是请求头的最后一行 "\r\n", 由于 line_read_from_buffer 在将 buffer 中的数据写入 line 时候会抛弃 "\r\n"
                    //所以在读取请求头部最后一行时写入 line 的字符为 0
                    LOG("LOG_DEBUG", "请求头部解析结束");
                    LOG("LOG_DEBUG", "请求方法: %d", connection->method);
                    LOG("LOG_DEBUG", "请求路径: %s", connection->url);
                    LOG("LOG_DEBUG", "HTTP 版本: %s", connection->http_version);
                    connection->request_parse_state = PARSE_FINISHED;
                }

                //清空行数据
                memset(connection->line, 0, MAX_LINE_SIZE);
                //更新 line_write_index
                connection->line_write_index = 0;
                //更新 line_read_index
                connection->line_read_index = 0;
                //更新行解析状态
                connection->line_receive_state = LINE_RECEIVE_NOT_FINISHED;
            }
        }else if(connection->request_parse_state == PARSE_CONTENT){
            //正处于 POST 报文请求体解析阶段
            return;
        }else{
            //处于解析完成阶段 connection->request_parse_state == PARSE_FINISHED
            return;
        }
    }
};

void output_format_data(struct HttpConnection *connection, const char *format, ...){
    //未考虑单次输入就超过缓冲区大小的情况
    va_list valist;
    va_start(valist, format);

    //向缓冲区写入数据
    int writen = vsnprintf(connection->response_data_buffer, MAX_BUFFER_SIZE, format, valist);

    //向客户端输出数据
    write(connection->clientfd, connection->response_data_buffer, writen);
    return;
}



void output_repsonse_status_line(struct HttpConnection *connection, const char *http_version, const char *status_code,  const char *message){
    output_format_data(connection, "%s %s %s\r\n", http_version, status_code, message);
}

void output_response_header(struct HttpConnection *connnection, const char *title, const char *value){
    output_format_data(connnection, "%s:%s\r\n", title, value);
}

void output_response_content_length(struct HttpConnection *connnection, long int length){
    output_format_data(connnection, "%s:%ld\r\n", "Content-Length", length);
}

void output_response_content_type(struct HttpConnection *connection, const char *type){
    output_format_data(connection, "%s:%s\r\n", "Content-Type", type);
}

void output_response_blankline(struct HttpConnection *connection){
    output_format_data(connection, "%s", "\r\n");
}

void output_file_data(struct HttpConnection *connection){
    switch (connection->response_type)
    {
        case FileRequest:
            //向请求报文体中写入文件信息
            int freadn;
            while( (freadn = fread(connection->file_buffer, sizeof(char),sizeof(connection->file_buffer), connection->fp)) != 0){
                int writen = write(connection->clientfd, connection->file_buffer, freadn);
                LOG("LOG_DEBUG", "向 fd:%d 写入 %d 字节", connection->clientfd, writen);
            }
            break;
        default:
            break;
    }
}

void do_response(struct HttpConnection *connection){
    switch (connection->method)
    {
    case GET:
        switch (connection->response_type)
        {
            case FileRequest:
                memset(connection->filename + strlen(ROOT), 0, strlen(connection->url));
                if(strlen(connection->url) == 1 && strcmp(connection->url, "/") == 0){
                    strcat(connection->filename, "/index.html");
                }else{
                    strcat(connection->filename, connection->url);
                }
                //写入状态行    
                output_repsonse_status_line(connection, "HTTP/1.1", "200", "OK");

                //在响应报文头部中写入文件类型
                if(strstr(connection->url, ".jpg") != NULL){
                    output_response_content_type(connection, "image/jpeg");
                }else if(strstr(connection->url, ".png") != NULL){
                    output_response_content_type(connection, "image/png");
                }

                char file_buffer[1024];
                if((connection->fp = fopen(connection->filename, "rb")) == NULL){
                    LOG("LOG_DEBUG", "打开文件 %s 失败", connection->filename);
                    close(connection->clientfd);
                }

                //在响应报文头部中写入响应文件大小
                fseek(connection->fp, 0, SEEK_END);
                output_response_content_length(connection, ftell(connection->fp));
                //文件指针移回头部
                rewind(connection->fp);

                //完成响应报文头部的写入
                output_response_blankline(connection);

                //传输报文体
                output_file_data(connection);

                //关闭文件指针
                fclose(connection->fp);

                //关闭 socket
                close(connection->clientfd);
                break;
            default:
                break;
        }

        break;
    default:
        break;
    }
}

void do_request(struct HttpConnection *connection){
    if(connection->request_parse_state != PARSE_FINISHED){
        parse_request(connection);
    }

    if(connection->request_parse_state == PARSE_FINISHED){
        do_response(connection);
    }
}