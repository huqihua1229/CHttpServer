#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include "http_message.h"

#define MAXEVENTS 10
#define MAX_CLIENTS 10

int main(int argc, char *argv[]){
    //初始化服务器套接字
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    //绑定服务器地址
    struct sockaddr_in server_addr; //sockaddr_in 在 <arpa/inet.h> 中
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));
    if(bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0){
        LOG("LOG_DEBUG", "bind() 失败");
        return -1;
    }

    //将服务器套接字改为监听模式
    if(listen(server_sock, 10) != 0){
        LOG("LOG_DEBUG", "listen() 失败");
        return -1;
    }

    //创建客户端消息数组
    struct HttpMessage clients[MAX_CLIENTS];

    //创建 epoll 描述符
    int epollfd = epoll_create(1);
    
    //添加监听描述符事件
    struct epoll_event ev;
    ev.data.fd = server_sock;
    ev.events = EPOLLIN;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, server_sock, &ev);

    //等待事件发生
    while(1){
        static struct epoll_event events[MAXEVENTS];

        //等待监听的 socket 有事件发生
        int infds = epoll_wait(epollfd, events, MAXEVENTS, -1);

        //返回失败
        if(infds < 0){
            LOG("LOG_DEBUG", "epoll_wait() 失败");
            return -1;
        }

        //超时
        if(infds == 0){
            LOG("LOG_DEBUG", "epoll_wait() 超时");
            return -1;
        }

        //遍历有事件发生的结构数组
        for(int i = 0; i < infds; i++){
            if((events[i].data.fd == server_sock) && (events[i].events & EPOLLIN)){
                //如果发生事件的是 server_sock，表示有新的客户端连接上来
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);

                if(client_sock < 0){
                    LOG("LOG_DEBUG", "accept() 失败");
                    continue;
                }

                //那新的客户端添加到 epoll 中
                memset(&ev, 0, sizeof(struct epoll_event));
                ev.data.fd = client_sock;
                ev.events = EPOLLIN;
                epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &ev);

                //刷新 fd 对应的连接信息
                init_http_message(clients + client_sock);

                LOG("LOG_DEBUG", "client(%s:%d fd=%ld) 连接成功", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_sock);

                continue;
            }else if(events[i].events & EPOLLIN){
                //客户端有新的数据到达或者客户端断开连接
                char buffer[1024];
                memset(buffer, 0, sizeof(buffer));

                //读取客户端的数据
                ssize_t isize = input_data(clients + events[i].data.fd, events[i].data.fd);

                //发生错误了或 socket 被对方关闭
                if(isize <= 0){
                    LOG("LOG_DEBUG", "client(fd=%ld) 断开连接", events[i].data.fd);
                    
                    //把已断开的客户端从 epoll_fd 中删除
                    memset(&ev, 0, sizeof(struct epoll_event));
                    ev.data.fd = events[i].data.fd;
                    ev.events = EPOLLIN;
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, &ev);
                    close(events[i].data.fd);
                    continue;
                }
                
                //对报文内容进行处理
                // LOG("LOG_DEBUG", "收到来自 client(fd=%ld) 的数据: %s", events[i].data.fd, buffer);
                // //向浏览器发送 http 响应报文
                // FILE *fp = NULL;
                // char file_buffer[1024];
                // if((fp = fopen("response.txt", "r")) == NULL){
                //     LOG("LOG_DEBUG", "打开文件 /response.txt 失败");
                //     close(events[i].data.fd);
                //     continue;
                // }

                // int file_readn;
                // while( fgets(file_buffer, sizeof(buffer), fp) != NULL){
                //     write(events[i].data.fd, file_buffer, strlen(file_buffer));
                // }
                // fclose(fp);
                parse_request(clients + events[i].data.fd);

                continue;
            }
        }
    }
}