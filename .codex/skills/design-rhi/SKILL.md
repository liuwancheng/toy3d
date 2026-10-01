---
name: design-rhi
description: 设计、实现或审查 Toy3d 公共 RHI 与图形后端；适用于修改设备/资源/命令/提交/binding 接口、检查 RenderScene 边界，以及评估 Vulkan、D3D11、D3D12 和移动端 profile 可实现性。
---

# RHI 工作方法

先从 document/index.md 路由到 document/rhi.md 的相关章节；涉及参数读 shader.md，涉及资源/GT-RT 读 render-framework.md。产品 contract 只维护在功能文档，不在 skill 建需求副本或长期审查台账。

1. 从调用方需要的通用操作出发，核对公共头、backend hook、原生实现、上层 caller 和测试完整链；不从 Vulkan 签名反推公共层。
2. 确定层次：schema/typed 编码、公共 descriptor/validation、device 创建、context recording、queue/viewport、业务 pass。不要用 wrapper 遮蔽职责混杂。
3. 列 Vulkan、D3D11 FL11_0/SM5、D3D12、移动 Vulkan profile 的实现、限制/安全降级和 Unsupported；设计评估不能描述成已实现/已测试。
4. 写清 creator、CPU owner、recording refs、GPU completion、destroyer、线程和失败状态；区分录制、提交、GPU 完成、present。
5. 修改顺序为公共语义/validation → backend hook/native → caller → 对应文档；检查跨 device、枚举转换、临时地址、失败回滚、terminal/退出。
6. C++ 完成后交 sub-agent 使用 verify-toy3d-build 独立验证；纯文档/skill 核对结构、真实接口和引用，不构建。

需要解释 UE4.27 架构来源时才读 references/ue427-rhi-summary.md；需要精确外部事实再查真实源码，区分参考与本项目选择，不照搬 UE 宏/对象/复杂调度。

审查优先报告正确性与生命周期风险，其次规范冲突和可维护性；每项包含文件位置、触发、影响及最小建议。新发现的代码问题与用户确认迭代范围，不能把过期结论长期写入 skill。
