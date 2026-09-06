## 1. 建立拆分基线

- [x] 1.1 盘点 `VulkanDevice` 的 mapping、creation、lifecycle 和 consumer 依赖，记录所有 `VulkanDevice&` 持有点及 accessors 调用点，并用 `rg` 结果确认范围覆盖 command context、viewport、swapchain 和 tests。
- [x] 1.2 补齐 RHI→Vulkan type/format mapping 的定向测试，覆盖成功映射、未知/不支持值及关键 pipeline/resource 枚举，并构建运行对应测试确认迁移前基线通过。

## 2. 集中 Vulkan 类型映射

- [x] 2.1 新增 `vulkan_type_mapping.*`，迁移 `PixelFormat`、resource usage/dimension/sample/aspect、shader/binding 和 pipeline state 转换；用定向 mapping 测试确认结果与错误分类不变。
- [x] 2.2 将 resource creation、pipeline creation、command context 和 format capability 查询切换到统一 mapping API，删除 `vulkan_device.cpp` anonymous namespace 与 `vulkan_resource.*` 中的重复转换，并用 `rg` 确认每项映射只有一个定义位置。
- [x] 2.3 在 runtime CMake 中显式登记 mapping 源文件，完成 Windows CMake configure 和 Vulkan/RHI 相关目标构建，确认 header 可独立包含且无循环依赖。

## 3. 拆分 backend creation 领域

- [x] 3.1 新增 `vulkan_resource_creation.*`，迁移 buffer、texture、texture view、shader、sampler 和 buffer-view `Unsupported` 路径，使函数只接收 owner、native handle、descriptor 与 memory/deletion 依赖；运行 resource/frontend tests 验证创建、owner 和错误行为不变。
- [x] 3.2 新增 `vulkan_binding_creation.*`，迁移 binding layout、logical binding set 与 physical binding packet materialization，保持 Global+View 聚合和 packet 强引用语义；运行 RHI binding tests 验证通过。
- [x] 3.3 新增 `vulkan_pipeline_creation.*`，迁移 compatibility render pass、pipeline layout 和 graphics pipeline native 创建，保持公共 pipeline cache 仍仅在 `RHIDevice` frontend；运行 pipeline/frontend 与 render-pass 相关测试验证通过。
- [x] 3.4 将全部 `VulkanDevice::create_*_impl()` 收敛为只传入窄依赖并原样返回结果的适配器，用 code review 与 `rg` 确认 creation modules 不接收 `VulkanDevice&`/`initialized`、不调用公共 `create_*()`、不复制 frontend policy。
- [x] 3.5 显式登记 creation 源文件并构建所有 Vulkan/RHI 测试目标，确认拆分 translation unit 后无 ODR、未解析符号或 include 泄漏。

## 4. 移除 backend consumer 的 device service locator

- [x] 4.1 重构 `VulkanGraphicsCommandContext` 构造链，逐项注入 owner identity、`VkDevice`、command-pool/frame association 和 `VulkanUploadManager&`，并改用窄 binding materialization 函数；运行 command/resource-state/binding tests 验证录制结果不变。
- [x] 4.2 重构 `VulkanSwapchain` 构造与 create 链，逐项注入 owner identity、physical device、logical device 和 surface，保持 queue handle 仅在 present 调用时显式传入；运行 swapchain/presentation tests 验证 acquire/present 状态映射不变。
- [x] 4.3 重构 `VulkanViewportContext` 构造链，逐项注入 owner、native handles、queue family、`VulkanQueue&` 和 upload/deletion 回收依赖，保持 frame slot、completion 与 WSI completion 两个域不混用；运行 viewport/resource-state/swapchain tests 验证通过。
- [x] 4.4 删除 command context、viewport 和 swapchain 中的 `VulkanDevice&` 成员及 `vulkan_device.h` include，删除仅为 service lookup 存在的 device accessors；用 `rg` 确认三个 consumer 不再引用完整 `VulkanDevice`，并构建 `Toy3dEditor`。

## 5. 生命周期与结构复查

- [x] 5.1 复查并测试 initialize 部分失败的逆序清理，确认 queue、deletion、upload、memory、logical device、surface、debug messenger、instance 的销毁顺序与设计一致。
- [x] 5.2 运行 ordinary shutdown、`shutdown_after_device_lost()` 和 creation admission 测试，确认 ordinary 路径只执行既定 idle policy、device-lost 路径不新增 native idle wait，且错误码不被 creation adapter 改写。
- [x] 5.3 更新 `document/rhi-design.md` 的 Vulkan backend implementation 摘要，明确 `RHIDevice`/`VulkanDevice` facade 与窄 creation modules/显式依赖的关系；执行文档引用和 `git diff --check` 验证。
- [x] 5.4 执行结构审查：公共 RHI/renderscene 无 Vulkan 类型，新模块无 static mutable state、无第二个 device/services bag、无新旧双轨 creation path，并将 `rg` 证据记录在交付摘要。

## 6. 集成验证

- [x] 6.1 主 agent 完成 Windows configure，构建 RHI frontend、binding、resource-state、viewport、swapchain tests 和 `Toy3dEditor`，再运行全量 CTest；所有命令成功并记录测试数量。
- [x] 6.2 主 agent 运行 Vulkan validation smoke，覆盖多帧、resize、minimize/restore 与正常关闭，确认无 validation warning/error，且可恢复 presentation status 未变成 terminal failure。
- [x] 6.3 由独立 sub-agent 使用 `verify-toy3d-build` 重新执行与风险相称的配置、构建、全量测试和 Vulkan validation smoke，并由主 agent 复核独立证据后再标记完成。
- [x] 6.4 运行 `openspec validate "decompose-vulkan-device-responsibilities" --strict`，确认 `skip_specs`、proposal、design 和 tasks 一致，且全部任务完成状态与实际证据相符。
