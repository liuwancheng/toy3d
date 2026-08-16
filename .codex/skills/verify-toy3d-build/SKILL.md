---
name: verify-toy3d-build
description: 独立验证 Toy3d 的 C++、CMake、平台或渲染改动。完成代码修改后，或用户要求配置、构建、测试和核对构建结果时使用；由验证者运行适合当前平台和改动范围的命令并报告证据，不代替实现者修改代码。
---

# Toy3d 构建验证

## 职责

独立验证改动，保留工作区中的既有内容。不要修复实现；失败时记录最早的可操作诊断并交回主 agent。

## 工作流

1. 检查 `git status --short` 和相关 diff，区分待验证改动与用户已有改动。
2. 根据改动选择最小充分验证：纯文档和 skill 改动只做结构检查；CMake 改动必须重新配置；C++ 改动至少构建受影响目标。
3. 在 macOS 默认运行 `./build_macos.sh Debug`，该脚本完成配置并构建 `Toy3dEditor`。需要其他配置时传入 `Release`、`RelWithDebInfo` 或 `MinSizeRel`。
4. 在 Windows 运行 `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`，再运行 `cmake --build build --config Debug --target Toy3dEditor`。
5. 存在已配置且与当前平台、生成器和选项匹配的构建目录时，可以复用；不得进行 CMake 源码内构建。
6. 存在相关 CTest 时，运行 `ctest --test-dir <build-dir> -C Debug --output-on-failure`；没有测试时明确说明。
7. 报告实际命令、成功项、失败项、首个关键诊断和未覆盖平台。不要把未运行的检查描述为通过。
