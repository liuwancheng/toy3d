## MODIFIED Requirements

### Requirement: 一帧只录制一个 graphics list
第一阶段每个 viewport Draw SHALL `begin_frame()`、创建一个 graphics context、录制 pending uploads、执行 `init_views()` 与 `compute_view_visibility()`、录制全部正式 scene graphics pass、以独立Tonemap pass把HDR SceneColor输出到最终目标、录制可选的独立ImGui pass、finish 一个 immutable list并 `end_frame()`。Base Pass、Tonemap和ImGui MUST使用该context，不得创建隐藏command list、独立submit或等待GPU。Tonemap与ImGui MUST分别begin/end自己的RHI render pass；该职责分离同样适用于Editor离屏输出和无Scene UI frame。

#### Scenario: Cube project 正常帧
- **WHEN** Cube project投递包含可见StaticMesh和可选UI payload的一次Draw
- **THEN** pending uploads、View初始化、visibility、Forward Base Pass、Tonemap和可选ImGui MUST按显式顺序串行录入同一context，并由一次 `end_frame()` 提交

#### Scenario: ImGui payload 为空
- **WHEN** 当前frame没有UI draw command
- **THEN** 同一graphics list MUST仍完成Base Pass、Tonemap与Present transition，且不得创建第二个空UI list或空render pass

## ADDED Requirements

### Requirement: SceneRenderer 与 UI payload 职责分离
一次性UI draw payload MAY与SceneRenderer由同一Draw command携带并在同一frame orchestration消费，但SceneRenderer的View/visibility/MeshBatch状态 MUST不拥有或修改ImGui context。ImGui rendering MUST能在没有合法Scene draw的Editor frame中由Renderer最终输出编排独立执行。

#### Scenario: GT 投递后继续构建下一帧UI
- **WHEN** frame N 的SceneRenderer和UI payload已经move进Draw command
- **THEN** GT MAY立即开始frame N+1的scene/UI构建，logical RT不得回读GT ImGui context或Application状态
