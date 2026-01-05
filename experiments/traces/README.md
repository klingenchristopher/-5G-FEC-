# 5G链路Trace文件

本目录用于存放5G网络链路的trace文件，用于实验和性能评估。

## Trace文件格式

推荐使用JSON或CSV格式记录链路质量指标：

### JSON格式示例

```json
{
  "scenario": "maritime",
  "duration": 100,
  "samples": [
    {
      "timestamp": 0.0,
      "rtt_ms": 45.2,
      "loss_rate": 0.02,
      "bandwidth_mbps": 120.5
    },
    {
      "timestamp": 0.1,
      "rtt_ms": 48.1,
      "loss_rate": 0.03,
      "bandwidth_mbps": 115.2
    }
  ]
}
```

### CSV格式示例

```csv
timestamp,rtt_ms,loss_rate,bandwidth_mbps
0.0,45.2,0.02,120.5
0.1,48.1,0.03,115.2
0.2,46.8,0.02,118.3
```

## 场景分类

### 1. 海事场景 (Maritime)
- **特点**: 距离远、信号弱、延迟高
- **典型RTT**: 50-150ms
- **典型丢包率**: 5-15%
- **典型带宽**: 20-80 Mbps

### 2. 移动场景 (Mobile)
- **特点**: 频繁切换、干扰强
- **典型RTT**: 20-80ms
- **典型丢包率**: 2-10%
- **典型带宽**: 50-150 Mbps

### 3. 密集场景 (Dense)
- **特点**: 用户多、资源竞争
- **典型RTT**: 30-100ms
- **典型丢包率**: 3-12%
- **典型带宽**: 30-120 Mbps

## 数据采集

可以使用以下工具采集真实5G链路数据：

1. **iperf3**: 带宽和延迟测试
2. **ping**: RTT测量
3. **tc-netem**: 网络环境模拟
4. **自定义工具**: 使用QUIC客户端记录传输指标

## 使用分析脚本

```bash
# 分析trace文件
python scripts/analyze_trace.py --trace traces/5g_maritime.trace --output traces/

# 查看生成的统计信息和图表
cat traces/statistics.json
open traces/metrics.png
```

## 添加新Trace

1. 按照上述格式记录数据
2. 将文件放入此目录
3. 添加场景描述到本README
4. 运行分析脚本验证数据有效性

## 注意事项

- Trace文件应包含足够长的时间序列（建议≥60秒）
- 采样频率建议为100ms-1s
- 记录环境信息（地点、时间、设备等）
- 注意数据隐私和脱敏处理

---

目前trace目录为空，需要通过实际网络测试采集数据。
