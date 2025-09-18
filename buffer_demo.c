#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

void demonstrate_send_receive_buffers() {
    printf("=== TCP 数据传输流程演示 ===\n\n");
    
    printf("1. 应用程序调用 write()\n");
    printf("   ┌─────────────┐\n");
    printf("   │ 用户数据    │\n");
    printf("   └─────────────┘\n");
    printf("         │ write()\n");
    printf("         ▼\n");
    
    printf("2. 数据进入本地内核发送缓冲区\n");
    printf("   ┌─────────────────────────────┐\n");
    printf("   │ 内核发送缓冲区 (SO_SNDBUF)   │ ← write() 在这里返回成功！\n");
    printf("   └─────────────────────────────┘\n");
    printf("         │ 异步发送\n");
    printf("         ▼\n");
    
    printf("3. TCP 协议栈处理\n");
    printf("   ┌─────────────────────────────┐\n");
    printf("   │ TCP 分段、添加头部、校验等    │\n");
    printf("   └─────────────────────────────┘\n");
    printf("         │ 通过网络\n");
    printf("         ▼\n");
    
    printf("4. 网络传输\n");
    printf("   ┌─────────────────────────────┐\n");
    printf("   │ 路由器、交换机、网络延迟...   │\n");
    printf("   └─────────────────────────────┘\n");
    printf("         │\n");
    printf("         ▼\n");
    
    printf("5. 到达对端内核接收缓冲区\n");
    printf("   ┌─────────────────────────────┐\n");
    printf("   │ 对端内核接收缓冲区 (SO_RCVBUF)│\n");
    printf("   └─────────────────────────────┘\n");
    printf("         │ read()\n");
    printf("         ▼\n");
    
    printf("6. 对端应用程序读取\n");
    printf("   ┌─────────────┐\n");
    printf("   │ 对端程序    │\n");
    printf("   └─────────────┘\n");
    
    printf("\n✅ write() 成功 = 数据进入了步骤2（本地发送缓冲区）\n");
    printf("❌ write() 成功 ≠ 对方收到数据（需要到达步骤5）\n\n");
}

void demonstrate_buffer_behavior() {
    printf("=== 缓冲区行为演示 ===\n\n");
    
    // 创建套接字对
    int sockpair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) == -1) {
        perror("socketpair");
        return;
    }
    
    // 设置非阻塞
    int flags = fcntl(sockpair[0], F_GETFL, 0);
    fcntl(sockpair[0], F_SETFL, flags | O_NONBLOCK);
    
    // 获取发送缓冲区大小
    int send_buf_size, recv_buf_size;
    socklen_t opt_len = sizeof(int);
    getsockopt(sockpair[0], SOL_SOCKET, SO_SNDBUF, &send_buf_size, &opt_len);
    getsockopt(sockpair[1], SOL_SOCKET, SO_RCVBUF, &recv_buf_size, &opt_len);
    
    printf("发送缓冲区大小: %d 字节\n", send_buf_size);
    printf("接收缓冲区大小: %d 字节\n", recv_buf_size);
    printf("\n");
    
    // 准备数据
    char data[1024];
    memset(data, 'A', sizeof(data));
    
    size_t total_written = 0;
    int write_count = 0;
    
    printf("开始写入数据...\n");
    
    while (write_count < 200) {  // 限制测试次数
        ssize_t written = write(sockpair[0], data, sizeof(data));
        write_count++;
        
        if (written > 0) {
            total_written += written;
            printf("第 %d 次写入: %zd 字节成功 (总计: %zu 字节)\n", 
                   write_count, written, total_written);
        } else if (written == -1 && errno == EAGAIN) {
            printf("第 %d 次写入: EAGAIN - 发送缓冲区已满！\n", write_count);
            printf("此时已写入 %zu 字节到发送缓冲区\n", total_written);
            printf("但对方一个字节都没有读取！\n");
            break;
        } else {
            printf("写入错误: %s\n", strerror(errno));
            break;
        }
    }
    
    printf("\n=== 关键观察 ===\n");
    printf("1. write() 成功写入了 %zu 字节\n", total_written);
    printf("2. 但对方程序还没有调用 read()\n");
    printf("3. 数据都在本地发送缓冲区中等待发送\n");
    printf("4. 这证明了 write() 成功 ≠ 对方收到数据\n");
    
    // 现在让对方读取一些数据
    printf("\n现在让对方读取一些数据...\n");
    char read_buf[2048];
    ssize_t read_bytes = read(sockpair[1], read_buf, sizeof(read_buf));
    if (read_bytes > 0) {
        printf("对方读取了 %zd 字节\n", read_bytes);
        
        // 现在应该可以再写入一些数据了
        ssize_t more_written = write(sockpair[0], data, sizeof(data));
        if (more_written > 0) {
            printf("读取后，又可以写入 %zd 字节了！\n", more_written);
        }
    }
    
    close(sockpair[0]);
    close(sockpair[1]);
}

void explain_tcp_reliability() {
    printf("\n=== TCP 可靠性机制 ===\n\n");
    
    printf("TCP 如何保证数据最终到达：\n");
    printf("1. 序列号: 每个字节都有序列号\n");
    printf("2. 确认机制: 接收方发送 ACK 确认\n");
    printf("3. 重传机制: 未收到 ACK 会重传\n");
    printf("4. 流量控制: 根据接收方缓冲区调整发送速度\n");
    printf("5. 拥塞控制: 根据网络状况调整发送速度\n");
    
    printf("\n但是：\n");
    printf("- write() 不会等待 ACK\n");
    printf("- write() 只是把数据交给内核\n");
    printf("- 内核负责后续的发送和重传\n");
    printf("- 应用程序如需确认到达，需要应用层协议\n");
}

int main() {
    demonstrate_send_receive_buffers();
    demonstrate_buffer_behavior();
    explain_tcp_reliability();
    
    printf("\n=== 总结 ===\n");
    printf("write() 返回成功意味着：\n");
    printf("✅ 数据已复制到本地内核发送缓冲区\n");
    printf("✅ 内核会负责发送这些数据\n");
    printf("✅ TCP 会保证可靠传输（除非连接断开）\n");
    printf("\n");
    printf("write() 返回成功不意味着：\n");
    printf("❌ 数据已经发送到网络\n");
    printf("❌ 对方已经收到数据\n");
    printf("❌ 对方应用程序已经处理数据\n");
    
    return 0;
}