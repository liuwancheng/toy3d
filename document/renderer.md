# Renderer：View、Mesh Pass、阴影与输出

Forward 仅在可见材质接受 Shadows、其编译 policy 允许 PCF、Primitive 接收阴影且方向光实际投影时建立 atlas；没有接收者/灯光、policy 关闭或缩略图时不分配、清空或绑定替代 atlas。上一提交保留其 GPU 引用；当前帧释放 owner 后使用 Off permutation。各 View 无活动阴影时跳过 shadow pass，其 Pass 参数保留有限的初始化值。

## 定位与帧流程

engine/runtime/renderscene，属于 Toy3dRuntime。GT 准备 owned ViewFamily 与场景更新，在同 FIFO 中先更新再 draw；RT Renderer 组织 frame/context，SceneRenderer 做 View/场景工作。上传/资源/退出见 [Render Framework](render-framework.md)，提交/WSI 见 [RHI](rhi.md)。

正常链：初始化 View/可见性 → MeshBatch → View/Object typed 参数 → Shadow → Base → Tonemap → ImGui → 同一 graphics context/list 的业务提交。每 pass 不单独拥有 frame/submit/present；SceneRenderer 不接管 UI payload 或建立第二套 scheduler。

## Prepare 与 Execute

MeshBatch 保存 PrimitiveSceneProxy、VertexFactory、RHI index binding、draw range、MaterialRenderProxy 与 Object snapshot；各 proxy 通过 collect_mesh_batches 输出 frame-local 输入，具体 geometry owner 负责资源生命周期。MeshBatch 表达 geometry/material/primitive 语义；pass prepare 解析 Program/VertexFactory/材质/资源、验证 layout/附件并创建 pipeline/binding，发生在 begin_render_pass 前。execute 仅消费已准备 MeshDrawCommand 的 RHI refs/value 与 draw 参数，不读 Asset/Material schema、不创建 device resource、不调任务系统。

GPUSkin batch 另持 section 骨骼 typed view 强引用，按 role/vertex factory/Pass selection 精确选择集合中的独立 program；4/8 influence 共用 shader。Object binding 按 Proxy/transform generation/section 共享于同帧 camera/shadow/picking，动画 bounds 随 pose 更新；具体 contract 见 [Animation](animation.md#公共网格边界与-permutation)。`SceneRenderer::view_infos() const` 仅在逻辑 RT 只读检查已准备的当前帧数据，不作为 GT 查询 RenderScene 的入口。

入口 pass/base_pass.h、shadow_pass.h、hit_proxy_pass.h；真实 render_base_pass 接受 device、shader cache、graphics context、BasePassInputs 和 draw list，不存在通用 TestPass::execute(RenderPassContext&) 协议。新 pass 复用既有边界，不为减少参数引入 Prepared/Token wrapper。

Viewport/scissor、顶点/索引/pipeline/bindings/draw args/sort key 明确；附件不藏在跨 pass 长期 MeshDrawCommand 内。recording 失败完整 discard，不能部分 draw 后成功。

## Shadow：选择与几何

ForwardSceneRenderer 一次选择最高 priority 的 enabled 方向光，stable tie 保持注册顺序；intensity=0 或 cast_shadows=false 也不能让低 priority 替补。light direction 是光传播方向，照明 N·L 使用相反方向。Primitive visible 为总体开关，cast/receives 独立；不可见不投射，不接收仍正常照明。

settings：cascade count 1..3，distribution exponent 0.1..10，tile size 512/1024/2048，有限 shadow_distance≥0、fade fraction [0,1)、bias 三项 [0,1]；默认关闭、1 层、E=3、S=2048、distance=10000 cm、fade=0.1、caster constant/slope=0.5、receiver=0.9。setter 非法输入原子拒绝。

每 View 接收范围 near 到 min(finite camera far, shadow_distance)，无限 far 用 shadow_distance；end≤near 禁用。不能用无限投影 far 角点伪造有限范围。

- 光 basis：f=归一传播方向，u0=world+Y，abs(dot(f,u0))>0.99 固定改+X；r=normalize(cross(u0,f))，u=cross(f,r)，+Z=f，LH。不能随 caster AABB 主轴旋转。
- 接收角点包围球固定半径避免随 camera 旋转缩放。有效 tile 边长 R，texel_world=2*radius/R，extent=radius+2*texel_world，texel_step=2*extent/R；球心 r/u 坐标按 step 最近格点 snap。
- caster 从全部 visible&&cast_shadows 的 Primitive world AABB 按光空间 XY/上游区间筛选，不能仅取 camera visible batch。Z 包住接收段和候选，加有限 padding；保持 0<near<far。退化/非有限错误不发布半个阴影状态。
- cascade 权重 1,E,E²（取 N 项），边界按累计权重分 near..end；E=1 等距，E=3 的两层 25%、三层约7.69%/30.77%。混合半宽为相邻较短段 10%，覆盖范围含 overlap，各层独立 sphere/snap/caster/Z；减少层数清旧状态。

## Shadow：Atlas 与采样

tile 分配 [S]、[S,S/2]、[S,S/2,S/2]；atlas 一层 S×S，两/三层 (S+S/2)×S；位置 (0,0)、(S,0)、(S,S/2)。每侧 border=4，有效边长 tile_size-8，2↔3 层复用纹理。limits 不足只按 2048→1024→512 降 S，不减 cascade；最小仍不足 Unsupported/保旧资源。

SceneRenderTargets 在 RT 持有每 View ShadowRenderTargets：D32Float、单 mip/layer/sample、DS|SRV、Depth-only SRV。完整候选后替换，旧资源由 list 持至 completion；普通重建不 submit/wait_idle。

所有有效 cascade draw 先 prepare；一次 pass 整 atlas Clear=0/Store，tile viewport/scissor 仅内部，边框保留 clear；结束整图 DepthStencilWrite→ShaderResourceGraphics。不逐层清共享图，不给 ShadowDepth shader 绑定正在写的 atlas。独立深度 shader 使用 Object/Pass，零 color attachment，保持当前所需最小 pixel stage。

caster bias 在 ShadowDepth vertex shader，符号遵守 reversed-Z；采样以 atlas 倒数宽高步长，tile 本地 clamp/边框防串层，深度软过渡和相机距离 fade/级联混合独立。不能用增 bias 掩盖世界 texel 精度不足。具体 PCF/Gather/soft transition 以 engine/shader 及 pass/shadow_pass.cpp 的实现和金值测试为证据，不能改成传统 Z 比较或假称硬件 raster depth bias 已有。

## 场景环境

World 的 settings 与 CPU Cube 快照通过 SceneInterface 的 FIFO 发布，RenderScene 每个场景域独占自己的 GPU TextureResource。更新先准备完整候选；recording 的环境 view 可用于当前帧，成功 submit/资源 commit 后 `resolve_environment_recording(true)` 接管，discard 保留旧有效环境。上传/格式/过滤能力失败明确诊断，不把已配置资源当 Off。

Forward View 参数提供环境逆旋转、强度和最高 mip；材质 feature/policy 与有效环境共同选择 Sky/Off 的 Program，active Cube/sampler 只在需要时绑定。PBR 使用镜面 IBL，没有球谐、环境漫反射或假 ambient。单个场景域一个环境，主场景、PIE 和 studio preview 相互独立；Asset/World 持久化见 [Assets](assets.md#环境资产) 与 [Editor](editor.md#场景环境与材质预览)。

## Tonemap、UI、Preview 与读回

场景 HDR color/depth 经 Base，Tonemap 转输出；UI 在最终输出之后按既有线性/显示约定合成，不因预览直接绕后端。Tonemap 参数和 Shader ABI 从 generated typed schema 创建，附件兼容和失败检查在 prepare。

材质窗口请求可在 Base 后、Tonemap 前绘制 EnvironmentBackground Global Pass。全屏三角形使用 reversed-Z 的零深度与 Equal 测试，只填未被几何覆盖的像素；加载同一 HDR color/depth 前显式建立写入到读取的依赖，不写深度。相机射线按环境逆旋转采样 Cube mip 0，与镜面 IBL 共用该场景域资源；背景隐藏不改变反射。窗口固定曝光随不可变 PreviewFrameRequest 传到 Tonemap，缩略图保持 EV 0 和禁用背景/阴影。背景 pipeline 延迟创建，Global Shader 候选准备/提交与旧资源退役沿既有原子发布边界。

UI texture 通过公共 RHI View/受控 ImGui 表示，多个窗口各自持资源/代次，不全局换一张图。Preview 使用独立 World/SceneRenderTargets、同正常 frame 管理；不能操纵主 World、给每个窗口私建 Vulkan ownership。

UI 快照遇到未登记纹理仍拒绝整帧，并诊断 draw-list/command、所属窗口、实际 ID、类别、元素数及允许的 font/viewport/additional ID。additional 列表最多显示16项并保留总数；窗口名仅用于排查，不作为资源身份。诊断不改变纹理登记或退役时序。

Editor 的 Play RenderScene 与编辑/Preview Scene 独立，Renderer 持有、Running 后发布 SceneInterface，退出先撤回。ViewportFrameOutput 的 play_scene 在提交时选择对应场景，复用主视口附件；编辑注册不随显示切换释放。各场景 MeshRenderData 生命周期独立。SceneRenderFeedback 是单次准备反馈，RT 在 geometry 可绘制且场景帧提交成功后发布 Ready，不能作为 GPU completion；启动失败发布诊断，UI/GT 不读取可变 RenderScene。运行会话与输入边界见 [Editor](editor.md#视口内-play)。

异步颜色读回必须等待实际 GPU completion，结果包含请求身份、尺寸/row pitch/格式；worker PNG 编码/缓存后 GT 检验 AssetId/content/generation 才接管，过期/失败丢弃。缩略图失败不回滚资产保存。

## 修改与验证

主要代码 forward_scene_renderer、scene_render_targets、pass/、postprocess/tonemap_pass、imgui_renderer；测试 engine/runtime/tests/tonemap_pass_tests.cpp、imgui_system_tests.cpp、renderer_scene_ownership_tests.cpp，以及 engine/editor/tests/thumbnail_integration_tests.cpp、texture_preview_image_tests.cpp。

新 pass 验证 prepare/execute 分层、附件/格式、状态和 owned refs、失败 discard；阴影检查 priority 平局/零强度、离屏 caster、near/end、E=1/3、atlas border/limits、层数下降清理、bias/fade/reversed-Z。相关自动测试之外补真实画面、最小化/重建、多个预览与退出，未覆盖的平台明示。
