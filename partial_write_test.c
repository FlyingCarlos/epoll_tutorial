#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

void test_partial_write_vs_eagain() {
    printf("=== 部分写入 vs EAGAIN 测试 ===\n\n");
    
    // 创建一个套接字对用于测试
    int sockpair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) == -1) {
        perror("socketpair");
        return;
    }
    
    // 设置写端为非阻塞
    int flags = fcntl(sockpair[0], F_GETFL, 0);
    fcntl(sockpair[0], F_SETFL, flags | O_NONBLOCK);
    
    // 准备测试数据
    const size_t chunk_size = 32 * 1024; // 32KB
    char *data = malloc(chunk_size);
    memset(data, 'A', chunk_size);
    
    size_t total_sent = 0;
    int write_count = 0;
    int partial_writes = 0;
    int eagain_count = 0;
    
    printf("开始写入测试，每次尝试写入 %zu 字节\n\n", chunk_size);
    
    // 持续写入直到遇到 EAGAIN
    while (write_count < 50) { // 限制测试次数
        ssize_t sent = write(sockpair[0], data, chunk_size);
        write_count++;
        
        if (sent > 0) {
            total_sent += sent;
            
            if (sent < chunk_size) {
                // 部分写入
                partial_writes++;
                printf("第 %d 次写入: 部分成功 %zd/%zu 字节 (%.1f%%)\n", 
                       write_count, sent, chunk_size, (float)sent/chunk_size*100);
                printf("  → 注意: errno 不是 EAGAIN, 而是: %s\n", strerror(errno));
            } else {
                // 完全写入
                printf("第 %d 次写入: 完全成功 %zd 字节\n", write_count, sent);
            }
        } else if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // EAGAIN - 缓冲区满
                eagain_count++;
                printf("第 %d 次写入: EAGAIN - 缓冲区满，0 字节写入\n", write_count);
                printf("  → 总共已写入: %zu 字节，现在需要等待\n", total_sent);
                break; // 停止测试
            } else {
                printf("第 %d 次写入: 其他错误 - %s\n", write_count, strerror(errno));
                break;
            }
        }
    }
    
    printf("\n=== 测试结果统计 ===\n");
    printf("总写入次数: %d\n", write_count);
    printf("总写入字节: %zu (%.2f MB)\n", total_sent, total_sent/1024.0/1024.0);
    printf("部分写入次数: %d\n", partial_writes);
    printf("EAGAIN 次数: %d\n", eagain_count);
    printf("平均每次写入: %.0f 字节\n", (float)total_sent/write_count);
    
    if (partial_writes > 0) {
        printf("\n✅ 发现部分写入！这证明了部分写入不会报 EAGAIN\n");
    } else {
        printf("\n📝 本次测试未出现部分写入，但原理依然成立\n");
    }
    
    if (eagain_count > 0) {
        printf("✅ 遇到 EAGAIN！这时缓冲区完全满了，一个字节都写不进去\n");
    }
    
    free(data);
    close(sockpair[0]);
    close(sockpair[1]);
}

void demonstrate_write_scenarios() {
    printf("\n=== 写入场景详解 ===\n\n");
    
    printf("场景1: 完全成功写入\n");
    printf("  write(fd, data, 1000) = 1000\n");
    printf("  → errno: 未设置\n");
    printf("  → 操作: 继续下一个任务\n\n");
    
    printf("场景2: 部分写入\n");
    printf("  write(fd, data, 1000) = 600\n");
    printf("  → errno: 未设置 (不是EAGAIN!)\n");
    printf("  → 操作: 继续写入剩余400字节\n\n");
    
    printf("场景3: EAGAIN\n");
    printf("  write(fd, data, 1000) = -1\n");
    printf("  → errno: EAGAIN\n");
    printf("  → 操作: 等待EPOLLOUT事件，然后重试\n\n");
    
    printf("场景4: 真正的错误\n");
    printf("  write(fd, data, 1000) = -1\n");
    printf("  → errno: ECONNRESET, EPIPE 等\n");
    printf("  → 操作: 关闭连接\n\n");
}

int main() {
    test_partial_write_vs_eagain();
    demonstrate_write_scenarios();
    
    printf("=== 关键要点 ===\n");
    printf("1. 部分写入 (返回值 > 0) 不会设置 EAGAIN\n");
    printf("2. EAGAIN 只在完全无法写入时发生 (返回值 = -1)\n");
    printf("3. 部分写入需要继续写剩余数据\n");
    printf("4. EAGAIN 需要等待 EPOLLOUT 事件\n");
    
    return 0;
}