#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>

// 客户端写入状态管理
struct write_buffer {
    char *data;
    size_t total_size;
    size_t sent_bytes;
    int fd;
};

// 演示如何正确处理 EAGAIN
void demonstrate_eagain_handling() {
    printf("\n=== EAGAIN 处理演示 ===\n");
    
    // 创建一个管道用于演示
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }
    
    // 设置写端为非阻塞
    int flags = fcntl(pipefd[1], F_GETFL, 0);
    fcntl(pipefd[1], F_SETFL, flags | O_NONBLOCK);
    
    // 准备大量数据
    const size_t data_size = 1024 * 1024; // 1MB
    char *data = malloc(data_size);
    memset(data, 'A', data_size);
    
    size_t total_sent = 0;
    int eagain_count = 0;
    int write_attempts = 0;
    
    printf("开始写入 %zu 字节数据...\n", data_size);
    
    while (total_sent < data_size) {
        ssize_t sent = write(pipefd[1], data + total_sent, data_size - total_sent);
        write_attempts++;
        
        if (sent > 0) {
            // 成功写入
            total_sent += sent;
            printf("写入 %zd 字节，总计: %zu/%zu\n", sent, total_sent, data_size);
        } else if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 缓冲区满了
                eagain_count++;
                printf("第 %d 次尝试: 缓冲区满 (EAGAIN)，已写入: %zu 字节\n", 
                       write_attempts, total_sent);
                
                // 在真实的epoll服务器中，这里会：
                // 1. 保存当前状态
                // 2. 注册 EPOLLOUT 事件
                // 3. 返回处理其他连接
                // 4. 等待 EPOLLOUT 事件再继续写入
                
                usleep(10000); // 模拟等待10ms
            } else {
                // 真正的错误
                perror("write error");
                break;
            }
        }
    }
    
    printf("\n写入完成统计:\n");
    printf("- 总写入: %zu 字节\n", total_sent);
    printf("- 写入尝试: %d 次\n", write_attempts);
    printf("- EAGAIN次数: %d 次\n", eagain_count);
    printf("- EAGAIN比例: %.1f%%\n", (float)eagain_count / write_attempts * 100);
    
    free(data);
    close(pipefd[0]);
    close(pipefd[1]);
}

// 模拟epoll服务器中的写入状态机
void simulate_epoll_write_state_machine() {
    printf("\n=== Epoll 写入状态机演示 ===\n");
    
    struct write_buffer wb = {
        .data = malloc(100000),  // 100KB数据
        .total_size = 100000,
        .sent_bytes = 0,
        .fd = -1  // 实际应用中是客户端socket
    };
    
    memset(wb.data, 'B', wb.total_size);
    
    printf("客户端请求发送 %zu 字节数据\n", wb.total_size);
    
    // 模拟不同的写入情况
    const char* scenarios[] = {
        "第一次写入: 成功写入部分数据",
        "第二次写入: 缓冲区满，返回EAGAIN", 
        "EPOLLOUT事件触发: 可以继续写入",
        "第三次写入: 完成剩余数据传输"
    };
    
    size_t chunk_sizes[] = {40000, 0, 35000, 25000}; // 模拟每次能写入的字节数
    
    for (int i = 0; i < 4; i++) {
        printf("\n%s\n", scenarios[i]);
        
        if (chunk_sizes[i] == 0) {
            printf("  → 模拟 EAGAIN: 发送缓冲区已满\n");
            printf("  → 将客户端fd注册到EPOLLOUT事件\n");
            printf("  → 返回事件循环处理其他连接...\n");
            continue;
        }
        
        size_t to_send = chunk_sizes[i];
        if (wb.sent_bytes + to_send > wb.total_size) {
            to_send = wb.total_size - wb.sent_bytes;
        }
        
        wb.sent_bytes += to_send;
        printf("  → 写入 %zu 字节\n", to_send);
        printf("  → 进度: %zu/%zu (%.1f%%)\n", 
               wb.sent_bytes, wb.total_size, 
               (float)wb.sent_bytes / wb.total_size * 100);
        
        if (wb.sent_bytes >= wb.total_size) {
            printf("  → 传输完成！切换回EPOLLIN模式\n");
            break;
        }
    }
    
    free(wb.data);
}

int main() {
    printf("EAGAIN 错误码深度解析\n");
    printf("EAGAIN = %d, EWOULDBLOCK = %d\n", EAGAIN, EWOULDBLOCK);
    printf("注意: 在大多数系统上 EAGAIN == EWOULDBLOCK\n");
    
    demonstrate_eagain_handling();
    simulate_epoll_write_state_machine();
    
    printf("\n=== 关键要点 ===\n");
    printf("1. EAGAIN 不是错误，是 '暂时不可用' 的信号\n");
    printf("2. 收到EAGAIN时应该保存状态，等待EPOLLOUT事件\n");
    printf("3. 这让单线程服务器能处理大文件传输而不阻塞\n");
    printf("4. Nginx、Redis等高性能服务器都基于这个机制\n");
    
    return 0;
}