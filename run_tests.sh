#!/bin/bash

# epoll 服务器性能测试脚本
# 使用方法: ./run_tests.sh [test_type] [server_port]

SERVER_PORT=${2:-8080}
TEST_TYPE=${1:-basic}

echo "=========================================="
echo "epoll 服务器性能测试"
echo "=========================================="
echo "服务器端口: $SERVER_PORT"
echo "测试类型: $TEST_TYPE"
echo "=========================================="

# 检查服务器是否正在运行
check_server() {
    echo "检查服务器是否在端口 $SERVER_PORT 运行..."
    if ! nc -z localhost $SERVER_PORT; then
        echo "错误: 服务器未在端口 $SERVER_PORT 运行"
        echo "请先启动服务器: ./epoll_server $SERVER_PORT"
        exit 1
    fi
    echo "✓ 服务器正在运行"
}

# 基础性能测试
run_basic_test() {
    echo "运行基础性能测试..."
    echo "- 100 个用户"
    echo "- 每秒增加 10 个用户"
    echo "- 运行 2 分钟"
    
    uv run locust -f locustfile.py \
        --host=localhost:$SERVER_PORT \
        --users=100 \
        --spawn-rate=10 \
        --run-time=2m \
        --headless \
        --html=basic_test_report.html \
        --csv=basic_test
}

# 压力测试
run_stress_test() {
    echo "运行压力测试..."
    echo "- 500 个用户"
    echo "- 每秒增加 20 个用户"
    echo "- 运行 3 分钟"
    echo "- 使用 StressTestUser"
    
    uv run locust -f locustfile.py \
        --host=localhost:$SERVER_PORT \
        --users=500 \
        --spawn-rate=20 \
        --run-time=3m \
        --headless \
        --html=stress_test_report.html \
        --csv=stress_test \
        StressTestUser
}

# 长连接测试
run_longconn_test() {
    echo "运行长连接测试..."
    echo "- 50 个用户"
    echo "- 每秒增加 5 个用户"
    echo "- 运行 5 分钟"
    echo "- 使用 LongConnectionUser"
    
    uv run locust -f locustfile.py \
        --host=localhost:$SERVER_PORT \
        --users=50 \
        --spawn-rate=5 \
        --run-time=5m \
        --headless \
        --html=longconn_test_report.html \
        --csv=longconn_test \
        LongConnectionUser
}

# 短连接测试
run_shortconn_test() {
    echo "运行短连接测试..."
    echo "- 200 个用户"
    echo "- 每秒增加 15 个用户"
    echo "- 运行 2 分钟"
    echo "- 使用 ShortConnectionUser"
    
    uv run locust -f locustfile.py \
        --host=localhost:$SERVER_PORT \
        --users=200 \
        --spawn-rate=15 \
        --run-time=2m \
        --headless \
        --html=shortconn_test_report.html \
        --csv=shortconn_test \
        ShortConnectionUser
}

# 混合测试
run_mixed_test() {
    echo "运行混合负载测试..."
    echo "- 300 个用户"
    echo "- 每秒增加 15 个用户"
    echo "- 运行 5 分钟"
    echo "- 混合所有用户类型"
    
    uv run locust -f locustfile.py \
        --host=localhost:$SERVER_PORT \
        --users=300 \
        --spawn-rate=15 \
        --run-time=5m \
        --headless \
        --html=mixed_test_report.html \
        --csv=mixed_test
}

# Web UI 模式
run_web_ui() {
    echo "启动 Locust Web UI..."
    echo "打开浏览器访问: http://localhost:8089"
    echo "按 Ctrl+C 停止测试"
    
    uv run locust -f locustfile.py --host=localhost:$SERVER_PORT
}

# 主逻辑
check_server

case $TEST_TYPE in
    basic)
        run_basic_test
        ;;
    stress)
        run_stress_test
        ;;
    longconn)
        run_longconn_test
        ;;
    shortconn)
        run_shortconn_test
        ;;
    mixed)
        run_mixed_test
        ;;
    web|ui)
        run_web_ui
        ;;
    all)
        echo "运行所有测试场景..."
        run_basic_test
        echo "等待 10 秒..."
        sleep 10
        run_stress_test
        echo "等待 10 秒..."
        sleep 10
        run_longconn_test
        echo "等待 10 秒..."
        sleep 10
        run_shortconn_test
        ;;
    *)
        echo "用法: $0 [test_type] [server_port]"
        echo ""
        echo "测试类型:"
        echo "  basic     - 基础性能测试 (默认)"
        echo "  stress    - 压力测试"
        echo "  longconn  - 长连接测试"
        echo "  shortconn - 短连接测试"
        echo "  mixed     - 混合负载测试"
        echo "  web|ui    - 启动 Web UI 界面"
        echo "  all       - 运行所有测试"
        echo ""
        echo "示例:"
        echo "  $0 basic 8080     # 基础测试，端口 8080"
        echo "  $0 stress         # 压力测试，默认端口 8080"
        echo "  $0 web            # 启动 Web UI"
        exit 1
        ;;
esac

echo "=========================================="
echo "测试完成!"
echo "查看 HTML 报告文件了解详细结果"
echo "=========================================="