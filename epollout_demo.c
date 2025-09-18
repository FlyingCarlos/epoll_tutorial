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
#include <signal.h>

#define MAX_EVENTS 100
#define BUFFER_SIZE 4096
#define DEFAULT_PORT 8080
#define LARGE_DATA_SIZE (10 * 1024 * 1024) // 10MB

// 客户端写入状态结构
struct client_write_state {
    char *data;          // 待发送的数据
    size_t total_size;   // 总数据大小
    size_t sent_bytes;   // 已发送字节数
    int fd;              // 客户端fd
};

static int epoll_fd = -1;
static int listen_fd = -1;
static volatile int running = 1;

// 存储客户端写入状态的简单映射（实际项目中应使用哈希表）
static struct client_write_state write_states[MAX_EVENTS];
static int write_states_count = 0;

int make_socket_non_blocking(int fd);
void handle_new_connection();
void handle_client_read(int client_fd);
void handle_client_write(int client_fd);
void signal_handler(int sig);
struct client_write_state* find_write_state(int fd);
void remove_write_state(int fd);
void add_write_state(int fd, const char* data, size_t size);
int continue_writing(struct client_write_state* state);

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    // 创建监听socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        exit(1);
    }
    
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(DEFAULT_PORT);
    
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(1);
    }
    
    make_socket_non_blocking(listen_fd);
    
    if (listen(listen_fd, 128) == -1) {
        perror("listen");
        exit(1);
    }
    
    // 创建epoll
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(1);
    }
    
    // 添加监听socket到epoll
    struct epoll_event event;
    event.data.fd = listen_fd;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
        perror("epoll_ctl add listen_fd");
        exit(1);
    }
    
    printf("EPOLLOUT Demo Server listening on port %d\n", DEFAULT_PORT);
    printf("Connect with: nc localhost %d\n", DEFAULT_PORT);
    printf("Send 'large' to trigger large data transfer\n");
    
    struct epoll_event events[MAX_EVENTS];
    
    while (running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            uint32_t events_mask = events[i].events;
            
            printf("Event on fd %d: ", fd);
            if (events_mask & EPOLLIN) printf("EPOLLIN ");
            if (events_mask & EPOLLOUT) printf("EPOLLOUT ");
            if (events_mask & EPOLLRDHUP) printf("EPOLLRDHUP ");
            if (events_mask & EPOLLERR) printf("EPOLLERR ");
            if (events_mask & EPOLLHUP) printf("EPOLLHUP ");
            printf("\n");
            
            if (fd == listen_fd) {
                if (events_mask & EPOLLIN) {
                    handle_new_connection();
                }
            } else {
                if (events_mask & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                    printf("Client fd %d disconnected\n", fd);
                    remove_write_state(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                } else {
                    if (events_mask & EPOLLIN) {
                        handle_client_read(fd);
                    }
                    if (events_mask & EPOLLOUT) {
                        handle_client_write(fd);
                    }
                }
            }
        }
    }
    
    close(epoll_fd);
    close(listen_fd);
    return 0;
}

int make_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void handle_new_connection() {
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept");
            break;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("New connection from %s:%d (fd=%d)\n", 
               client_ip, ntohs(client_addr.sin_port), client_fd);
        
        make_socket_non_blocking(client_fd);
        
        // 只注册EPOLLIN，EPOLLOUT按需添加
        struct epoll_event event;
        event.data.fd = client_fd;
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
            perror("epoll_ctl add client_fd");
            close(client_fd);
            continue;
        }
        
        const char* msg = "Connected! Send 'large' to test EPOLLOUT handling\n";
        write(client_fd, msg, strlen(msg));
    }
}

void handle_client_read(int client_fd) {
    char buffer[BUFFER_SIZE];
    
    while (1) {
        ssize_t bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
        
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            
            // 移除换行符
            if (buffer[bytes_read - 1] == '\n') {
                buffer[bytes_read - 1] = '\0';
            }
            
            printf("Received from fd %d: %s\n", client_fd, buffer);
            
            if (strcmp(buffer, "large") == 0) {
                // 生成大数据进行发送
                char* large_data = malloc(LARGE_DATA_SIZE);
                if (!large_data) {
                    const char* error_msg = "Memory allocation failed\n";
                    write(client_fd, error_msg, strlen(error_msg));
                    return;
                }
                
                // 填充数据
                for (int i = 0; i < LARGE_DATA_SIZE; i++) {
                    large_data[i] = 'A' + (i % 26);
                }
                
                printf("Attempting to send %d bytes to fd %d\n", LARGE_DATA_SIZE, client_fd);
                
                ssize_t bytes_sent = write(client_fd, large_data, LARGE_DATA_SIZE);
                
                if (bytes_sent == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        printf("❌ EAGAIN: Socket buffer full, registering EPOLLOUT\n");
                        // 保存数据状态，注册EPOLLOUT
                        add_write_state(client_fd, large_data, LARGE_DATA_SIZE);
                    } else {
                        perror("write");
                        free(large_data);
                    }
                } else if (bytes_sent < LARGE_DATA_SIZE) {
                    printf("⚠️  Partial write: %zd/%d bytes, registering EPOLLOUT\n", 
                           bytes_sent, LARGE_DATA_SIZE);
                    // 保存剩余数据状态，注册EPOLLOUT
                    add_write_state(client_fd, large_data + bytes_sent, LARGE_DATA_SIZE - bytes_sent);
                } else {
                    printf("✅ Complete write: %zd bytes (unlikely with 10MB!)\n", bytes_sent);
                    free(large_data);
                }
            } else {
                // 普通回声
                char response[BUFFER_SIZE];
                snprintf(response, sizeof(response), "Echo: %s\n", buffer);
                write(client_fd, response, strlen(response));
            }
            
        } else if (bytes_read == 0) {
            printf("Client fd %d disconnected\n", client_fd);
            remove_write_state(client_fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
            close(client_fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("read");
            remove_write_state(client_fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
            close(client_fd);
            return;
        }
    }
}

void handle_client_write(int client_fd) {
    printf("🔄 EPOLLOUT triggered for fd %d - socket ready for writing\n", client_fd);
    
    struct client_write_state* state = find_write_state(client_fd);
    if (!state) {
        printf("❌ No write state found for fd %d\n", client_fd);
        return;
    }
    
    int result = continue_writing(state);
    
    if (result == 0) {
        // 写入完成
        printf("✅ All data sent to fd %d, removing EPOLLOUT\n", client_fd);
        remove_write_state(client_fd);
        
        // 切换回只监听EPOLLIN
        struct epoll_event event;
        event.data.fd = client_fd;
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &event);
    } else if (result == -1) {
        // 写入错误
        printf("❌ Write error for fd %d\n", client_fd);
        remove_write_state(client_fd);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        close(client_fd);
    }
    // result == 1: 还有数据要写，继续等待EPOLLOUT
}

// 查找写入状态
struct client_write_state* find_write_state(int fd) {
    for (int i = 0; i < write_states_count; i++) {
        if (write_states[i].fd == fd) {
            return &write_states[i];
        }
    }
    return NULL;
}

// 移除写入状态
void remove_write_state(int fd) {
    for (int i = 0; i < write_states_count; i++) {
        if (write_states[i].fd == fd) {
            if (write_states[i].data) {
                free(write_states[i].data);
            }
            // 将最后一个元素移到当前位置
            write_states[i] = write_states[write_states_count - 1];
            write_states_count--;
            break;
        }
    }
}

// 添加写入状态并注册EPOLLOUT
void add_write_state(int fd, const char* data, size_t size) {
    if (write_states_count >= MAX_EVENTS) {
        printf("❌ Too many write states\n");
        return;
    }
    
    struct client_write_state* state = &write_states[write_states_count++];
    state->fd = fd;
    state->data = malloc(size);
    memcpy(state->data, data, size);
    state->total_size = size;
    state->sent_bytes = 0;
    
    // 添加EPOLLOUT到现有事件
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;  // 同时监听读写
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) == -1) {
        perror("epoll_ctl MOD add EPOLLOUT");
    } else {
        printf("📝 Registered EPOLLOUT for fd %d (%zu bytes pending)\n", fd, size);
    }
}

// 继续写入数据
// 返回值: 0=完成, 1=还有数据, -1=错误
int continue_writing(struct client_write_state* state) {
    while (state->sent_bytes < state->total_size) {
        size_t remaining = state->total_size - state->sent_bytes;
        ssize_t bytes_sent = write(state->fd, 
                                  state->data + state->sent_bytes, 
                                  remaining);
        
        if (bytes_sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("⏳ Still EAGAIN on fd %d, will wait for next EPOLLOUT\n", state->fd);
                return 1; // 还有数据要写
            } else {
                perror("continue write");
                return -1; // 错误
            }
        } else if (bytes_sent == 0) {
            printf("❌ write returned 0 for fd %d\n", state->fd);
            return -1;
        }
        
        state->sent_bytes += bytes_sent;
        printf("📤 Progress fd %d: %zu/%zu bytes (%.1f%%)\n", 
               state->fd, state->sent_bytes, state->total_size,
               (double)state->sent_bytes / state->total_size * 100);
        
        if (bytes_sent < remaining) {
            // 部分写入，继续等待下次EPOLLOUT
            return 1;
        }
    }
    
    return 0; // 完成
}

void signal_handler(int sig) {
    printf("\nShutting down...\n");
    running = 0;
}