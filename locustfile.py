#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Locust 性能测试脚本 for epoll_server.c

这个脚本测试 epoll 服务器的各种功能：
- TCP 连接建立和关闭
- 各种命令的响应时间
- 并发连接处理能力
- 长连接和短连接性能

运行方式:
1. 启动服务器: ./epoll_server 8080
2. 运行测试: locust -f locustfile.py --host=localhost:8080
"""

import socket
import time
import random
import logging
from typing import Optional

from locust import User, task, events
from locust.exception import LocustError


class TCPSocketClient:
    """TCP Socket 客户端，用于与 epoll 服务器通信"""
    
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.connected = False
        
    def connect(self) -> None:
        """建立 TCP 连接"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(10)  # 10秒超时
            
            start_time = time.time()
            self.socket.connect((self.host, self.port))
            end_time = time.time()
            
            # 接收欢迎消息
            welcome = self.socket.recv(1024).decode('utf-8', errors='ignore')
            self.connected = True
            
            # 记录连接时间
            events.request.fire(
                request_type="TCP",
                name="connect",
                response_time=(end_time - start_time) * 1000,
                response_length=len(welcome),
                exception=None
            )
            
        except Exception as e:
            self.connected = False
            events.request.fire(
                request_type="TCP",
                name="connect",
                response_time=0,
                response_length=0,
                exception=e
            )
            raise LocustError(f"Failed to connect: {e}")
    
    def disconnect(self) -> None:
        """关闭 TCP 连接"""
        if self.socket and self.connected:
            try:
                self.socket.close()
                self.connected = False
            except Exception as e:
                logging.warning(f"Error closing socket: {e}")
    
    def send_command(self, command: str, name: Optional[str] = None) -> str:
        """发送命令并接收响应"""
        if not self.connected or not self.socket:
            raise LocustError("Not connected to server")
        
        if name is None:
            name = command.split()[0]  # 使用命令的第一个词作为名称
        
        try:
            start_time = time.time()
            
            # 发送命令
            message = command + '\n'
            self.socket.send(message.encode('utf-8'))
            
            # 接收响应
            response = self.socket.recv(4096).decode('utf-8', errors='ignore')
            
            end_time = time.time()
            
            # 记录请求
            events.request.fire(
                request_type="CMD",
                name=name,
                response_time=(end_time - start_time) * 1000,
                response_length=len(response),
                exception=None
            )
            
            return response
            
        except Exception as e:
            events.request.fire(
                request_type="CMD",
                name=name,
                response_time=0,
                response_length=0,
                exception=e
            )
            raise LocustError(f"Command failed: {e}")


class EpollServerUser(User):
    """epoll 服务器性能测试用户"""
    
    # 用户行为权重配置
    weight = 1
    wait_time_min = 0.1  # 最小等待时间（秒）
    wait_time_max = 2.0  # 最大等待时间（秒）
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        
        # 解析主机和端口
        if ':' in self.host:
            host_part, port_part = self.host.split(':', 1)
            self.server_host = host_part
            self.server_port = int(port_part)
        else:
            self.server_host = self.host
            self.server_port = 8080
        
        self.client = None
        
        # 命令集合用于随机测试
        self.commands = [
            "ping",
            "time", 
            "help",
            "echo hello",
            "echo world",
            "echo test message",
            "echo locust performance testing"
        ]
    
    def on_start(self):
        """用户开始时的初始化操作"""
        self.client = TCPSocketClient(self.server_host, self.server_port)
        self.client.connect()
    
    def on_stop(self):
        """用户结束时的清理操作"""
        if self.client:
            # 发送 quit 命令优雅断开
            try:
                self.client.send_command("quit")
            except:
                pass
            self.client.disconnect()
    
    def wait_time(self):
        """用户等待时间"""
        return random.uniform(self.wait_time_min, self.wait_time_max)
    
    @task(10)
    def ping_command(self):
        """ping 命令测试 - 最常用的命令"""
        response = self.client.send_command("ping")
        if "pong" not in response.lower():
            raise LocustError(f"Unexpected ping response: {response}")
    
    @task(5)
    def time_command(self):
        """time 命令测试"""
        response = self.client.send_command("time")
        if "current time" not in response.lower():
            raise LocustError(f"Unexpected time response: {response}")
    
    @task(8)
    def echo_command(self):
        """echo 命令测试 - 测试不同长度的消息"""
        message_lengths = [
            "short",
            "medium length message for testing",
            "this is a longer message to test the echo functionality of the epoll server",
            "a" * 100,  # 100 字符
            "b" * 500,  # 500 字符
        ]
        
        test_message = random.choice(message_lengths)
        command = f"echo {test_message}"
        response = self.client.send_command(command, name="echo")
        
        if test_message not in response:
            raise LocustError(f"Echo failed. Expected '{test_message}' in response, got: {response}")
    
    @task(2)
    def help_command(self):
        """help 命令测试"""
        response = self.client.send_command("help")
        if "available commands" not in response.lower():
            raise LocustError(f"Unexpected help response: {response}")
    
    @task(3)
    def random_command_sequence(self):
        """随机命令序列测试 - 模拟真实用户行为"""
        num_commands = random.randint(2, 5)
        for _ in range(num_commands):
            command = random.choice(self.commands)
            try:
                self.client.send_command(command, name="random_sequence")
                # 命令间小延迟
                time.sleep(random.uniform(0.01, 0.1))
            except Exception as e:
                logging.warning(f"Random command failed: {e}")
                break


class StressTestUser(EpollServerUser):
    """压力测试用户 - 更激进的测试模式"""
    
    weight = 1
    wait_time_min = 0.01  # 更短的等待时间
    wait_time_max = 0.1
    
    @task(20)
    def rapid_ping(self):
        """快速 ping 测试"""
        self.client.send_command("ping", name="stress_ping")
    
    @task(10)
    def rapid_echo(self):
        """快速 echo 测试"""
        message = f"stress_test_{random.randint(1, 1000)}"
        self.client.send_command(f"echo {message}", name="stress_echo")


class LongConnectionUser(EpollServerUser):
    """长连接测试用户 - 保持连接长时间活跃"""
    
    weight = 1
    wait_time_min = 1.0   # 更长的等待时间
    wait_time_max = 5.0
    
    @task(5)
    def periodic_ping(self):
        """定期 ping 保持连接活跃"""
        self.client.send_command("ping", name="long_conn_ping")
    
    @task(3)
    def periodic_time(self):
        """定期获取时间"""
        self.client.send_command("time", name="long_conn_time")
    
    @task(2)
    def large_echo(self):
        """大消息 echo 测试"""
        large_message = "x" * random.randint(1000, 3000)
        self.client.send_command(f"echo {large_message}", name="large_echo")


class ShortConnectionUser(User):
    """短连接测试用户 - 每次请求都重新建立连接"""
    
    weight = 1
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        
        # 解析主机和端口
        if ':' in self.host:
            host_part, port_part = self.host.split(':', 1)
            self.server_host = host_part
            self.server_port = int(port_part)
        else:
            self.server_host = self.host
            self.server_port = 8080
    
    def wait_time(self):
        return random.uniform(0.5, 2.0)
    
    @task
    def short_connection_test(self):
        """短连接测试 - 连接、发送命令、立即断开"""
        client = TCPSocketClient(self.server_host, self.server_port)
        try:
            client.connect()
            
            # 随机选择一个命令
            commands = ["ping", "time", "echo short_test"]
            command = random.choice(commands)
            
            response = client.send_command(command, name="short_conn")
            
            # 发送 quit 命令
            client.send_command("quit", name="short_conn_quit")
            
        except Exception as e:
            logging.warning(f"Short connection test failed: {e}")
        finally:
            client.disconnect()


# 事件监听器 - 用于收集额外的性能指标
@events.test_start.add_listener
def on_test_start(environment, **kwargs):
    print("=" * 50)
    print("开始 epoll 服务器性能测试")
    print(f"目标服务器: {environment.host}")
    print("=" * 50)


@events.test_stop.add_listener 
def on_test_stop(environment, **kwargs):
    print("=" * 50)
    print("epoll 服务器性能测试完成")
    print("=" * 50)