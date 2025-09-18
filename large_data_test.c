#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DATA_SIZE (100 * 1024 * 1024)  // 100MB
#define BUFFER_SIZE (64 * 1024)        // 64KB chunks

// 测试阻塞 vs 非阻塞写入性能
void test_blocking_write(int sockfd) {
    char *data = malloc(DATA_SIZE);
    if (!data) {
        perror("malloc");
        return;
    }
    
    // 填充测试数据
    memset(data, 'A', DATA_SIZE);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    ssize_t total_sent = 0;
    while (total_sent < DATA_SIZE) {
        ssize_t sent = write(sockfd, data + total_sent, DATA_SIZE - total_sent);
        if (sent <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Would block at %zd bytes\n", total_sent);
                break;
            }
            perror("write");
            break;
        }
        total_sent += sent;
        printf("Sent chunk: %zd bytes, total: %zd/%d\n", sent, total_sent, DATA_SIZE);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("Total sent: %zd bytes in %.3f seconds\n", total_sent, elapsed);
    printf("Throughput: %.2f MB/s\n", (total_sent / 1024.0 / 1024.0) / elapsed);
    
    free(data);
}

void test_nonblocking_write(int sockfd) {
    // 设置为非阻塞
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    char *data = malloc(DATA_SIZE);
    if (!data) {
        perror("malloc");
        return;
    }
    
    memset(data, 'B', DATA_SIZE);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    ssize_t total_sent = 0;
    int write_attempts = 0;
    int would_block_count = 0;
    
    while (total_sent < DATA_SIZE) {
        ssize_t sent = write(sockfd, data + total_sent, 
                           DATA_SIZE - total_sent > BUFFER_SIZE ? 
                           BUFFER_SIZE : DATA_SIZE - total_sent);
        write_attempts++;
        
        if (sent > 0) {
            total_sent += sent;
            printf("NonBlocking sent: %zd bytes, total: %zd/%d\n", 
                   sent, total_sent, DATA_SIZE);
        } else if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                would_block_count++;
                printf("Would block (attempt %d), sleeping...\n", write_attempts);
                usleep(10000); // 10ms
                continue;
            } else {
                perror("write");
                break;
            }
        }
        
        // 模拟处理其他连接的时间
        if (write_attempts % 10 == 0) {
            usleep(1000); // 1ms 处理其他任务
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("NonBlocking results:\n");
    printf("Total sent: %zd bytes in %.3f seconds\n", total_sent, elapsed);
    printf("Write attempts: %d, would-block count: %d\n", write_attempts, would_block_count);
    printf("Throughput: %.2f MB/s\n", (total_sent / 1024.0 / 1024.0) / elapsed);
    
    free(data);
}

int main() {
    printf("Large Data Write Test\n");
    printf("Testing with %d MB of data\n", DATA_SIZE / 1024 / 1024);
    
    // 创建一个 socket pair 进行测试
    int sockpair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) == -1) {
        perror("socketpair");
        exit(1);
    }
    
    printf("\n=== Testing Non-blocking Write ===\n");
    test_nonblocking_write(sockpair[0]);
    
    // 清空接收缓冲区
    char buffer[BUFFER_SIZE];
    while (read(sockpair[1], buffer, BUFFER_SIZE) > 0) {
        // 消费数据
    }
    
    close(sockpair[0]);
    close(sockpair[1]);
    
    return 0;
}