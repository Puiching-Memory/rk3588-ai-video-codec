# RD 基准测试（bench/）

RK3588 端到端 **码率-画质（RD）** 与 **性能** 对比框架，已集成到 rkvc 项目。

## 对比路线（默认）

| codec 名 | 方案 |
|----------|------|
| `h264` | FFmpeg `h264_rkmpp` 硬编硬解 |
| `h265` | FFmpeg `hevc_rkmpp` 硬编硬解 |
| `svt-av1` | SVT-AV1 编 + `av1_rkmpp` 硬解 |
| `rkvc-v2` | **rkvc Session** 三档语义（`RKVC_POLICIES` 展开） |
| `rkvc-realtime` | Session `realtime` → H.264 RKMPP |
| `rkvc-balanced` | Session `balanced` → HEVC RKMPP |
| `rkvc-quality` | Session `quality` → SVT-AV1 p11 + av1_rkmpp（与 `svt-av1` 基线同 preset） |
| `post-upscale` | **下采样编码 + 上采样后处理**（bench / Session 均用 RGA 硬件） |
| `svt-av1+up3x-bilinear` | 单算法路线（`ENC_SCALE_DENOM=3` 时 CSV 名为 `svt-av1+up{N}x-{algo}`） |
| `svt-av1+superres` | **实验 / 搁置**：SVT-AV1 + AV1 内建 superres（见下节） |

## AV1 内建 superres（实验，搁置）

SVT-AV1 开启 `--superres-mode` 等参数，在编码器内部做低分辨率编码 + 规范上采样。RD 图中 codec 名为 `svt-av1+superres`（虚线，深绿六边形标记）。

**状态：搁置。** `av1_rkmpp` 硬解 superres 码流时，`hwdownload` 会因显示宽（1920）与 DMA stride（编码宽 ~1182）不一致而崩溃；待 MPP/ffmpeg-rkmpp 修复前，显式启用本路线时自动改用系统 `libaom-av1` 软解（慢，但可跑通 RD）。

```bash
# 显式启用（不在默认 RUN_CODECS 中）
RUN_CODECS=svt-av1,svt-av1+superres ./scripts/run-bench.sh /path/to/1080p.mp4

# superres 参数（可选）
SVT_SUPERRES_MODE=4          # 默认 auto；1=fixed 3=qthresh
SVT_SUPERRES_DENOM=9         # fixed 时分母 9~16（8/9 ~ 8/16 水平缩放）
SVT_SUPERRES_FFMPEG=/usr/bin/ffmpeg   # 软解 ffmpeg
```

## 下采样 + 后处理上采样（评估 NN 占位）

模拟未来「低分辨率编码 → 解码 → NN/传统上采样」管线，post-upscale 路线与 **Session 产品路径一致**（RKMPP 硬解 DMABUF → RGA 上采样，含 SVT-AV1 IVF）：

```bash
# rkvc_session_upscale：单进程 Session decode + RGA（bench 内自动调用）
build/rkvc_session_upscale -i stream.mp4 -o out.nv12 \
  --width 1920 --height 1080 --enc-scale-denom 3 --post-upscale bilinear --print-timing
```

下采样参考帧仍用 `rkvc_yuv_upscale`（仅 prep 阶段）。

```bash
# 仅跑 post-upscale 路线（对比 SVT-AV1 全分辨率基线）
RUN_CODECS=svt-av1,post-upscale ./scripts/run-bench.sh /path/to/1080p.mp4

# 3× 下采样（1080p→360p）编码 + 上采样还原，对比全分辨率 SVT-AV1
ENC_SCALE_DENOM=3 UPSCALE_ALGOS=nearest,bilinear,bicubic \
  RUN_CODECS=svt-av1,post-upscale ./scripts/run-bench.sh /path/to/1080p.mp4
```

管线：`REF → 1/N 下采样 → SVT-AV1 编 → av1_rkmpp 解 → 传统上采样 → 与全分辨率 REF 比 PSNR/SSIM`。

rkvc Session 侧对应字段：`enc_scale_denom`、`post_upscale_algo`（`rkvc_encode --enc-scale-denom 2 --post-upscale bilinear`）。

## 快速开始

```bash
# 1. 构建依赖与 rkvc
./scripts/build-svt.sh
./scripts/rebuild-ffmpeg-rkmpp.sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

# 2. （可选）绘图 Python 环境
cd bench && uv sync    # 或 pip install matplotlib numpy

# 3. 跑基准（默认从源视频中间截取 4s，17 个码率点 25–1000 kbps）
./scripts/run-bench.sh /path/to/1080p.mp4

# 仅重绘图表
PLOT_ONLY=1 ./scripts/run-bench.sh
```

## 输出

| 路径 | 说明 |
|------|------|
| `bench/results/rd_data.csv` | 原始数据（含 `rga_sec` / `write_sec` / `postproc_sec` 分列） |
| `bench/results/rd_curve_e2e.png` | RD 曲线（横轴 log，含低码率放大图 `rd_curve_e2e_lowzoom.png`） |
| `bench/results/perf_e2e.png` | E2E 性能对比 |
| `bench/work/` | 中间文件（可删） |

## 环境变量

- `RUN_CODECS` — 默认 `h264,h265,svt-av1,rkvc-v2`（`rkvc-v2` 展开为三档 policy）
- `TARGET_KBPS` — 目标码率点（逗号分隔 kbps），默认 `25,30,40,50,60,80,100,150,200,300,400,500,600,700,800,900,1000`
- `SVT_RD_MODE` — SVT-AV1 RD 扫点：`calibrated`（默认，CRF/CQP 校准表）或 `vbr`（`--rc 1 --tbr`）
- `ENC_SCALE_DENOM` — post-upscale 编码下采样分母（默认 `2`）
- `UPSCALE_ALGOS` — 上采样算法（RGA 硬件），逗号分隔（默认 `nearest,bilinear,bicubic`）
- `RKVC_POLICIES` — rkvc 语义档位，默认 `realtime,balanced,quality`
- `CLIP_SEC` — 截取秒数（默认 `4`）
- `CLIP_OFFSET` — 截取位置：`middle`（默认，居中）| `start`
- `CLIP_START_SEC` — 显式起点秒数（设置后覆盖 `CLIP_OFFSET`）
- `RKVC_BUILD` — rkvc 构建目录（含 `rkvc_transcode`）
- `SVT_PRESET` — SVT 编码 preset（默认 11）
- `RAMDISK_DIR` — YUV 放 tmpfs，减少 I/O 干扰

## 单独绘图

```bash
cd bench
python3 plot_rd_curve.py --csv results/rd_data.csv
python3 plot_perf.py --csv results/rd_data.csv --frames 62
```

详细结论见 [REPORT.md](REPORT.md)。
