toy3d/
├── CMakeLists.txt                # 主CMake文件
├── platform/                     # 平台抽象层
│   ├── CMakeLists.txt
│   │   ├── platform_interface.h  # 平台接口抽象
│   │   ├── windows/              # Windows平台实现
│   │   │   ├── win32_platform.h
│   │   │   └── win32_platform.cpp
│   │   └── macos/                # macOS平台实现
│   │       ├── macos_platform.h
│   │       └── macos_platform.cpp
│   └── tests/                    # 平台层测试
├── engine/                       # 引擎核心
│   ├── CMakeLists.txt
│   │   └── engine.h
│   │   └── engine.cpp           # 引擎主类实现
│   │   ├── core/                 # 核心系统
│   │   │   ├── logger.h
│   │   │   ├── memory.h
│   │   │   ├── system.h
│   │   │   ├── command_line_parser.h  # 新增：命令行解析
│   │   │   ├── command_line_parser.cpp
│   │   │   ├── config_manager.h       # 新增：配置管理
│   │   │   └── config_manager.cpp
│   │   ├── window/               # 窗口系统
│   │   │   ├── window_interface.h
│   │   │   ├── win32/
│   │   │   │   └── win32_window.cpp
│   │   │   └── glfw/
│   │   │       └── glfw_window.cpp
│   │   ├── render/               # 渲染系统
│   │   │   ├── rhi/              # 渲染硬件接口
│   │   │   │   ├── rhi_interface.h
│   │   │   │   ├── dx12/
│   │   │   │   │   └── dx12_rhi.cpp
│   │   │   │   └── vulkan/
│   │   │   │       └── vulkan_rhi.cpp
│   │   │   ├── renderer.h
│   │   │   └── renderer.cpp
│   └── tests/                   # 引擎单元测试
├── editor/                      # 编辑器
│   ├── CMakeLists.txt
│   │   ├── editor_app.h
│   │   └── editor_app.cpp
│   └── resources/              # 编辑器资源文件
├── samples/                    # 示例项目
│   ├── CMakeLists.txt
│   └── hello_triangle/
│       ├── CMakeLists.txt
│       └── main.cpp
├── 3rdparty/                  # 第三方库
│   ├── CMakeLists.txt
│   ├── vulkan/
│   ├── glfw/
│   └── imgui/
├── docs/                      # 文档
└── scripts/                   # 构建脚本和工具