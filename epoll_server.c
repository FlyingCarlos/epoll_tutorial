#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>

#define MAX_EVENTS 1000
#define BUFFER_SIZE 4096
#define DEFAULT_PORT 8080
#define LISTEN_BACKLOG 128

// 全局变量
static int epoll_fd = -1;
static int listen_fd = -1;
static volatile int running = 1;

// 函数声明
int create_and_bind(int port);
int make_socket_non_blocking(int fd);
void handle_new_connection(int listen_fd, int epoll_fd);
void handle_client_message(int client_fd, int epoll_fd);
void handle_client_disconnect(int client_fd, int epoll_fd);
void process_message(const char* request, char* response, int response_size);
void signal_handler(int sig);
void cleanup_and_exit();

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    
    // 解析命令行参数
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number: %d\n", port);
            exit(EXIT_FAILURE);
        }
    }
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN); // 忽略SIGPIPE
    
    printf("Starting epoll server on port %d...\n", port);
    
    // 1. 创建和绑定监听socket
    listen_fd = create_and_bind(port);
    if (listen_fd == -1) {
        exit(EXIT_FAILURE);
    }
    
    // 2. 设置为非阻塞
    if (make_socket_non_blocking(listen_fd) == -1) {
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    // 3. 开始监听
    if (listen(listen_fd, LISTEN_BACKLOG) == -1) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    // 4. 创建epoll实例
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    // 5. 将监听socket注册到epoll
    struct epoll_event event;
    event.data.fd = listen_fd;
    event.events = EPOLLIN | EPOLLET; // 边沿触发
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
        perror("epoll_ctl: listen_fd");
        cleanup_and_exit();
    }
    
    printf("Server listening on 0.0.0.0:%d\n", port);
    printf("Press Ctrl+C to stop the server\n");
    
    // 6. 主事件循环
    struct epoll_event events[MAX_EVENTS];
    
    while (running) {
        // "number of fds"（就绪文件描述符数量）
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000); // 1秒超时
        
        if (nfds == -1) {
            if (errno == EINTR) {
                continue; // 被信号中断，继续
            }
            perror("epoll_wait");
            break;
        }
        
        // 处理所有就绪的事件
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            uint32_t events_mask = events[i].events;
            
            // 打印事件详情（调试用）
            printf("Event on fd %d: ", fd);
            if (events_mask & EPOLLIN) printf("EPOLLIN ");
            if (events_mask & EPOLLOUT) printf("EPOLLOUT ");
            if (events_mask & EPOLLRDHUP) printf("EPOLLRDHUP ");
            if (events_mask & EPOLLPRI) printf("EPOLLPRI ");
            if (events_mask & EPOLLERR) printf("EPOLLERR ");
            if (events_mask & EPOLLHUP) printf("EPOLLHUP ");
            // 注意：EPOLLET 是触发模式标志，不会出现在返回的事件中
            printf("\n");
            
            if (fd == listen_fd) {
                // 监听套接字事件
                if (events_mask & EPOLLIN) {
                    // 新连接到达
                    handle_new_connection(listen_fd, epoll_fd);
                } else if (events_mask & EPOLLERR) {
                    // 监听套接字错误
                    printf("Error on listen socket fd %d\n", fd);
                    perror("listen socket error");
                    // 在实际应用中，可能需要重新创建监听套接字
                } else if (events_mask & EPOLLHUP) {
                    // 监听套接字挂起（极少见）
                    printf("Listen socket fd %d hung up\n", fd);
                }
            } else {
                // 客户端连接事件
                if (events_mask & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                    // 连接错误、挂起或对端关闭写操作
                    printf("Connection error/close detected on fd %d\n", fd);
                    handle_client_disconnect(fd, epoll_fd);
                } else if (events_mask & EPOLLIN) {
                    // 有数据可读
                    handle_client_message(fd, epoll_fd);
                } else if (events_mask & EPOLLOUT) {
                    // 可写事件（处理大量数据写入或从EAGAIN恢复）
                    printf("Socket fd %d ready for writing (recovered from EAGAIN)\n", fd);
                    // 在实际应用中，这里会：
                    // 1. 从保存的缓冲区继续发送数据
                    // 2. 发送完成后切换回EPOLLIN模式
                    // 3. 释放发送缓冲区内存
                } else if (events_mask & EPOLLPRI) {
                    // 紧急数据
                    printf("Priority data available on fd %d\n", fd);
                }
            }
        }
    }
    
    cleanup_and_exit();
    return 0;
}

// 创建并绑定socket
int create_and_bind(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return -1;
    }
    
    // 设置SO_REUSEADDR选项
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        close(fd);
        return -1;
    }
    
    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(fd);
        return -1;
    }
    
    return fd;
}

// 设置socket为非阻塞模式
int make_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL");
        return -1;
    }
    
    return 0;
}

// 处理新连接
void handle_new_connection(int listen_fd, int epoll_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // 循环接受所有等待的连接（边沿触发模式）
    while (1) {
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有更多连接了
                break;
            }
            perror("accept");
            break;
        }
        
        // 打印客户端信息
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("New connection from %s:%d (fd=%d)\n", 
               client_ip, ntohs(client_addr.sin_port), client_fd);
        
        // 设置客户端socket为非阻塞
        if (make_socket_non_blocking(client_fd) == -1) {
            close(client_fd);
            continue;
        }
        
        // 将新的客户端fd注册到epoll
        struct epoll_event event;
        event.data.fd = client_fd;
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET; // 边沿触发，监听可读事件和对端关闭
        
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
            perror("epoll_ctl: client_fd");
            close(client_fd);
            continue;
        }
        
        // 发送欢迎消息
        const char* welcome_msg = "Welcome to Carlos's Echo Server!\n";
        write(client_fd, welcome_msg, strlen(welcome_msg));
    }
}

// 处理客户端消息
void handle_client_message(int client_fd, int epoll_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    // 循环读取所有可用数据（边沿触发模式）
    while (1) {
        bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
        
        if (bytes_read > 0) {
            // 收到数据
            buffer[bytes_read] = '\0';
            
            // 移除末尾的换行符
            if (buffer[bytes_read - 1] == '\n') {
                buffer[bytes_read - 1] = '\0';
                bytes_read--;
            }
            
            printf("Received from fd %d: %s\n", client_fd, buffer);
            
            // 处理消息并生成回复
            char response[BUFFER_SIZE];
            process_message(buffer, response, BUFFER_SIZE);
            
            // 发送回复到内核发送缓冲区
            ssize_t response_len = strlen(response);
            ssize_t bytes_sent = write(client_fd, response, response_len);
            
            if (bytes_sent == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("write");
                    handle_client_disconnect(client_fd, epoll_fd);
                    return;
                }
                // EAGAIN: 本地发送缓冲区满，需要等待
                // 在高性能服务器中，这里需要：
                // 1. 保存未发送的数据
                // 2. 注册 EPOLLOUT 事件
                // 3. 等待套接字可写
            } else if (bytes_sent != response_len) {
                // 部分数据进入发送缓冲区，剩余数据需要后续处理
                // 在高性能服务器中，这里需要：
                // 1. 保存剩余的 (response_len - bytes_sent) 字节
                // 2. 注册 EPOLLOUT 事件继续发送
                printf("Warning: partial write (%zd/%zd bytes)\n", bytes_sent, response_len);
                // 注意：即使部分写入成功，也不代表客户端已收到任何数据
            }
            
        } else if (bytes_read == 0) {
            // 客户端正常关闭连接
            printf("Client fd %d disconnected\n", client_fd);
            handle_client_disconnect(client_fd, epoll_fd);
            return;
            
        } else {
            // bytes_read == -1
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有更多数据了
                break;
            } else {
                // 读取错误
                perror("read");
                handle_client_disconnect(client_fd, epoll_fd);
                return;
            }
        }
    }
}

// 处理客户端断开连接
void handle_client_disconnect(int client_fd, int epoll_fd) {
    printf("Closing connection fd %d\n", client_fd);
    
    // 从epoll中移除
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL) == -1) {
        perror("epoll_ctl DEL");
    }
    
    // 关闭socket
    close(client_fd);
}

// 处理消息并生成回复
void process_message(const char* request, char* response, int response_size) {
    if (strlen(request) == 0) {
        snprintf(response, response_size, "Empty message received\n");
        return;
    }
    
    // 简单的命令处理
    if (strcmp(request, "ping") == 0) {
        snprintf(response, response_size, "pong\n");
    } else if (strcmp(request, "time") == 0) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        snprintf(response, response_size, "Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    } else if (strcmp(request, "quit") == 0 || strcmp(request, "exit") == 0) {
        snprintf(response, response_size, "Goodbye!\n");
    } else if (strncmp(request, "echo ", 5) == 0) {
        snprintf(response, response_size, "%s\n", request + 5);
    } else if (strcmp(request, "help") == 0) {
        snprintf(response, response_size, 
                "Available commands:\n"
                "  ping     - responds with pong\n"
                "  time     - shows current time\n"
                "  echo <msg> - echoes your message\n"
                "  help     - shows this help\n"
                "  quit/exit - disconnect\n");
    } else {
        // 默认回声
        snprintf(response, response_size, "Echo: %s\n", request);
    }
}

// 信号处理函数
void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down gracefully...\n", sig);
    running = 0;
}

// 清理资源并退出
void cleanup_and_exit() {
    printf("Cleaning up resources...\n");
    
    if (epoll_fd != -1) {
        close(epoll_fd);
    }
    
    if (listen_fd != -1) {
        close(listen_fd);
    }
    
    printf("Server shutdown complete\n");
    exit(EXIT_SUCCESS);
}