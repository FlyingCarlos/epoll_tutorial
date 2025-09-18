#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

void demonstrate_blocking_vs_nonblocking() {
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event events[10];
    
    printf("=== 阻塞模式演示 ===\n");
    printf("调用 epoll_wait 阻塞模式，超时 1 秒...\n");
    
    time_t start = time(NULL);
    int nfds = epoll_wait(epoll_fd, events, 10, 1000); // 1秒超时
    time_t end = time(NULL);
    
    printf("返回值: %d, 耗时: %ld 秒\n", nfds, end - start);
    printf("在这 1 秒内，进程是休眠的，CPU 可以处理其他任务\n\n");
    
    printf("=== 非阻塞模式演示 ===\n");
    printf("连续调用 epoll_wait 非阻塞模式 5 次...\n");
    
    for (int i = 0; i < 5; i++) {
        start = time(NULL);
        nfds = epoll_wait(epoll_fd, events, 10, 0); // 立即返回
        end = time(NULL);
        
        printf("第 %d 次调用: 返回值 %d, 耗时 %ld 秒\n", i+1, nfds, end - start);
        usleep(200000); // 0.2秒，避免刷屏
    }
    
    printf("\n非阻塞模式立即返回，但如果频繁调用就变成了轮询\n");
    
    close(epoll_fd);
}

int main() {
    demonstrate_blocking_vs_nonblocking();
    return 0;
}