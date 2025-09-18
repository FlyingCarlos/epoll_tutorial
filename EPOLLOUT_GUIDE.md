# EPOLLOUT事件处理指南

## 为什么需要EPOLLOUT？

当`write()`调用遇到以下情况时，我们需要使用EPOLLOUT事件：

1. **EAGAIN/EWOULDBLOCK**: 内核发送缓冲区满，无法写入任何数据
2. **部分写入**: 只有部分数据被写入缓冲区，剩余数据需要稍后发送

## 处理流程

### 1. 初始状态
```c
// 客户端socket初始只注册EPOLLIN
event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
```

### 2. 检测写入问题
```c
ssize_t bytes_sent = write(client_fd, data, data_size);

if (bytes_sent == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 情况1: 完全写入失败 - 注册EPOLLOUT
        save_pending_data(client_fd, data, data_size);
        register_epollout(client_fd);
    }
} else if (bytes_sent < data_size) {
    // 情况2: 部分写入 - 注册EPOLLOUT
    save_pending_data(client_fd, data + bytes_sent, data_size - bytes_sent);
    register_epollout(client_fd);
}
```

### 3. 注册EPOLLOUT事件
```c
void register_epollout(int client_fd) {
    struct epoll_event event;
    event.data.fd = client_fd;
    // 同时监听读写事件
    event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &event);
}
```

### 4. 处理EPOLLOUT事件
```c
void handle_epollout(int client_fd) {
    // 继续发送待发送的数据
    int result = continue_writing(client_fd);
    
    if (result == WRITE_COMPLETE) {
        // 所有数据发送完成，移除EPOLLOUT
        struct epoll_event event;
        event.data.fd = client_fd;
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;  // 只保留EPOLLIN
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &event);
        
        free_pending_data(client_fd);
    }
    // 如果仍有数据待发送，继续等待下一个EPOLLOUT
}
```

## 关键要点

1. **按需添加EPOLLOUT**: 不要一开始就监听EPOLLOUT，只在需要时添加
2. **及时移除EPOLLOUT**: 数据发送完成后立即移除，避免不必要的事件触发
3. **状态管理**: 需要保存每个客户端的待发送数据状态
4. **渐进式发送**: 每次EPOLLOUT事件可能仍然只能发送部分数据

## 实际运行示例

```
Attempting to send 10485760 bytes to fd 5
⚠️  Partial write: 2621440/10485760 bytes, registering EPOLLOUT
📝 Registered EPOLLOUT for fd 5 (7864320 bytes pending)

Event on fd 5: EPOLLOUT 
🔄 EPOLLOUT triggered for fd 5 - socket ready for writing
📤 Progress fd 5: 2193654/7864320 bytes (27.9%)

Event on fd 5: EPOLLOUT 
🔄 EPOLLOUT triggered for fd 5 - socket ready for writing
📤 Progress fd 5: 4027178/7864320 bytes (51.2%)

... (多次EPOLLOUT事件) ...

Event on fd 5: EPOLLOUT 
🔄 EPOLLOUT triggered for fd 5 - socket ready for writing
📤 Progress fd 5: 7864320/7864320 bytes (100.0%)
✅ All data sent to fd 5, removing EPOLLOUT
```

这个过程展示了：
- 初始写入了2.6MB，还剩7.8MB待发送
- 通过多次EPOLLOUT事件逐步发送完所有数据
- 每次可发送的量取决于内核缓冲区可用空间

## 注意事项

1. **内存管理**: 待发送数据需要保存在内存中，注意内存泄漏
2. **连接断开**: 客户端断开时需要清理待发送数据
3. **超时处理**: 长时间无法发送可能需要超时机制
4. **背压控制**: 避免积累太多待发送数据