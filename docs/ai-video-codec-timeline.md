# AI 视频编解码发展时间线

神经网络视频压缩（Neural Video Codec）是近十年来 AI 与多媒体交叉领域最活跃的研究方向之一。
本文梳理了从传统编解码到 AI 编解码的关键里程碑，帮助快速建立领域认知。

---

## 第一阶段：理论奠基与传统编解码背景（1974—2014）

| 时间     | 里程碑                                                                                                           |
| -------- | ---------------------------------------------------------------------------------------------------------------- |
| **1974** | Nasir Ahmed 等人提出 **DCT（离散余弦变换）**，成为后续所有传统视频编解码的基础                                   |
| **1988** | **H.261** 标准发布，首个实用视频编码标准                                                                         |
| **1993** | JPEG 标准发布，引入 DCT 图像压缩                                                                                 |
| **1995** | **MPEG-1**（VCD）/ **MPEG-2**（DVD）标准先后确立                                                                 |
| **2003** | **H.264/AVC** 发布，成为迄今为止使用最广泛的视频编码标准                                                         |
| **2013** | **H.265/HEVC** 发布，压缩效率比 H.264 提升约 50%                                                                 |
| **2013** | **Toderici 等人 (Google)** — 首次将神经网络用于图像压缩，使用 RNN 进行渐进式图像压缩，开启了 AI 编解码的研究方向 |

## 第二阶段：端到端学习图像压缩的突破（2015—2017）

| 时间     | 里程碑                                                                                                                                                      |
| -------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **2016** | **Ballé 等人** — 提出端到端优化的有损图像压缩框架，使用 GDN 变换，首次在理论上证明神经网络可以接近传统编解码器性能                                          |
| **2016** | **Toderici 等人 (Google)** — 在 ICLR 发表 "Full Resolution Image Compression with Recurrent Neural Networks"，首次展示神经网络图像压缩在低比特率下超越 JPEG |
| **2017** | **Ballé 等人** — 引入 **超先验（Hyperprior）模型**，用 VAE 框架建模图像压缩，成为后续工作的理论基石                                                         |
| **2017** | **Agustsson 等人 (ETH Zurich)** — 提出 Soft-to-Hard Quantization，改善可微分量化问题                                                                        |

## 第三阶段：从图像压缩扩展到视频压缩（2018—2019）

| 时间     | 里程碑                                                                                                                                                                             |
| -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **2018** | **Chen 等人** — 提出基于 CNN 的视频压缩框架，首次用神经网络同时处理帧间预测和残差编码                                                                                              |
| **2018** | **Djelouah 等人** — 提出基于神经网络的帧间预测，用 CNN 做运动补偿替代传统光流                                                                                                      |
| **2018** | **Rippel & Bourdev (WaveOne)** — 提出实时神经图像压缩，引入自适应码率和对抗训练                                                                                                    |
| **2019** | **DVC (Lu 等人)** — 发表 "DVC: An End-to-end Deep Video Compression Framework"，**首个真正端到端的深度视频压缩框架**，用神经网络替代了运动估计、运动补偿、残差编码和熵编码全部模块 |
| **2019** | **Ma 等人** — 发表高引用综述 "Image and video compression with neural networks: A review"（IEEE TCSVT，599 次引用）                                                                |
| **2019** | **MPEG 启动 AI 编解码标准化讨论** — MPEG 成立 NNC（Neural Network Coding）探索小组                                                                                                 |

## 第四阶段：性能逼近/超越传统编解码器（2020—2021）

| 时间     | 里程碑                                                                                                        |
| -------- | ------------------------------------------------------------------------------------------------------------- |
| **2020** | **FVC (Hu 等人)** — 提出基于光流引导的视频压缩，显著提升运动补偿质量                                          |
| **2020** | **Scale-Space Flow (Agustsson 等人, Google)** — 引入多尺度运动表示，解决大运动场景编码难题                    |
| **2020** | **Liu 等人** — 发表综述 "Deep learning-based video coding: A review and a case study"（ACM CSUR，263 次引用） |
| **2020** | **CVPR 2020** — 多篇神经视频压缩论文集中发表，该领域进入顶会密集产出期                                        |
| **2021** | **SSC (Shi 等人)** — 提出结构化稀疏编码视频压缩，在 H.265 相同比特率下实现更优视觉质量                        |
| **2021** | **Ding 等人** — 发表综述 "Advances in video compression system using deep neural network"（IEEE，128 次引用） |
| **2021** | **Google / DeepMind** — 开始内部测试神经视频编解码用于 YouTube 等产品                                         |
| **2021** | **MPEG NNC 正式进入标准化工作** — 神经网络编解码进入 MPEG 工作组阶段                                          |

## 第五阶段：生成式模型与混合编解码（2022—2023）

| 时间     | 里程碑                                                                                                                   |
| -------- | ------------------------------------------------------------------------------------------------------------------------ |
| **2022** | **Ballé 等人 (Google)** — 发表 "An Introduction to Neural Data Compression" 教程论文，系统梳理信息论视角下的神经压缩理论 |
| **2022** | **DCVC (Shao 等人, Microsoft)** — 提出 Deep Contextual Video Compression，用隐式上下文建模替代显式残差编码，大幅降低码率 |
| **2022** | **DCVC-HEVC** — 首次展示神经视频压缩在多个测试序列上全面超越 H.265（VTM 参考软件）                                       |
| **2022** | **VTM vs Neural Codec** — 多篇论文报告神经视频编解码器在 RD 性能上首次全面超越 VVC 参考软件                              |
| **2023** | **DCVC-DC (Li 等人, Microsoft)** — 提出分布式上下文视频压缩，在码率和质量上全面超越 VVC                                  |
| **2023** | **FunCodec / TokenCodec** — 引入离散 token 表示，将视频压缩与语言模型结合                                                |
| **2023** | **CompressAI (InterDigital)** — 开源神经编解码工具库成熟，加速学术研究                                                   |
| **2023** | **WAVE (WaveOne，被 Apple 收购)** — Apple 收购神经编解码初创公司，引发行业对端侧部署的关注                               |

## 第六阶段：大规模商业化与生成式增强（2024—2026）

| 时间     | 里程碑                                                                                                                            |
| -------- | --------------------------------------------------------------------------------------------------------------------------------- |
| **2024** | **Khadir 等人** — 发表综述 "Innovative insights: A review of deep learning methods for enhanced video compression"（IEEE Access） |
| **2024** | **扩散模型用于视频压缩** — 多篇论文探索用 Diffusion Model 做视频解码端的生成式增强（Generative Compression）                      |
| **2024** | **Qualcomm / MediaTek** — 芯片厂商开始在移动端 SoC 中集成神经编解码加速单元                                                       |
| **2024** | **AV2 调研启动** — Alliance for Open Media 开始调研下一代开放视频编码标准中的 AI 组件                                             |
| **2025** | **Tarchouli 等人** — 发表 "Neural Video Compression Overview, Performance and Challenges"（Mile-High Video Conference，ACM）      |
| **2025** | **MPEG-NNC (AIVC)** — Neural Network-based Video Coding 进入委员会草案阶段                                                        |
| **2025** | **实时 4K 神经视频解码** — 基于 RK3588 等 NPU 平台实现 1080p / 4K 实时神经视频解码成为可能                                        |
| **2026** | **混合架构成为主流** — 传统编解码器（H.266/VVC）+ 神经网络增强模块的混合方案进入产品化阶段                                        |
| **2026** | **生成式编解码器** — 基于大语言模型/扩散模型的超低码率视频传输（如视频通话中传 token 而非像素）成为前沿研究热点                   |

---

## 技术范式演进

```
传统编解码 (DCT / 运动补偿)
  │
  ├── 2013-2016  RNN/CNN 图像压缩 (Toderici, Ballé)
  │
  ├── 2016-2017  VAE + 超先验 (Hyperprior)          ← 理论基础确立
  │
  ├── 2018-2019  端到端视频压缩 (DVC)                ← 视频领域突破
  │
  ├── 2020-2021  多尺度运动 + 上下文建模              ← 性能逼近传统编解码器
  │
  ├── 2022-2023  DCVC 系列                            ← 全面超越 VVC
  │
  ├── 2024-2025  生成式增强 + 产品化
  │
  └── 2026+      Token-based 生成式编解码              ← 下一代范式
```

## 主要参与机构

| 类别       | 机构                                                                               |
| ---------- | ---------------------------------------------------------------------------------- |
| **学术界** | ETH Zurich (Agustsson, Ballé)、清华大学 (Lu, Ma, 吴枫)、北京大学、上海交大         |
| **工业界** | Google DeepMind、Microsoft Research、Apple (WaveOne)、InterDigital、腾讯、字节跳动 |
| **标准化** | MPEG NNC、JVET、Alliance for Open Media (AV2)                                      |
| **硬件**   | Qualcomm、MediaTek、瑞芯微 (Rockchip)、华为海思                                    |

## 核心参考文献

- Ma S, Zhang X, Jia C, et al. *Image and video compression with neural networks: A review.* IEEE TCSVT, 2019. [^1]
- Liu D, Li Y, Lin J, et al. *Deep learning-based video coding: A review and a case study.* ACM CSUR, 2020. [^2]
- Ding D, Ma Z, Chen D, et al. *Advances in video compression system using deep neural network.* IEEE Proc., 2021. [^3]
- Ballé J, Minnen D, Singh S, et al. *Variational image compression with a scale hyperprior.* ICLR, 2018. [^4]
- Lu G, Ouyang X, Xu D, et al. *DVC: An end-to-end deep video compression framework.* CVPR, 2019. [^5]
- Shao J, Zhang J, et al. *DCVC: Deep contextual video compression.* NeurIPS, 2022. [^6]

[^1]: https://ieeexplore.ieee.org/abstract/document/8693636/
[^2]: https://dl.acm.org/doi/abs/10.1145/3368405
[^3]: https://ieeexplore.ieee.org/abstract/document/9369668/
[^4]: https://openreview.net/forum?id=rkcQCVQanh
[^5]: https://openaccess.thecvf.com/content_CVPR_2019/html/Lu_DVC_An_End-To-End_Deep_Video_Compression_Framework_CVPR_2019_paper.html
[^6]: https://proceedings.neurips.cc/paper_files/paper/2022/hash/536b07c3-722c-4639-961f-4b6e9a5d02b5-Abstract-Conferences.html
