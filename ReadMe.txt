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
│   │   │   │       ├── vulkan_device.cpp
│   │   │   │       └── vulkan_command_context.cpp
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


//1、 RHI 层
参考 UE的 Global Shader设计，先实现非RDG的方案，让Render线程调用时，和调用原生API很类似：

D:\ue4.27plus\Engine\Source\Runtime\SlateRHIRenderer\Private\SlateRHIRenderingPolicy.cpp
D:\ue4.27plus\Engine\Source\Runtime\Renderer\Private\MobileDecalRendering.cpp

void FDecalRendering::SetShader(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, const FViewInfo& View,
	const FTransientDecalRenderData& DecalData, EDecalRenderStage DecalRenderStage, const FMatrix& FrustumComponentToClip)
{
	const FMaterialShaderMap* MaterialShaderMap = DecalData.MaterialResource->GetRenderingThreadShaderMap();
	const EDebugViewShaderMode DebugViewMode = View.Family->GetDebugViewShaderMode();

	// When in shader complexity, decals get rendered as emissive even though there might not be emissive decals.
	// FDeferredDecalEmissivePS might not be available depending on the decal blend mode.
	TShaderRef<FDeferredDecalPS> PixelShader = (DecalRenderStage == DRS_Emissive && DebugViewMode == DVSM_None)
		? TShaderRef<FDeferredDecalPS>(MaterialShaderMap->GetShader<FDeferredDecalEmissivePS>())
		: MaterialShaderMap->GetShader<FDeferredDecalPS>();

	TShaderMapRef<FDeferredDecalVS> VertexShader(View.ShaderMap);

	{
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		PixelShader->SetParameters(RHICmdList, View, DecalData.MaterialProxy, *DecalData.DecalProxy, DecalData.FadeAlpha);
	}

	// SetUniformBufferParameter() need to happen after the shader has been set otherwise a DebugBreak could occur.

	// we don't have the Primitive uniform buffer setup for decals (later we want to batch)
	{
		auto& PrimitiveVS = VertexShader->GetUniformBufferParameter<FPrimitiveUniformShaderParameters>();
		auto& PrimitivePS = PixelShader->GetUniformBufferParameter<FPrimitiveUniformShaderParameters>();

		// uncomment to track down usage of the Primitive uniform buffer
		//	check(!PrimitiveVS.IsBound());
		//	check(!PrimitivePS.IsBound());

		// to prevent potential shader error (UE-18852 ElementalDemo crashes due to nil constant buffer)
		SetUniformBufferParameter(RHICmdList, VertexShader.GetVertexShader(), PrimitiveVS, GIdentityPrimitiveUniformBuffer);

		if (DebugViewMode == DVSM_None)
		{
			SetUniformBufferParameter(RHICmdList, PixelShader.GetPixelShader(), PrimitivePS, GIdentityPrimitiveUniformBuffer);
		}
	}

	VertexShader->SetParameters(RHICmdList, View.ViewUniformBuffer, FrustumComponentToClip);

	// Set stream source after updating cached strides
	RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);
}

void RenderDeferredDecalsMobile(FRHICommandList& RHICmdList, const FScene& Scene, const FViewInfo& View)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// Build a list of decals that need to be rendered for this view
	FTransientDecalRenderDataList SortedDecals;
	FDecalRendering::BuildVisibleDecalList(Scene, View, DRS_Mobile, &SortedDecals);
	if (SortedDecals.Num())
	{
		SCOPED_DRAW_EVENT(RHICmdList, DeferredDecals);
		INC_DWORD_STAT_BY(STAT_Decals, SortedDecals.Num());

		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
		RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);

		for (int32 DecalIndex = 0, DecalCount = SortedDecals.Num(); DecalIndex < DecalCount; DecalIndex++)
		{
			const FTransientDecalRenderData& DecalData = SortedDecals[DecalIndex];
			const FDeferredDecalProxy& DecalProxy = *DecalData.DecalProxy;
			const FMatrix ComponentToWorldMatrix = DecalProxy.ComponentTrans.ToMatrixWithScale();
			const FMatrix FrustumComponentToClip = FDecalRendering::ComputeComponentToClipMatrix(View, ComponentToWorldMatrix);
						
			const float ConservativeRadius = DecalData.ConservativeRadius;
			const bool bInsideDecal = ((FVector)View.ViewMatrices.GetViewOrigin() - ComponentToWorldMatrix.GetOrigin()).SizeSquared() < FMath::Square(ConservativeRadius * 1.05f + View.NearClippingDistance * 2.0f);
			bool bReverseHanded = false;
			{
				// Account for the reversal of handedness caused by negative scale on the decal
				const auto& Scale3d = DecalProxy.ComponentTrans.GetScale3D();
				bReverseHanded = Scale3d[0] * Scale3d[1] * Scale3d[2] < 0.f;
			}
			EDecalRasterizerState DecalRasterizerState = FDecalRenderingCommon::ComputeDecalRasterizerState(bInsideDecal, bReverseHanded, View.bReverseCulling);
			GraphicsPSOInit.RasterizerState = GetDecalRasterizerState(DecalRasterizerState);

			if (bInsideDecal)
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
					false, CF_Always,
					true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
					false, CF_DepthNearOrEqual,
					true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();
			}
			
			if (bDeferredShading)
			{
				GraphicsPSOInit.BlendState = MobileDeferred_GetDecalBlendState(DecalData.FinalDecalBlendMode, DecalData.bHasNormal);
			}
			else
			{
				GraphicsPSOInit.BlendState = MobileForward_GetDecalBlendState(DecalData.FinalDecalBlendMode);
			}

			// Set shader params
			FDecalRendering::SetShader(RHICmdList, GraphicsPSOInit, View, DecalData, DRS_Mobile, FrustumComponentToClip);
			
			RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer(), 0, 0, 8, 0, UE_ARRAY_COUNT(GCubeIndices) / 3, 1);
		}
	}
}



######### Flax引擎如何封装shader binding

1、一个drawcall最多有4个cb，一个是BindViewData、一个是MaterialShaderDataPerDraw
2、其中1号槽位，为BindViewData只负责视口矩阵的uniform数据
3、0号位置，会遍历所有材质上的cb数据，Material->Bind(bindParams)会通过遍历材质身上的所有uniform、srv、uav等，其中uniform就会叠加到0号slot
4、2号位置负责处理instance的cb数据，MaterialShaderDataPerDraw只负责instance的数据，它会通过dynamic offset映射单个instance的cb数据
5、3号位置负责ddgi的cb数据
6、它在drawcall调用前会执行真正的binding，这个时候，内部有维护一个pipelinestate，甚至是buff状态，如果和前一个一致，就跳过的逻辑
