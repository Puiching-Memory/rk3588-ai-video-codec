# 测试

## 测试哲学

本项目的测试策略参考 SQLite：

- 把公共 API 契约测试放在最前面，优先验证非法输入、边界值和错误返回，而不是只跑 happy path。
- 把每一次缺陷修复都沉淀为回归用例，避免同类错误再次进入交付件。
- 用多套构建配置重复运行同一批测试，专门捕获内存错误、未定义行为和覆盖盲区。
- 默认优先选择确定性、可重复、无硬件依赖的测试；硬件编解码能力测试单独进行，避免让基础回归受环境波动影响。

## 当前测试矩阵

- `tests`：基础单元测试，覆盖版本/错误码、帧生命周期、公共 API 契约和配置校验。
- `asan`：在基础单元测试上启用 AddressSanitizer 和 UndefinedBehaviorSanitizer。
- `coverage`：用覆盖率插桩重新编译并执行同一批测试，度量测试盲区。
- `scripts/test-strict.sh`：顺序执行上述矩阵，并在环境允许时附加 Valgrind 和 gcovr 汇总。

## 执行命令

```bash
# 基线单元测试
cmake --preset tests
cmake --build --preset tests
ctest --preset tests

# Sanitizer 矩阵
cmake --preset asan
cmake --build --preset asan
ctest --preset asan

# 覆盖率矩阵
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage

# 一键严格模式
./scripts/test-strict.sh
```

## 交付前最低要求

- 基线单元测试全部通过。
- ASan/UBSan 构建全部通过。
- 覆盖率构建完成并保留报告。
- 新缺陷必须附带新的回归测试。
- 对依赖硬件的编码/解码链路，单独记录设备型号、内核版本、FFmpeg 构建信息和测试产物。

## 后续建议

- 为编码器和解码器增加故障注入接口，补齐 OOM 和 I/O 错误测试。
- 把覆盖率阈值和 sanitizer 构建接入 CI，阻止未达标变更合入。
- 为硬件链路增加固定样本的长时间 soak test 和产物校验。