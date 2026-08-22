## Purpose

定义 StaticMesh Asset 的 RenderData、各类 vertex/index buffer、VertexFactory、整体 ready gate 与 replacement 边界，使 Mesh 不会以部分可用状态参与 Draw。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `StaticMeshRenderData` | 新增 class | StaticMesh-owned stable representation；协调 LOD 子资源与整体 ready/replacement；自身不继承 RenderResource |
| `PositionVertexBuffer` | 新增 RenderResource | 拥有 position initial payload 与 RHIBuffer |
| `StaticMeshVertexBuffer` | 新增 RenderResource | 拥有 normal/tangent/UV initial payload 与 RHIBuffer |
| `ColorVertexBuffer` | 新增可选 RenderResource | 拥有 vertex color payload；缺失时使用明确默认路径 |
| `IndexBuffer` | 新增 RenderResource | 拥有 index payload、format 与 RHIBuffer |
| `VertexFactory` | 新增 class | RT 初始化并组合 buffer layout/binding；不拥有 Mesh Asset，是否继承 RenderResource由本 Spec固定为不继承 |

## ADDED Requirements

### Requirement: 子资源独立初始化
position、vertex attributes、optional color 和 index buffer SHALL 作为独立 RenderResource 初始化，VertexFactory MUST 在所需 buffer contract 可用后初始化。

#### Scenario: 同帧初始化与 Draw
- **WHEN** 所有 Mesh 子资源在本帧录制 upload
- **THEN** upload、transition 和使用这些资源的 Draw MAY 在同一业务 list 内执行，无需等待 GPU

### Requirement: 整体 ready gate
StaticMeshRenderData MUST 在全部必要子资源成功 submit 后才发布整体可绘制；任一必要子资源失败时不得部分 Draw。

#### Scenario: Index buffer 失败
- **WHEN** vertex buffers 成功而 index buffer 初始化失败
- **THEN** Mesh MUST fallback 或 skip，并记录完整诊断

### Requirement: 第一阶段一次初始化全部 LOD
第一阶段 SHALL 一次初始化 Asset 中全部 LOD，不实现 streaming、partial residency 或 upload byte budget。

#### Scenario: 多 LOD Mesh
- **WHEN** Mesh render data 开始 init
- **THEN** 所有 LOD 必要资源 MUST 加入同一生命周期协调范围

### Requirement: Replacement 使用完整候选 RenderData
重建 Mesh MUST 创建新的完整 StaticMeshRenderData，全部 ready 后切换引用，再通过 ownership transfer 释放旧对象。

#### Scenario: Candidate 失败
- **WHEN** 新 RenderData 任一必要资源失败
- **THEN** 当前 active RenderData MUST 保持可用且不得被提前释放
