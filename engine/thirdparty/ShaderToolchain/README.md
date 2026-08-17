# Toy3d Shader Toolchain

本目录保存 Toy3d 离线 Shader 编译所需的锁定工具链。普通 CMake 构建只消费已经提交的平台 bundle，不下载或重新构建第三方源码，也不会搜索 `PATH`、Vulkan SDK、Xcode 或 UE 安装目录。

## 目录

```text
ShaderToolchain/
├── Build/             锁、bootstrap 和一键发布入口
├── windows-x64/       Windows x64 正式 bundle
├── macos-x64/         Intel macOS 正式 bundle
└── macos-arm64/       Apple Silicon macOS 正式 bundle
```

每个平台 bundle 包含 DXC、`dxcompiler` 动态库、`spirv-val`、SPIRV-Reflect 静态库及其完整公共头文件依赖、第三方 license，以及 `Toy3dShaderToolchain.manifest`。Windows 同时包含与 MSVC Debug CRT 匹配的 SPIRV-Reflect Debug 静态库；其他配置使用 Release 静态库。manifest 锁定 host platform、source revision、构建参数、compiler identity 和每个 artifact 的 SHA-256。

`macos-x64` 和 `macos-arm64` 必须分别在对应架构的 Mac 上构建并运行验证，不能用未经运行验证的交叉编译产物代替。

## 一键发布

Windows x64：

```powershell
engine\thirdparty\ShaderToolchain\Build\publish_windows_x64.bat
```

macOS（脚本自动识别 Intel 或 Apple Silicon）：

```bash
bash engine/thirdparty/ShaderToolchain/Build/publish_macos.sh
```

发布会在被忽略的 `build/shader-toolchain/` 中拉取 lock 指定的源码、构建 Release 工具（Windows 额外构建 Debug SPIRV-Reflect 静态库）、生成 staging bundle，并真实编译最小 vertex/pixel HLSL、执行 `spirv-val`。全部成功后才替换当前 host 对应的平台目录；替换失败会恢复旧 bundle。

已有完整构建树时可以只重新打包和验证：

```powershell
engine\thirdparty\ShaderToolchain\Build\publish_windows_x64.bat --package-only
```

```bash
bash engine/thirdparty/ShaderToolchain/Build/publish_macos.sh --package-only
```

只拉取并核对锁定源码、不构建：

```powershell
python engine/thirdparty/ShaderToolchain/Build/bootstrap_shader_toolchain.py --skip-build
```

升级时人工审查并修改 `Build/toolchain-lock.json` 和 `Build/dxc-version/version.inc`，随后分别在各 host 运行发布入口。脚本不会自动选择上游最新版。

## CMake 与运行路径

`Toy3dShaderToolchainSync` 将当前 host bundle 同步到：

```text
bin/ShaderToolchain/<host-platform>/
```

`Toy3dShaderCompiler` 同步到 `bin/`。调用方可用 `--toolchain-root <path>` 显式指定 bundle；未指定时，工具使用自身相邻的 `ShaderToolchain/<host-platform>/`。找不到或 hash 不匹配会明确失败。

二进制由 Git LFS 管理。首次使用先安装 Git LFS 并初始化当前用户环境；clone 后若 CMake 报告 unresolved LFS pointers，请运行：

```text
git lfs install
git lfs pull
```
