# Perf comparison (this machine)

## Payload = 32 B

| Impl | Procs | Count/Warmup | Payload (B) | rate_hz | RTT min (us) | RTT mean (us) | RTT p50 (us) | RTT p95 (us) | RTT p99 (us) | RTT max (us) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SwIPC `exemples/25-IpcPerfMonitor` (C++) | 2 | 2000/200 | 32 | 16500.2 | 35.0 | 46.0 | 39.0 | 89.0 | 202.0 | 633.0 |
| ROS2 Jazzy ping/pong (rclpy) + FastDDS (`rmw_fastrtps_cpp`) | 2 | 2000/200 | 32 | 3125.7 | 180.9 | 275.0 | 224.8 | 481.3 | 687.7 | 3718.3 |

| Comparison (SwIPC vs ROS2 FastDDS) | Factor |
|---|---:|
| Throughput (`rate_hz`) | **5.28x** higher |
| RTT mean | **5.98x** lower (~6x faster) |
| RTT p50 | **5.76x** lower |
| RTT p95 | **5.41x** lower |
| RTT p99 | **3.40x** lower |

## Payload = 1000 B

| Impl | Procs | Count/Warmup | Payload (B) | rate_hz | RTT min (us) | RTT mean (us) | RTT p50 (us) | RTT p95 (us) | RTT p99 (us) | RTT max (us) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SwIPC `exemples/25-IpcPerfMonitor` (C++) | 2 | 2000/200 | 1000 | 14125.0 | 36.0 | 57.0 | 48.0 | 98.0 | 252.0 | 1584.0 |
| ROS2 Jazzy ping/pong (rclpy) + FastDDS (`rmw_fastrtps_cpp`) | 2 | 2000/200 | 1000 | 738.5 | 664.4 | 935.3 | 895.6 | 1154.1 | 1459.1 | 3628.0 |

| Comparison (SwIPC vs ROS2 FastDDS) | Factor |
|---|---:|
| Throughput (`rate_hz`) | **19.13x** higher |
| RTT mean | **16.41x** lower |
| RTT p50 | **18.66x** lower |
| RTT p95 | **11.78x** lower |
| RTT p99 | **5.79x** lower |

