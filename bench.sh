#!/usr/bin/env bash
# 并发压测脚本
# 用法: ./bench.sh [port] [concurrency] [total_requests]
# 例: ./bench.sh 9999 1000 1000

set -euo pipefail

PORT=${1:-9999}
CONCURRENCY=${2:-1000}
TOTAL=${3:-1000}
HOST="localhost"
OUT="$(mktemp /tmp/bench_times.XXXXXX)"

trap 'rm -f "$OUT"' EXIT

# 清空输出文件
: > "$OUT"

run_one() {
  start=$(date +%s%N)
  # 如果你的 nc 不支持 -N，请改用 --send-only 或 timeout
  echo "ping" | nc -N "$HOST" "$PORT" >/dev/null 2>&1
  rc=$?
  end=$(date +%s%N)
  if [ $rc -ne 0 ]; then
    echo "error" >> "$OUT"
  else
    elapsed_ns=$((end - start))
    printf "%d\n" "$elapsed_ns" >> "$OUT"
  fi
}

# 启动请求，控制并发
count=0
while [ $count -lt "$TOTAL" ]; do
  # throttle when达到并发上限
  while [ "$(jobs -r | wc -l)" -ge "$CONCURRENCY" ]; do
    sleep 0.01
  done
  run_one & 
  count=$((count + 1))
done

wait

# 统计：转换为 ms，并计算平均、最小、最大、成功数、失败数
awk 'BEGIN{sum=0;cnt=0;min=0;max=0;err=0}
/^[0-9]+$/ {
  ms=$1/1e6;
  sum+=ms; cnt++;
  if(min==0 || ms<min) min=ms;
  if(ms>max) max=ms;
}
$0=="error"{err++}
END{
  if(cnt==0){print "No successful requests"; exit}
  printf "total_requests=%d\nsuccessful=%d\nfailed=%d\navg_ms=%.6f\nmin_ms=%.6f\nmax_ms=%.6f\n",
    cnt+err, cnt, err, sum/cnt, min, max
}' "$OUT"

# 保留原始纳秒文件路径以供检查
echo "raw_times_file=$OUT"