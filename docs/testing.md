# 测试

## 测试哲学

本项目的测试策略参考 SQLite，但按 RK3588 视频编解码库的交付现实落地：

- 公共 API 契约优先：先验证非法输入、边界值、错误码和资源释放，再验证 happy path。
- 异常路径必须可重复：OOM、I/O 错误、空指针和无硬件环境都要有确定性测试。
- 同一批测试在多种构建配置下重复运行：普通 Debug、ASan/UBSan、coverage instrumentation。
- 覆盖率是对测试集的测试：覆盖率构建不代替交付构建，交付前仍必须跑 Release/硬件链路。
- 缺陷修复必须沉淀为回归测试；没有回归用例的修复不能进入关键客户交付分支。

## 当前测试矩阵

| 层级       | 目标                                                                 | 说明                                                                                       |
| ---------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| 基础契约   | `tests/test_types.c`, `tests/test_frame.c`, `tests/test_contracts.c` | 版本、错误码、帧生命周期、公共 API 参数校验、I/O 缺失路径                                  |
| 内部一致性 | `tests/test_internal.c`                                              | FFmpeg 错误码映射、枚举校验、像素格式转换、内部帧包装                                      |
| 异常注入   | `tests/test_fault_injection.c`                                       | `RKVC_ENABLE_FAULT_INJECTION` 下确定性模拟项目自有分配 OOM                                 |
| 硬件集成   | `tests/test_hardware.c`                                              | 自动探测 RKMPP 设备；可用时执行 H.265 硬件编码/解码文件往返，不可用时 CTest skip           |
| 动态分析   | `asan` preset                                                        | AddressSanitizer + UndefinedBehaviorSanitizer；当前环境下 LSan 关闭，泄漏检查交给 Valgrind |
| 覆盖率     | `coverage` preset                                                    | 用 gcov instrumentation 重新编译并执行同一测试集                                           |
| 严格门禁   | `scripts/test-strict.sh`                                             | 顺序执行 `tests`、`asan`、`coverage`，有 Valgrind/gcovr 时自动附加报告                     |

`RKVC_ENABLE_FAULT_INJECTION` 默认关闭，只在测试 preset 中启用，交付库不会暴露测试钩子。

## 执行命令

```bash
# 基线单元测试
cmake --preset tests
cmake --build --preset tests
ctest --preset tests --output-on-failure

# Sanitizer 矩阵
cmake --preset asan
cmake --build --preset asan
ctest --preset asan --output-on-failure

# 覆盖率矩阵
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage --output-on-failure

# 一键严格模式
./scripts/test-strict.sh
```

RK3588 实机上可在安装 `gcovr` 后设置最低覆盖率门禁：

```bash
RKVC_COVERAGE_MIN_LINE=80 RKVC_COVERAGE_MIN_BRANCH=70 ./scripts/test-strict.sh
```

脚本会生成 `build-coverage/coverage/index.html` 和 `coverage.xml`。在没有 RKMPP 设备节点的环境中，`test_hardware` 会跳过，可使用 `RKVC_COVERAGE_MIN_LINE=60 RKVC_COVERAGE_MIN_BRANCH=50` 作为无硬件基础门禁；在 RK3588 实机交付环境中使用 `80/70`。

Valgrind 默认只跑无硬件依赖的单元测试，因为 Rockchip MPP/FFmpeg 硬件栈会产生第三方库噪声。需要显式审查硬件栈时可运行：

```bash
RKVC_VALGRIND_HARDWARE=1 ./scripts/test-strict.sh
```

## 交付前最低要求

- `./scripts/test-strict.sh` 全部通过。
- 如环境安装了 Valgrind，所有 `test_*` 可执行文件必须以 `--leak-check=full --error-exitcode=1` 通过。
- 如环境安装了 gcovr，必须归档 HTML/XML 覆盖率报告，并记录 line/branch 覆盖率。
- 可移植包必须通过 `./scripts/test-portable.sh <package-dir>`。
- RK3588 实机必须完成固定样本编码、解码、管道、长时间 soak test 和产物校验。
- 新缺陷必须附带新的回归测试；不能复现的缺陷至少要补充契约测试或交付清单项。

## 发布清单

每次关键客户交付前由负责人逐项确认并保留日志：

- 源码：`git status --short` 只包含本次交付变更，子模块版本已锁定。
- 构建：Debug、Release、ASan/UBSan、coverage 构建均成功。
- 单元：`test_types`、`test_frame`、`test_contracts`、`test_internal`、`test_fault_injection` 全部通过。
- 异常：OOM 注入、缺失文件路径、NULL 参数、非法枚举、边界配置均覆盖。
- 动态：Valgrind 或等价内存工具无泄漏、无越界、无未初始化读取。
- 覆盖：覆盖率报告已归档，未覆盖区域有解释或后续任务。
- 硬件：记录 SoC 型号、内核版本、设备节点权限、FFmpeg/MPP 版本、输入样本 SHA256、输出产物 SHA256。
- 包：SDK/可移植包完整性、RPATH、动态库依赖、头文件和 pkg-config/CMake 包配置均验证。
