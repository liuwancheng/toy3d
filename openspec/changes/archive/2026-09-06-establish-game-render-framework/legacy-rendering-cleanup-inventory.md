# 阶段 0：旧渲染路径清单

## 本次选择依据

- 主导航：`design.md` 的“多次 Apply 导航”要求从第一个依赖已满足的 capability 开始。
- 目标 capability：`legacy-rendering-cleanup`，它是第一个未完成阶段且无直接前置。
- 类型治理：目标 Spec 的 `Type Contracts` 明确本阶段不新增运行时类型；本次只删除旧类型并收敛现有 `toy3d::Engine` 临时路径。
- 分侧：`toy3d::Engine`、Platform、Window、Asset 与 GameScene 数据模型属于 Game side；Task Graph、公共 RHI 与 backend 属于保留的 Render-side 基础；本清单中的 frame transport、Scene frame processor 和 resource cache 是被删除的旧跨侧双轨。

## 废弃定义、调用方、测试与替代 capability

| 废弃范围 | 定义 | 正式调用方 | 测试 / CMake target | 替代 capability |
| --- | --- | --- | --- | --- |
| `RenderFramePacket`、`RenderFrameQueue`、`RenderFrameDispatcher`、`RenderFrameCompletion` | `engine/runtime/renderscene/render_frame_{packet,queue,dispatcher,completion}.*` | `RenderSceneFrameProcessor`、`SceneOutputResourceCache`、旧 frame transport tests | `render_frame_transport_tests.cpp` / `Toy3dRenderFrameTransportTests` | `rendering-thread-lifecycle`、`render-command-transport`、`frame-synchronization` |
| Scene update batch 与 frame processor | `rendercore/render_scene_update.h`、`renderscene/scene/render_scene.*`、`renderscene/render_scene_frame_processor.*` | 旧 packet processor、foundation pipeline | `render_scene_tests.cpp`、`render_foundation_pipeline_tests.cpp` / `Toy3dRenderSceneTests`、`Toy3dRenderFoundationPipelineTests` | `renderer-scene-ownership`、`primitive-proxy-lifecycle` |
| Camera/View frame builder 与长期 viewport frame | `renderscene/view/scene_view.*`、`renderscene/view/viewport_frame.*` | 旧 packet 与 frame processor | `scene_view_tests.cpp`、`viewport_frame_tests.cpp` / `Toy3dSceneViewTests`、`Toy3dViewportFrameTests` | `view-render-flow` |
| 旧 Scene output frame cache | `renderscene/output/scene_output*.{h,cpp}` | 旧 frame packet 与 processor | `scene_output_resource_cache_tests.cpp` / `Toy3dSceneOutputResourceCacheTests` | `view-render-flow`、`rhi-frame-submission` |
| `RenderResourceCache`、upload batch、primitive resolver | `renderscene/resources/render_resource_{cache,upload}.*`、`primitive_render_resources.*` | `toy3d::Engine`、旧 frame processor | `render_resource_cache_tests.cpp`、`render_resource_upload_tests.cpp`、`vulkan_bootstrap_context_tests.cpp` / 对应三个 test target | `render-resource-manager`、`static-mesh-resources`、`texture-resources`、`renderer-bootstrap` |
| typed render-resource ID / revision / update | `rendercore/render_id.h`、`render_resource_revision.h`、`render_resource_update.h` | `StaticMesh`、`MaterialInstance`、旧 Scene/resource pipeline | 上述 cache/upload/Scene/frame tests | stable representation address 与 ownership transfer；分别由资源和 proxy capability 实现 |
| 空壳 `RHIDeviceCommandList` 与带 RHI 参数的 RenderCommand 原型 | `drivers/rhi/rhi_device_command_list.h`、`rendercore/render_command.*` | `drivers/rhi/rhi.h` 聚合入口 | `render_command_tests.cpp` / `Toy3dRenderCommandTests` | 无 RHI 参数的 `render-command-transport` |
| Engine 直持 RHI/cache/concrete SceneRendering 的旧启动与帧循环 | `engine/runtime/engine.h/.cpp` | `Engine::init()`、`main_loop()`、`exit()` | `Toy3dEditor` 构建与安全启动/退出检查 | `engine-composition-root`、`renderer-bootstrap` |

## 保留的底层能力

- `engine/core/task_graph/` 的 named queue、GraphEvent、wait/help 与 shutdown。
- 公共 RHI 的 command context/list local state、queue completion、viewport abort、device bootstrap 接口。
- Vulkan upload manager、queue completion tracking、deferred deletion 与资源保活测试 seam。
- GameScene 的 World/Actor/Component/Camera 值模型，以及 StaticMesh/Material 的 Game-side Asset 数据。

本清单使用排除 `document/archive/`、`build/` 与 `bin/` 的 `rg` 全仓搜索生成；清理完成后以相同范围复查废弃符号。
