#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

void test_partial_write_with_pipe() {
    printf("=== 管道部分写入测试 ===\n");
    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }
    
    // 设置写端为非阻塞
    int flags = fcntl(pipefd[1], F_GETFL, 0);
    fcntl(pipefd[1], F_SETFL, flags | O_NONBLOCK);
    
    // 先填满管道缓冲区的大部分
    char filler[1024];
    memset(filler, 'X', sizeof(filler));
    
    size_t total_written = 0;
    while (1) {
        ssize_t written = write(pipefd[1], filler, sizeof(filler));
        if (written <= 0) break;
        total_written += written;
        if (total_written > 60000) break; // 写入足够多的数据
    }
    
    printf("预填充管道: %zu 字节\n", total_written);
    
    // 现在尝试写入一个大块数据，应该会部分写入
    char big_data[10000];
    memset(big_data, 'A', sizeof(big_data));
    
    printf("\n尝试写入 %zu 字节的大数据块...\n", sizeof(big_data));
    
    ssize_t result = write(pipefd[1], big_data, sizeof(big_data));
    
    if (result > 0) {
        if (result < sizeof(big_data)) {
            printf("✅ 部分写入！成功写入 %zd/%zu 字节 (%.1f%%)\n", 
                   result, sizeof(big_data), (float)result/sizeof(big_data)*100);
            printf("   errno = %d (%s) - 注意不是EAGAIN！\n", errno, strerror(errno));
        } else {
            printf("完全写入：%zd 字节\n", result);
        }
    } else if (result == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("EAGAIN：缓冲区满，0字节写入\n");
        } else {
            printf("其他错误：%s\n", strerror(errno));
        }
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
}

int main() {
    test_partial_write_with_pipe();
    return 0;
}