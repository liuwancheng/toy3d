# Toy3d 功能文档入口

日常开发按下表选一份主文档，再读相关章节、公共接口与测试。跨模块改动只补读直接关联文档。本文和功能文档组成长期知识库，AGENTS 规定全仓协作；skill 只提供特定任务的方法。

| 任务 | 主文档 | 从哪里核对代码 |
| --- | --- | --- |
| 文件、进程、日志、文本、哈希、图像与格式能力 | [Core](core.md) | `engine/core/` 公共头及 CMake target |
| 坐标、矩阵、Transform、几何、厘米与角度 | [Math](math.md) | `engine/core/math/`、`engine/core/tests/math_tests.cpp` |
| 线程、Queue、TaskGraph、等待/退出 | [Threading](threading.md) | `engine/core/threading/`、`engine/core/task_graph/` |
| 反射、序列化、Asset 身份、YAML/meta、导入/缓存 | [Assets](assets.md) | `engine/core/asset/`、reflection、serialization、`engine/tools/` |
| Engine/Application、配置、输入、平台、资源部署 | [Runtime](runtime.md) | `engine/runtime/engine.*`、application、config、platform、input |
| World/Actor/Component、注册、挂接、settings 更新 | [GameScene](gamescene.md) | `engine/runtime/gamescene/` |
| GT/RT、RenderCommand、CPU fence、资源上传/退出 | [Render Framework](render-framework.md) | `engine/runtime/rendercore/`、`renderscene/renderer.*` |
| View、可见性、MeshBatch、Shadow/Base/Tonemap/UI、预览 | [Renderer](renderer.md) | `engine/runtime/renderscene/` |
| 公共 RHI、后端、GPU 状态/同步、binding、Vulkan/VMA/WSI | [RHI](rhi.md) | `engine/runtime/drivers/rhi/`、`drivers/vulkan/` |
| Shader 语言/ABI/compiler/codegen/ShaderMap | [Shader](shader.md) | `engine/tools/shader_compiler/`、`rendercore/shader/`、[语法](shader-language-v1.ebnf) |
| 材质、实例继承、参数、纹理、源码重编译/候选发布 | [Material](material.md) | `rendercore/material/`、`engine/core/material/`、Editor 工作流 |
| 面板/组件/资产编辑器、Workspace、场景保存、撤销/异步 | [Editor](editor.md) | `engine/editor/`、其 tests |

## 使用与维护

- 先定位真实 target、公共头、完整调用链和失败测试，再修改；文档例子只演示现有 API。片段前置条件写在相邻文字中，完整创建/清理见所列测试，禁止根据类名臆造接口。
- 每份文档保存核心 contract、关键流程、修改检查点和验证入口；算法以注明的代码为证据，不复制整段源码。未实现范围明确标注，不能当成可调用能力。
- 改公开接口、格式、默认值或生命周期时同批更新主文档和示例；不增加第二份模块总设计、长期进度表、状态评审记录或每模块 skill。
- 大改可临时写一份短计划；已确认长期决策合并到主文档，任务/验证记录放 PR，完成后删除计划。普通维护直接改代码和对应章节。
- OpenSpec 仅在用户明确选择时使用；旧内容供显式追溯，不参与规范路由。历史文档从 Git 查阅，不在知识库复制保存。

## Skills

- `.codex/skills/design-rhi/`：公共 RHI/后端设计与审查方法，产品约束只在功能文档。
- `.codex/skills/verify-toy3d-build/`：按改动范围独立构建/测试。
- `.codex/skills/grilling/`：用户要求压力测试想法时使用。
- `.agents/skills/openspec-*/`：显式 OpenSpec 工作流，关闭隐式触发。
