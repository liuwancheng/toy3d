# Render Framework：GT/RT、资源与退出

## 定位与 ownership

engine/runtime/rendercore 的 rendering_thread、render_command、frame_synchronization 是 Game/Render 桥；renderscene 的 Renderer/RenderResource/Manager 管渲染生命周期。当前都属于 Toy3dRuntime，不能臆造独立 Toy3dRenderCore target。

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

Renderer start 全部成功才 Running/发布 SceneInterface；placeholder/font bootstrap 使用显式 context/list/submit/completion，启动可等待一次，失败保留原 RHIStatus 并回滚。GlobalShaderMap 先验证 required shaders，不能在 draw 时发现启动缺关键程序。

RenderResource 状态 Uninitialized/PendingUpload/Ready/Failed/Released，mutable state 仅逻辑 RT。RenderResourceManager 是非 owning 集合，不拥有 GT 对象；资源地址稳定，不通过公开 Prepared/Token 类型泄漏记录机制。

- CPU 初始数据保留至该资源所随的业务 recording **submit 成功**；record_upload 后不是 Ready/GPU complete。
- RHI upload 在返回前复制为所拥有 staging；调用方不得令 command list 持临时 CPU 地址。
- recording commit/discard 由实际 frame/submit 结果驱动；abort/明确未提交可丢录制并重试，确定 resource-local 失败进入 Failed。
- submit 成功后的 present 失败不能撤回已经发生的提交/资源 commit；提交结果不确定进入 terminal，不能当可安全重放。
- Mesh/Texture 的 GT 表示保留稳定 opaque RT 身份；RT 操作资源。删除在 Proxy 更新/移除后的 FIFO 转移所有权释放，不能 GT 先析构再发裸指针命令。
- Texture view 替换更新 generation 以失效 Material cache；仅像素更新不伪造 view 身份变化。真实 GPU 生命周期仍由 command list/completion 保活。

## Terminal 与退出

首次 terminal 错误锁存并停止新业务；已接受 payload、fence、release 仍须清理。关闭 admission、drain 的边界统一，不让排队资源在任意 GT 析构。

退出顺序：停 GT 生产/撤回桥 → drain 已接受命令与 fence → 清 manager 非 owning 集合 → 释放 SceneProxy、view、资源/cache → 后端临时池/viewport/device → 停 RT → shutdown TaskGraph。不能 RT 结束后再依赖 RT 执行释放。DeviceLost 走有限清理、不无穷等 completion/idle。

## 修改与验证

测试入口 engine/runtime/tests/rendering_thread_tests.cpp、frame_synchronization_tests.cpp、render_resource_manager_tests.cpp、renderer_scene_ownership_tests.cpp。覆盖多线程/SingleThread、非法 producer、捕获 ownership、terminal 后 fence、bootstrap 失败、record/submit/abort/present、重复退出和 payload 析构线程；RHI/WSI 结果分类见 [RHI](rhi.md)。

改桥/资源生命周期应检查从 GT 创建到最后 GPU completion 的完整链，不只修改一层局部“成功”分支；不能把 CPU fence 改成 GPU 安全证明。
