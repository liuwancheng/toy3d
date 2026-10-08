# Render Framework：GT/RT、资源与退出

## 定位与 ownership

engine/runtime/rendercore 的 rendering_thread、render_command、frame_synchronization 是 Game/Render 桥；rendercore 的 RenderResource/RenderResourceManager、材质代理、网格渲染数据和纹理资源管理通用渲染资源；renderscene 的 Renderer 管场景与帧调度。当前都属于 Toy3dRuntime，不能臆造独立 Toy3dRenderCore target。

GT 拥有可变 World/组件和游戏侧表示；RT 拥有 SceneProxy、资源渲染状态与记录上下文；RHI 持有 GPU in-flight 引用。SceneInterface 是 root 管理的非 owning 桥，只在 Running 到 teardown 边界内发布，不存第二套命令队列。

## RenderCommand 与 CPU 同步

唯一普通入口 `enqueue_render_command(name, rvalue_callable)`，closure 签名 void() noexcept、可移动、携带 owned 数据；不能引用临时对象、GT mutable 状态或任意 worker owner。

真实接口片段（定义 rendercore/render_command.h）：只在已启动 facade 的 GT 或逻辑 RT 调用，payload 在业务侧准备为 owned 数据。

```cpp
toy3d::enqueue_render_command("UpdateOwnedData",
    [payload = std::move(payload)]() noexcept
    {
        // 此处只消费 owned payload，执行 RT 操作；不回读 GT 对象。
    });
```

正常返回表示 ownership 已接受或 inline 路径已处理；terminal 时业务 callable 可被跳过但 payload 必须在正确线程销毁。错误 caller/阶段以 TaskGraphException 诊断，不是 bool admission。RT 调用 inline；SingleThread 的 GT 为逻辑 RT 时 inline。其它 worker 不能直接生产 RenderCommand。GT→RT 经同一 NamedThread Normal FIFO，不建立并行正式入口。

RenderCommandFence 为 tracked CPU completion，terminal 时仍完成清理/同步链；begin/status 为 GT 语义，一个 outstanding fence 不重复 begin，未 begin 可视为 complete。wait 不超过 TaskGraph 生命周期。它只说明 CPU 队列到达，不表示 GPU/submit/present 已完成。

FrameEndSync 两个轮转点控制最多一帧 CPU lag；关闭 lag 则等待当前点。普通 setter/draw/release 不 flush，不按每帧 GPU idle；flush 仅启动、明确替换或退出安全点。

## 启动与上传

### 共享资源生命周期

作者场景、PIE、预览可同时使用同一资产；CPU 数据保留时，设备资源可回收并重新上传。实现位于 Toy3dRuntime 的 rendercore，复用公共 RHI；没有独立 RenderResourceSet。显存预算算法、设备丢失后自动恢复和加载调度不属于此生命周期模块。

- Position、StaticMesh、Color、Index、SkinWeight 和 BoneMatrix buffer 以及 TextureResource 通过 RenderResource 统一表达上传、发布、回滚和释放。StaticMeshRenderData、SkeletalMeshRenderData 是普通 owner，组织内部资源并判断整份几何是否可绘制；骨骼 pose buffer 仍归实例。
- RenderResourceRef<T> 保存 typed shared_ptr，并通过 T 的私有 retain()/release() 维护 ref_count_；T 可以是资源或几何 owner，不要求继承 RenderResource。拷贝加一、移动不改变计数、析构归还。计数表示长期渲染使用权，不等于 shared_ptr 引用数或 GPU 在途引用数。几何计数只在 RenderData 上维护，内部 buffer 不重复统计 proxy 使用者。句柄及资源状态仅在逻辑 RT 操作；GT 只传递 owned CPU 数据和稳定身份。
- manager 的 acquire(T&) 要求表示由 shared_ptr 所有，返回 RHIResult<RenderResourceRef<T>>，并在首次获取时安排上传。manager 保活已登记资源或几何 owner，再借用内部 buffer 的稳定地址安排上传；没有独立 RenderResourceSet 或 type-erased owner 基类。引用归零只标记回收，collect_reclaims() 在 recording 已 commit/discard 的 RT 安全点重新检查计数，先解除内部上传地址再撤销 RHI 引用，最后解除 owner 保活；record_pending_uploads() 在开始新录制前自动收集。重新获取句柄取消未执行回收。command list/completion 和后端删除队列继续保证 GPU 使用结束前不销毁 native 对象，不建立第二套 GPU fence。
- 不可变 CPU 几何/像素为重上传来源；提交前候选失败整体回滚。Ready 表示成功提交并发布，非 GPU completion。RenderData 的状态从内部资源推导，不另存一套上传状态；必需 buffer 全部可用后才能绘制。任一 buffer 确定性失败，discard 会取消兄弟 buffer 的上传并保留失败诊断；可重试失败撤回所有候选但保留待上传状态。更新失败保留旧发布资源；Texture binding generation 不因回收归零。
- proxy 持句柄，场景删除只结束该 proxy 的使用权，不查询场景内共享情况。材质渲染配置持有其激活纹理的句柄，场景环境持有 Cube 句柄；CPU 资产/cache 可独立存活，不以 use_count() 决定驻留。
- terminal/退出先停生产并处理录制，再在 RT 撤销所有登记资源和 manager 关联，随后销毁 device；遗留 CPU 资产析构不得回调已销毁 manager。DeviceLost 有限清理。单个资源不能同时绑定不同 manager/device；Vulkan/D3D11/D3D12/mobile 均复用公共生命周期，后端实现/能力状态不因此改变。
- 显式 RT 所有的 bootstrap/临时资源可使用 begin_init(RenderResource&)/release(RenderResource&)；几何 owner 有对应的 begin_init/release 重载。它们不产生渲染使用计数，调用方负责地址存活至 recording 结束。manager 保留其设备归属至 release、owner 析构或 terminal，禁止跨 manager 使用；有使用句柄的资源不能被显式强制释放。普通回收不是终态，Failed 诊断在仍被使用时保留；不可变 buffer 不支持 begin_update，TextureResource 的 update 负责可回滚更新。

RT 使用示例（资源表示已由 StaticMesh 所有，manager 属于当前设备）：

```cpp
auto acquired = manager.acquire(*mesh->render_data());
if (!acquired)
{
    return acquired.status();
}
geometry = std::move(acquired).value();
```

geometry 是 proxy 的 RenderResourceRef<StaticMeshRenderData> 成员，随 proxy 析构自动归还。帧内借用不另计数，借用期间必须有句柄或 manager ownership 保活。

Renderer start 全部成功才 Running/发布 SceneInterface；placeholder/font bootstrap 使用显式 context/list/submit/completion，启动可等待一次，失败保留原 RHIStatus 并回滚。GlobalShaderMap 先验证 required shaders，不能在 draw 时发现启动缺关键程序。

RenderResource 状态 Uninitialized/PendingUpload/Ready/Failed，mutable state 仅逻辑 RT。RenderResourceManager 不拥有 GT 业务对象；它保活登记的共享资源表示，上传集合借用稳定地址，不通过公开 Prepared/Token 类型泄漏记录机制。

- CPU 重上传来源随资源表示保留；当前网格保留不可变打包流，纹理保留最近提交的像素。record_upload 后不是 Ready/GPU complete。
- RHI upload 在返回前复制为所拥有 staging；调用方不得令 command list 持临时 CPU 地址。
- recording commit/discard 由实际 frame/submit 结果驱动；abort/明确未提交可丢录制并重试，确定 resource-local 失败进入 Failed。
- submit 成功后的 present 失败不能撤回已经发生的提交/资源 commit；提交结果不确定进入 terminal，不能当可安全重放。
- Mesh/Texture 的 GT 表示保留稳定 opaque RT 身份；RT 操作资源。删除在 Proxy 更新/移除后的 FIFO 转移所有权释放，不能 GT 先析构再发裸指针命令。
- Texture view 替换更新 generation 以失效 Material cache；仅像素更新不伪造 view 身份变化。真实 GPU 生命周期仍由 command list/completion 保活。

## Terminal 与退出

首次 terminal 错误锁存并停止新业务；已接受 payload、fence、release 仍须清理。关闭 admission、drain 的边界统一，不让排队资源在任意 GT 析构。

退出顺序：停 GT 生产/撤回桥 → drain 已接受命令与 fence → 处理 recording/释放 SceneProxy → manager 撤销全部登记资源的设备驻留并解除关联 → 释放 view/cache、后端临时池/viewport/device → 停 RT → shutdown TaskGraph。terminal 可先撤销驻留再销毁 proxy，遗留句柄仅归还本地计数，不回调 manager。不能 RT 结束后再依赖 RT 执行释放。DeviceLost 走有限清理、不无穷等 completion/idle。

## 修改与验证

测试入口 engine/runtime/tests/rendering_thread_tests.cpp、frame_synchronization_tests.cpp、render_resource_manager_tests.cpp、renderer_scene_ownership_tests.cpp。覆盖多线程/SingleThread、非法 producer、捕获 ownership、terminal 后 fence、bootstrap 失败、record/submit/abort/present、重复退出和 payload 析构线程；RHI/WSI 结果分类见 [RHI](rhi.md)。

改桥/资源生命周期应检查从 GT 创建到最后 GPU completion 的完整链，不只修改一层局部“成功”分支；不能把 CPU fence 改成 GPU 安全证明。

Asset Texture 的 `TextureDesc::cube` 使用方形六面存储；每个 mip payload 顺序连接六 faces，row/slice pitch 描述一个 face。通用 Texture 不限制为环境格式或 512；严格环境 loader 从 EnvironmentAsset 构建 RGBA16F 完整 mips，并声明 `requires_linear_filter`。TextureResource 复用现有 recording/discard/commit/completion 生命周期，逐 face/mip 上传、覆盖完整六层 transitions；失败更新保留旧 binding generation 与 view。
