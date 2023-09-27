//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12HelloTriangle.h"
#include <stdexcept>

// #RTX includes
#include "DXRHelper.h"
#include "nv_helpers_dx12/BottomLevelASGenerator.h"
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"
#include "glm/gtc/type_ptr.hpp"
#include "manipulator.h" 
#include "Windowsx.h"
#include <iostream>
#define NumRayTypes 2
D3D12HelloTriangle::D3D12HelloTriangle(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	m_frameIndex(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_rtvDescriptorSize(0)
{
}
// #RTX
void D3D12HelloTriangle::CheckRaytracingSupport() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {}; 
	ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))); 
	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) 
		throw std::runtime_error("Raytracing not supported on device");
}
void D3D12HelloTriangle::OnInit()
{
	LoadPipeline();
	LoadAssets();

	// #RTX---------------------------------------------------------------
	// Check the raytracing capabilities of the device 
	CheckRaytracingSupport();
	CreateShaderResourceHeap();
	// Setup the acceleration structures (AS) for raytracing. When setting up 
	// geometry, each bottom-level AS has its own transform matrix. 
	CreateAccelerationStructures();

	//QUESTIONABLE IF WE NEED IT HERE 
	ThrowIfFailed(m_commandList->Close());

	CreateRaytracingPipeline();
	CreatePerInstanceConstantBuffers();

	// Allocate the buffer for output #RTX image
	CreateRaytracingOutputBuffer();
	// Camera
	nv_helpers_dx12::CameraManip.setWindowSize(GetWidth(), GetHeight());
	nv_helpers_dx12::CameraManip.setLookat(glm::vec3(1.5f, 1.5f, 1.5f), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	CreateCameraBuffer();
	// Create the buffer containing the raytracing result (always output in a
	// UAV), and create the heap referencing the resources used by the raytracing,
	// such as the acceleration structure
	CreateShaderBindingTable();
	//--------------------------------------------------------------------

}
// --------------ROOT SIGNATURES----------------------------
// Create #RTX RAYGEN Root Signature
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateRayGenSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc; 

	rsc.AddHeapRangesParameter(
		{ {0 /*u0*/, 1 /* num desc */, 0 /*register space*/,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV output*/,
		1 /*heap slot where the UAV is defined*/}, 
		//{0 /*t0*/, 1, 0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV /*TLAS*/, 0},
		//{0, 1, 0,D3D12_DESCRIPTOR_RANGE_TYPE_CBV /*Cam*/ ,2} 
		});

	//rsc.AddHeapRangesParameter({ {0, 1, 0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0},{0, 1, 0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1} });
	
	return rsc.Generate(m_device.Get(), true);
}
// Create #RTX  HIT root signture (is empty for now, no resources needed)
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateHitSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	// CB colors
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0);
	// vertices data
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); 
	// indices
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1);
	// Scene BVH
	rsc.AddHeapRangesParameter({ { 2 /*t2*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0 /*2nd slot of the heap*/ },});

	return rsc.Generate(m_device.Get(), true);
}
// Create #RTX  MISS root signture (is empty for now)
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateMissSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc; 
	return rsc.Generate(m_device.Get(), true);
}
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateGlobalSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	// CB indices
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, 0, 1, 2);
	return rsc.Generate(m_device.Get(), false);
}

// ----------------------------------------------------
// ---------CREATE RAYTRACING PIPELINE (similar to PSO in rasterization)------
void D3D12HelloTriangle::CreateRaytracingPipeline()
{
	m_globalSignature = CreateGlobalSignature();
	nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get(), m_globalSignature.Get());

	

	
	// The pipeline contains the DXIL code of all the shaders potentially executed
	// during the raytracing process. This section compiles the HLSL code into a
	// set of DXIL libraries. We chose to separate the code in several libraries
	// by semantic (ray generation, hit, miss) for clarity. Any code layout can be
	// used.
	m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"RayGen.hlsl");
	m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Miss.hlsl");
	m_hitLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Hit.hlsl");

	// SHADING-----------------------
	m_shadowLibrary = nv_helpers_dx12::CompileShaderLibrary(L"ShadowRay.hlsl");
	pipeline.AddLibrary(m_shadowLibrary.Get(), { L"ShadowClosestHit", L"ShadowMiss" });
	m_shadowSignature = CreateMissSignature();
	//------------------------------
	// In a way similar to DLLs, each library is associated with a number of
	// exported symbols. This
	// has to be done explicitly in the lines below. Note that a single library
	// can contain an arbitrary number of symbols, whose semantic is given in HLSL
	// using the [shader("xxx")] syntax
	pipeline.AddLibrary(m_rayGenLibrary.Get(), { L"RayGen" });
	pipeline.AddLibrary(m_missLibrary.Get(), { L"Miss" });
	pipeline.AddLibrary(m_hitLibrary.Get(), { L"ClosestHit", L"PlaneClosestHit"});

	// To be used, each DX12 shader needs a root signature defining which
	// parameters and buffers will be accessed.
	m_rayGenSignature = CreateRayGenSignature();
	m_missSignature = CreateMissSignature();
	m_hitSignature = CreateHitSignature();


	// Hit group for the triangles, with a shader simply interpolating vertex
	// colors
	pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");
	pipeline.AddHitGroup(L"PlaneHitGroup", L"PlaneClosestHit");
	pipeline.AddHitGroup(L"ShadowHitGroup", L"ShadowClosestHit");
	// The following section associates the root signature to each shader. Note
 // that we can explicitly show that some shaders share the same root signature
 // (eg. Miss and ShadowMiss). Note that the hit shaders are now only referred
 // to as hit groups, meaning that the underlying intersection, any-hit and
 // closest-hit shaders share the same root signature.
	pipeline.AddRootSignatureAssociation(m_rayGenSignature.Get(), { L"RayGen" });
	pipeline.AddRootSignatureAssociation(m_missSignature.Get(), { L"Miss" });
	pipeline.AddRootSignatureAssociation(m_hitSignature.Get(), { L"HitGroup" });
	pipeline.AddRootSignatureAssociation(m_shadowSignature.Get(),
		{ L"ShadowHitGroup" });
	pipeline.AddRootSignatureAssociation(m_missSignature.Get(),
		{ L"Miss", L"ShadowMiss" });
	pipeline.AddRootSignatureAssociation(m_hitSignature.Get(),
		{ L"HitGroup", L"PlaneHitGroup" });



	// The payload size defines the maximum size of the data carried by the rays,
	// ie. the the data
	// exchanged between shaders, such as the HitInfo structure in the HLSL code.
	// It is important to keep this value as low as possible as a too high value
	// would result in unnecessary memory consumption and cache trashing.
	pipeline.SetMaxPayloadSize(8 * sizeof(float)); // RGB + distance

	// Upon hitting a surface, DXR can provide several attributes to the hit. In
	// our sample we just use the barycentric coordinates defined by the weights
	// u,v of the last two vertices of the triangle. The actual barycentrics can
	// be obtained using float3 barycentrics = float3(1.f-u-v, u, v);
	pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates

	// The raytracing process can shoot rays from existing hit points, resulting
	// in nested TraceRay calls. Our sample code traces only primary rays, which
	// then requires a trace depth of 1. Note that this recursion depth should be
	// kept to a minimum for best performance. Path tracing algorithms can be
	// easily flattened into a simple loop in the ray generation.
	pipeline.SetMaxRecursionDepth(2);

	// Compile the pipeline for execution on the GPU
	m_rtStateObject = pipeline.Generate();

	// Cast the state object into a properties object, allowing to later access
	// the shader pointers by name
	ThrowIfFailed(
		m_rtStateObject->QueryInterface(IID_PPV_ARGS(&m_rtStateObjectProps)));

}

void D3D12HelloTriangle::CreateRaytracingOutputBuffer() {
	D3D12_RESOURCE_DESC resDesc = {}; 
	resDesc.DepthOrArraySize = 1; 
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; 
	// The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB 
	// formats cannot be used with UAVs. For accuracy we should convert to sRGB 
	// ourselves in the shader 
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; 
	resDesc.Width = GetWidth(); 
	resDesc.Height = GetHeight(); 
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; 
	resDesc.MipLevels = 1; 
	resDesc.SampleDesc.Count = 1; 
	ThrowIfFailed(m_device->CreateCommittedResource( &nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&m_outputResource)));
	// RTX output
	m_RTOutputHeapIndex = nv_helpers_dx12::CreateBufferView(m_device.Get(), m_outputResource.Get(), m_outputResource->GetGPUVirtualAddress(),
			m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::UAV);
}
void D3D12HelloTriangle::CreateShaderResourceHeap() { 
	m_CbvSrvUavHeap = nv_helpers_dx12::CreateDescriptorHeap( m_device.Get(), 65536, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true); 
	m_CbvSrvUavHandle = m_CbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
}
void D3D12HelloTriangle::CreateShaderBindingTable() { 
	// The SBT helper class collects calls to Add*Program.  If called several
  // times, the helper must be emptied before re-adding shaders.
	m_sbtHelper.Reset();
	// The pointer to the beginning of the heap is the only parameter required by
	// shaders without root parameters
	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle =
		m_CbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	// The helper treats both root parameter pointers and heap pointers as void*,
	// while DX12 uses the
	// D3D12_GPU_DESCRIPTOR_HANDLE to define heap pointers. The pointer in this
	// struct is a UINT64, which then has to be reinterpreted as a pointer.
	auto heapPointer = reinterpret_cast<UINT64*>(srvUavHeapHandle.ptr);
	// The ray generation only uses heap data
	m_sbtHelper.AddRayGenerationProgram(L"RayGen", { heapPointer });
	// The miss and hit shaders do not access any external resources: instead they
	// communicate their results through the ray payload
	m_sbtHelper.AddMissProgram(L"Miss", {});
	m_sbtHelper.AddMissProgram(L"ShadowMiss", {});
	// FOR PER-INSTANCE DATA WE NEED TO PUSH ALL CBs IN THE INSTANCE ORDER TO THE SBT
	for (int i = 0; i < 3; ++i) {
		// Even though we will not use heap Pointer for this sahder function, it is passed, as it is specified in RS
		// We can also just ignore it, but then it make things not clear
		m_sbtHelper.AddHitGroup(L"HitGroup", { 
			(void*)(m_perInstanceConstantBuffers[i]->GetGPUVirtualAddress()),
			// for now we just hardcode these values. I do not know if we actually will need to pass Vertex and Index data, but useful to know, how to do it
			(void*)(m_modelVertexAndNum[i].first->GetGPUVirtualAddress()),
			(void*)(m_modelIndexAndNum[i].first->GetGPUVirtualAddress()),
			heapPointer });

		m_sbtHelper.AddHitGroup(L"ShadowHitGroup", {});
	}
	//  !!!!!!ALL DATA FOR NOW IS JUST A FILLER FOR THE EXAMPLE OF USE
	m_sbtHelper.AddHitGroup(L"PlaneHitGroup",
		{(void*)(m_perInstanceConstantBuffers[0]->GetGPUVirtualAddress()),
		(void*)(m_modelVertexAndNum[0].first->GetGPUVirtualAddress()),
			(void*)(m_modelIndexAndNum[0].first->GetGPUVirtualAddress()),
		heapPointer });
	m_sbtHelper.AddHitGroup(L"ShadowHitGroup", {});
	m_sbtHelper.AddHitGroup(L"HitGroup", { 
		(void*)(m_perInstanceConstantBuffers[3]->GetGPUVirtualAddress()),
		(void*)(m_modelVBVs[0].BufferLocation),
			(void*)(m_modelIndexAndNum[0].first->GetGPUVirtualAddress()),
		heapPointer});
	m_sbtHelper.AddHitGroup(L"ShadowHitGroup", {});



	// Compute the size of the SBT given the number of shaders and their
	// parameters
	uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();
	// Create the SBT on the upload heap. This is required as the helper will use
	// mapping to write the SBT contents. After the SBT compilation it could be
	// copied to the default heap for performance.
	m_sbtStorage = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	if (!m_sbtStorage) {
		throw std::logic_error("Could not allocate the shader binding table");
	}
	// Compile the SBT from the shader and parameters info
	m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());
}
// Load the rendering pipeline dependencies.
void D3D12HelloTriangle::LoadPipeline()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_12_1,
			IID_PPV_ARGS(&m_device)
			));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_12_1,
			IID_PPV_ARGS(&m_device)
			));
	}

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it.
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
		));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}
	}

	ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}
	}
}


// Load the sample assets.
void D3D12HelloTriangle::LoadAssets()
{

	//LoadModel("Assets/car/scene.gltf"); 
	LoadModel("Assets/car/scene.gltf");
	//LoadModel("Assets/cars2/scene.gltf");
	//LoadModel("Assets/Cube/Cube.gltf");
	// Wait for the command list to execute; we are reusing the same command 
	// list in our main loop but for now, we just want to wait for setup to 
	// complete before continuing.
	WaitForPreviousFrame();
}

// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
	UpdateCameraBuffer();
	// ANIMATE 
	m_time++;
	m_instances[0].second = XMMatrixScaling(0.01f, 0.01f, 0.01f) * XMMatrixRotationAxis({ 0.f, 1.f, 0.f }, static_cast<float>(m_time) / 50.0f) * XMMatrixTranslation(1.f, 0.1f * cosf(m_time / 20.f), 1.f);
	m_instances[1].second = XMMatrixScaling(0.003f, 0.003f, 0.003f) * XMMatrixRotationAxis({ 0.f, 0.f, 1.f }, static_cast<float>(m_time) / 50.0f) * XMMatrixTranslation(0.f, 0.1f * cosf(m_time / 20.f), 1.f);
	m_instances[2].second = XMMatrixScaling(0.007f, 0.007f, 0.007f) * XMMatrixRotationAxis({ 0.f, -1.f, 0.f }, static_cast<float>(m_time) / 50.0f) * XMMatrixTranslation(-1.f, 0.1f * cosf(m_time / 20.f), 0.f);

	m_instances[3].second = XMMatrixScaling(0.05f, 0.002f, 0.05f) * XMMatrixTranslation(0.f, - 1.f, 0.f);
	m_instances[4].second = XMMatrixScaling(0.01f, 0.01f, 0.01f);
}

// Render the scene.
void D3D12HelloTriangle::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(1, 0));

	WaitForPreviousFrame();
}

void D3D12HelloTriangle::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForPreviousFrame();

	CloseHandle(m_fenceEvent);
}
void D3D12HelloTriangle::OnKeyUp(UINT8 key)
{ // Alternate between rasterization and raytracing using the spacebar 
	if (key == VK_SPACE)
		m_raster = !m_raster;
}
void D3D12HelloTriangle::PopulateCommandList()
{
	
	ThrowIfFailed(m_commandAllocator->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
	
	// UNCOMMENT when add ImGui / Debug lines 
	//CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	//m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	
	//------------------------- #RTX PREPARE FRAME AND RENDER---------------------------------------------
	
	CreateTopLevelAS(m_instances, true); // Update TLAS for Animations
	
	std::vector<ID3D12DescriptorHeap*> heaps = { m_CbvSrvUavHeap.Get() };
	m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

	// Set global RS
	m_commandList->SetComputeRootSignature(m_globalSignature.Get());

	
	CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	m_commandList->ResourceBarrier(1, &transition);
	D3D12_DISPATCH_RAYS_DESC desc = {};
	// The layout of the SBT is as follows: ray generation shader, miss
	// shaders, hit groups. As described in the CreateShaderBindingTable method,
	// all SBT entries of a given type have the same size to allow a fixed stride.
	// The ray generation shaders are always at the beginning of the SBT.
	uint32_t rayGenerationSectionSizeInBytes = m_sbtHelper.GetRayGenSectionSize();
	desc.RayGenerationShaderRecord.StartAddress = m_sbtStorage->GetGPUVirtualAddress();
	desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;
	// The miss shaders are in the second SBT section, right after the ray
	// generation shader. We have one miss shader for the camera rays and one
	// for the shadow rays (not yet), so this section has a size of 2*m_sbtEntrySize. We
	// also indicate the stride between the two miss shaders, which is the size
	// of a SBT entry
	uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
	desc.MissShaderTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
	desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
	desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();
	// The hit groups section start after the miss shaders. In this sample we
	// have one 1 hit group for the triangle (for other cases we can use >1 like multiple pixel shaders)
	uint32_t hitGroupsSectionSize = m_sbtHelper.GetHitGroupSectionSize();
	desc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes + missSectionSizeInBytes;
	desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
	desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();
	// Window size to dispatch primary rays
	desc.Width = GetWidth();
	desc.Height = GetHeight();
	desc.Depth = 1;
	// Bind the raytracing pipeline
	m_commandList->SetPipelineState1(m_rtStateObject.Get());

	HeapOffsets offsets;
	offsets.TlasIndex = m_TlasHeapIndex;
	offsets.camIndex = m_camHeapIndex;
	m_commandList->SetComputeRoot32BitConstants(0, 2, reinterpret_cast<void*>(&offsets), 0);
	// ----------DRAWING ------------------------------------------
	// Dispatch the rays and write to the raytracing output
	m_commandList->DispatchRays(&desc);
	//-----------COPY OUTPUT TO RTV + RETURN STATES----------------
	// The raytracing output needs to be copied to the actual render target used
	// for display. For this, we need to transition the raytracing output from a
	// UAV to a copy source, and the render target buffer to a copy destination.
	// We can then do the actual copy, before transitioning the render target
	// buffer into a render target, that will be then used to display the image
	transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	m_commandList->ResourceBarrier(1, &transition);
	transition = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
	m_commandList->ResourceBarrier(1, &transition);
	m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(), m_outputResource.Get());
	transition = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_commandList->ResourceBarrier(1, &transition);

	//-------------------------------------------------------------------
	

	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloTriangle::WaitForPreviousFrame()
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// #RTX 
// Create a bottom-level acceleration structure based on a list of vertex
// buffers in GPU memory along with their vertex count. The build is then done
// in 3 steps: gathering the geometry, computing the sizes of the required
// buffers, and building the actual AS
D3D12HelloTriangle::AccelerationStructureBuffers
D3D12HelloTriangle::CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers,
										std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers) {

	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS; 
	// Adding all vertex buffers and not transforming their position for now
	for (size_t i = 0; i < vVertexBuffers.size(); i++) {
		if (i < vIndexBuffers.size() && vIndexBuffers[i].second > 0)
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0, vVertexBuffers[i].second, sizeof(Vertex), vIndexBuffers[i].first.Get(), 0, vIndexBuffers[i].second, nullptr, 0, true);
		else
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0, vVertexBuffers[i].second, sizeof(Vertex), 0, 0);
	}
	// The AS build requires some scratch space to store temporary information. 
	// The amount of scratch memory is dependent on the scene complexity. 
	UINT64 scratchSizeInBytes = 0; 
	// The final AS also needs to be stored in addition to the existing vertex 
	// buffers. It size is also dependent on the scene complexity. 
	UINT64 resultSizeInBytes = 0; 
	bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes, &resultSizeInBytes); 
	// Once the sizes are obtained, the application is responsible for allocating 
	// the necessary buffers. Since the entire generation will be done on the GPU, 
	// we can directly allocate those on the default heap 
	AccelerationStructureBuffers buffers; 
	buffers.pScratch = nv_helpers_dx12::CreateBuffer( m_device.Get(), scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, nv_helpers_dx12::kDefaultHeapProps); 
	buffers.pResult = nv_helpers_dx12::CreateBuffer( m_device.Get(), resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nv_helpers_dx12::kDefaultHeapProps); 
	// Build the acceleration structure. Note that this call integrates a barrier 
	// on the generated AS, so that it can be used to compute a top-level AS right 
	// after this method. 
	bottomLevelAS.Generate(m_commandList.Get(), buffers.pScratch.Get(), buffers.pResult.Get(), false, nullptr); 
	return buffers;
}
//-----------------------------------------------------------------------------
// Create the main acceleration structure that holds all instances of the scene.
// Similarly to the bottom-level AS generation, it is done in 3 steps: gathering
// the instances, computing the memory requirements for the AS, and building the AS itself
void D3D12HelloTriangle::CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances, // pair of bottom level AS and matrix of the instance
	bool updateOnly) {
	if (!updateOnly)
	{
		// Gather all the instances into the builder helper 
		for (size_t i = 0; i < instances.size(); i++)
		{
			m_topLevelASGenerator.AddInstance(instances[i].first.Get(), instances[i].second, static_cast<UINT>(i),
				// Hit group id refers to the order in which we added Hit Groups to SBT
				static_cast<UINT>(NumRayTypes * i)); //2 is for 2 shaders - hit and shadow hit
		}
		// As for the bottom-level AS, the building the AS requires some scratch space 
		// to store temporary data in addition to the actual AS. In the case of the 
		// top-level AS, the instance descriptors also need to be stored in GPU 
		// memory. This call outputs the memory requirements for each (scratch, 
		// results, instance descriptors) so that the application can allocate the 
		// corresponding memory 
		UINT64 scratchSize, resultSize, instanceDescsSize;
		m_topLevelASGenerator.ComputeASBufferSizes(m_device.Get(), true, &scratchSize, &resultSize, &instanceDescsSize);
		// Create the scratch and result buffers. Since the build is all done on GPU, 
		// those can be allocated on the default heap 
		m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(m_device.Get(), scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nv_helpers_dx12::kDefaultHeapProps);
		m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer(m_device.Get(), resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nv_helpers_dx12::kDefaultHeapProps);
		// The buffer describing the instances: ID, shader binding information, 
		// matrices ... Those will be copied into the buffer by the helper through 
		// mapping, so the buffer has to be allocated on the upload heap. 
		m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(m_device.Get(), instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
		
	}
	// After all the buffers are allocated, or if only an update is required, we 
		// can build the acceleration structure. Note that in the case of the update 
		// we also pass the existing AS as the 'previous' AS, so that it can be 
		// refitted in place.
	m_topLevelASGenerator.Generate(m_commandList.Get(), m_topLevelASBuffers.pScratch.Get(), m_topLevelASBuffers.pResult.Get(), m_topLevelASBuffers.pInstanceDesc.Get());
	
}
//-----------------------------------------------------------------------------
// Combine the BLAS and TLAS builds to construct the entire acceleration
// structure required to raytrace the scene
void D3D12HelloTriangle::CreateAccelerationStructures() { 
	
	// BLAS for model
	AccelerationStructureBuffers modelBottomLevelBuffers = CreateBottomLevelAS(m_modelVertexAndNum, m_modelIndexAndNum);
	//AccelerationStructureBuffers modelBottomLevelBuffers = CreateBottomLevelAS({ m_modelVertexAndNum2 }, { m_modelIndexAndNum2 });
	m_instances = { {modelBottomLevelBuffers.pResult, XMMatrixIdentity()}, {modelBottomLevelBuffers.pResult, XMMatrixTranslation(-.6f, 0, 1.f)}, {modelBottomLevelBuffers.pResult, XMMatrixTranslation(.6f, 0, -1.f)},
		{modelBottomLevelBuffers.pResult, XMMatrixTranslation(0, 0, 0)}, {modelBottomLevelBuffers.pResult, XMMatrixIdentity()} };
	CreateTopLevelAS(m_instances);
	m_TlasHeapIndex = nv_helpers_dx12::CreateBufferView(m_device.Get(), nullptr, m_topLevelASBuffers.pResult->GetGPUVirtualAddress(),
		m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::AS);
	// Flush the command list and wait for it to finish 
	m_commandList->Close(); 
	ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()}; 
	m_commandQueue->ExecuteCommandLists(1, ppCommandLists); 
	m_fenceValue++; 
	m_commandQueue->Signal(m_fence.Get(), m_fenceValue); 
	m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent); 
	WaitForSingleObject(m_fenceEvent, INFINITE); 
	// Once the command list is finished executing, reset it to be reused for rendering 
	ThrowIfFailed( m_commandList->Reset(m_commandAllocator.Get(), nullptr));
	// Store the AS buffers. The rest of the buffers will be released once we exit the function 
	m_bottomLevelAS = modelBottomLevelBuffers.pResult;

}
//-----------------------------------------------------------------------------
// Camera----------------------------------------------------------------------
void D3D12HelloTriangle::CreateCameraBuffer() {
	uint32_t nbMatrix = 4; // view, perspective, viewInv, perspectiveInv 
	m_cameraBufferSize = nbMatrix * sizeof(XMMATRIX); // discuss, how we can work with GLM instead of DXMath
	// Create the constant buffer for all matrices 
	m_cameraBuffer = nv_helpers_dx12::CreateBuffer( m_device.Get(), m_cameraBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps); 
	// Create a descriptor heap that will be used by the rasterization shaders 
	//m_constHeap = nv_helpers_dx12::CreateDescriptorHeap( m_device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true); //In our case REMOVE 
	// Describe and create the constant buffer view. 
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {}; 
	cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress(); 
	cbvDesc.SizeInBytes = m_cameraBufferSize; 
	
	m_camHeapIndex = nv_helpers_dx12::CreateBufferView(m_device.Get(), m_cameraBuffer.Get(), m_cameraBuffer->GetGPUVirtualAddress(),
		m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::CBV);
}
void D3D12HelloTriangle::UpdateCameraBuffer() {
	std::vector<XMMATRIX> matrices(4);

	// Initialize the view matrix, ideally this should be based on user
	// interactions The lookat and perspective matrices used for rasterization are
	// defined to transform world-space vertices into a [0,1]x[0,1]x[0,1] camera
	// space
	const glm::mat4& mat = nv_helpers_dx12::CameraManip.getMatrix();
	memcpy(&matrices[0].r->m128_f32[0], glm::value_ptr(mat), 16 * sizeof(float));

	float fovAngleY = 45.0f * XM_PI / 180.0f;
	matrices[1] =
		XMMatrixPerspectiveFovRH(fovAngleY, m_aspectRatio, 0.1f, 1000.0f);

	// Raytracing has to do the contrary of rasterization: rays are defined in
	// camera space, and are transformed into world space. To do this, we need to
	// store the inverse matrices as well.
	XMVECTOR det;
	matrices[2] = XMMatrixInverse(&det, matrices[0]);
	matrices[3] = XMMatrixInverse(&det, matrices[1]);

	// Copy the matrix contents
	uint8_t* pData;
	ThrowIfFailed(m_cameraBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, matrices.data(), m_cameraBufferSize);
	m_cameraBuffer->Unmap(0, nullptr);
}

//--------------------------------------------------------------------------------------------------
void D3D12HelloTriangle::OnButtonDown(UINT32 lParam) {
	nv_helpers_dx12::CameraManip.setMousePosition(-GET_X_LPARAM(lParam),
		-GET_Y_LPARAM(lParam));
}

//--------------------------------------------------------------------------------------------------
void D3D12HelloTriangle::OnMouseMove(UINT8 wParam, UINT32 lParam) {
	using nv_helpers_dx12::Manipulator;
	Manipulator::Inputs inputs;
	inputs.lmb = wParam & MK_LBUTTON;
	inputs.mmb = wParam & MK_MBUTTON;
	inputs.rmb = wParam & MK_RBUTTON;
	if (!inputs.lmb && !inputs.rmb && !inputs.mmb)
		return; // no mouse button pressed

	inputs.ctrl = GetAsyncKeyState(VK_CONTROL);
	inputs.shift = GetAsyncKeyState(VK_SHIFT);
	inputs.alt = GetAsyncKeyState(VK_MENU);

	CameraManip.mouseMove(-GET_X_LPARAM(lParam), -GET_Y_LPARAM(lParam), inputs);
}
 //------------------------------------------------------------------------------
 void D3D12HelloTriangle::CreatePerInstanceConstantBuffers()
 { // Due to HLSL packing rules, we create the CB with 9 float4 (each needs to start on a 16-byte // boundary) 
	 XMVECTOR bufferData[] = { 
		 // A 
		 XMVECTOR{1.0f, 0.0f, 0.0f, 1.0f}, 
		 XMVECTOR{1.0f, 0.4f, 0.0f, 1.0f}, 
		 XMVECTOR{1.f, 0.7f, 0.0f, 1.0f}, 
		 // B 
		 XMVECTOR{0.0f, 1.0f, 0.0f, 1.0f},
		 XMVECTOR{0.0f, 1.0f, 0.4f, 1.0f}, 
		 XMVECTOR{0.0f, 1.0f, 0.7f, 1.0f}, 
		 // C 
		 XMVECTOR{0.0f, 0.0f, 1.0f, 1.0f}, 
		 XMVECTOR{0.4f, 0.0f, 1.0f, 1.0f}, 
		 XMVECTOR{0.7f, 0.0f, 1.0f, 1.0f},
		 // D 
		 XMVECTOR{0.4f, 0.0f, 0.0f, 1.0f},
		 XMVECTOR{0.1f, 0.2f, 0.0f, 1.0f},
		 XMVECTOR{0.7f, 0.1f, 0.0f, 1.0f}, };
	 m_perInstanceConstantBuffers.resize(4); 
	 int i = 0; 
	 for (auto& cb : m_perInstanceConstantBuffers) 
	 { 
		 const uint32_t bufferSize = sizeof(XMVECTOR) * 3; 
		 cb = nv_helpers_dx12::CreateBuffer(m_device.Get(), bufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps); 
		 uint8_t* pData; 
		 ThrowIfFailed(cb->Map(0, nullptr, (void**)&pData)); 
		 memcpy(pData, &bufferData[i * 3], bufferSize); 
		 cb->Unmap(0, nullptr); 
		 ++i; 
	 }
 }
 uint32_t D3D12HelloTriangle::GetCBV_SRV_UAVDescriptorIndex(ID3D12Resource* resource, ID3D12DescriptorHeap* heap) const

 {
	 return static_cast<uint32_t>((resource->GetGPUVirtualAddress() - heap->GetGPUDescriptorHandleForHeapStart().ptr) / m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
 }
 void D3D12HelloTriangle::LoadModel(const std::string& name)
 {
	 tinygltf::TinyGLTF context;
	 std::string error;
	 std::string warning;
	 if (!context.LoadASCIIFromFile(&m_TestModel, &error, &warning, name)) {
		 if (!error.empty()) {
			 printf("ERROR!\n");
		 }
		 if (!warning.empty()) {
			 printf( "WARNING!\n");
		 }
		 printf("Couldn't find file. (%s)", name.c_str());
	 }
	 else {
		 printf("SUCCESS!\n");
	 }
	 for (auto& mesh : m_TestModel.meshes) {
		 for (auto& prim : mesh.primitives) {
			 ComPtr<ID3D12Resource> newVBuffer;
			 m_modelVertexAndNum.push_back({ newVBuffer, 0 });
			 m_modelVBVs.push_back(D3D12_VERTEX_BUFFER_VIEW());

			 ComPtr<ID3D12Resource> newIBuffer;
			 m_modelIndexAndNum.push_back({ newIBuffer, 0 });
			 m_modelIBVs.push_back(D3D12_INDEX_BUFFER_VIEW());

			 const tinygltf::Accessor& vertexAccessor = m_TestModel.accessors[prim.attributes.at("POSITION")];
			 const tinygltf::Accessor& indexAccessor = m_TestModel.accessors[prim.indices];

			 const tinygltf::BufferView& vertexBufferView = m_TestModel.bufferViews[vertexAccessor.bufferView];
			 const tinygltf::BufferView& indexBufferView = m_TestModel.bufferViews[indexAccessor.bufferView];

			 // Calculate the size of the vertex and index data.
			 UINT vertexDataSize = vertexAccessor.count * vertexAccessor.ByteStride(vertexBufferView);
			 UINT indexDataSize = indexAccessor.count * indexAccessor.ByteStride(indexBufferView);
			 /*
			 vertexDataSize = static_cast<UINT>(vertexAccessor.count * sizeof(Vertex));
			 indexDataSize = static_cast<UINT>(indexAccessor.count * sizeof(UINT));
			 */

			 const float* vertexData = reinterpret_cast<const float*>(&m_TestModel.buffers[vertexBufferView.buffer].data[vertexBufferView.byteOffset + vertexAccessor.byteOffset]);
			 //MAKE SIZE CHECKING FOR DATA TYPE
			 const UINT* indexData = reinterpret_cast<const UINT*>(&m_TestModel.buffers[indexBufferView.buffer].data[indexBufferView.byteOffset + indexAccessor.byteOffset]);

			 
			 m_modelVertexAndNum.back().second = vertexAccessor.count;
			 m_modelIndexAndNum.back().second = indexAccessor.count;
			 ThrowIfFailed(m_device->CreateCommittedResource(
				 &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				 D3D12_HEAP_FLAG_NONE,
				 &CD3DX12_RESOURCE_DESC::Buffer(vertexDataSize),
				 D3D12_RESOURCE_STATE_GENERIC_READ,
				 nullptr,
				 IID_PPV_ARGS(&m_modelVertexAndNum.back().first)));
			 // Copy the triangle data to the vertex buffer.
			 UINT8* pVertexDataBegin;
			 CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
			 ThrowIfFailed(m_modelVertexAndNum.back().first->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
			 memcpy(pVertexDataBegin, vertexData, vertexDataSize);
			 m_modelVertexAndNum.back().first->Unmap(0, nullptr);
			 // Initialize the vertex buffer view.
			 m_modelVBVs.back().BufferLocation = m_modelVertexAndNum.back().first->GetGPUVirtualAddress();
			 m_modelVBVs.back().StrideInBytes = sizeof(Vertex);
			 m_modelVBVs.back().SizeInBytes = vertexDataSize;

			 // Indices
			 ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				 D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(indexDataSize),
				 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_modelIndexAndNum.back().first)));
			 // Copy the triangle data to the index buffer.
			 UINT8* pIndexDataBegin;
			 ThrowIfFailed(m_modelIndexAndNum.back().first->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
			 memcpy(pIndexDataBegin, indexData, indexDataSize);
			 m_modelIndexAndNum.back().first->Unmap(0, nullptr);
			 // Initialize the index buffer view.
			 m_modelIBVs.back().BufferLocation = m_modelIndexAndNum.back().first->GetGPUVirtualAddress();
			 m_modelIBVs.back().Format = DXGI_FORMAT_R32_UINT;
			 m_modelIBVs.back().SizeInBytes = indexDataSize;
		 }
	 }
 }
 void D3D12HelloTriangle::LoadModel2(const std::string& name)
 {
	 tinygltf::TinyGLTF context;
	 std::string error;
	 std::string warning;
	 if (!context.LoadASCIIFromFile(&m_TestModel2, &error, &warning, name)) {
		 if (!error.empty()) {
			 printf("ERROR!\n");
		 }
		 if (!warning.empty()) {
			 printf("WARNING!\n");
		 }
		 printf("Couldn't find file. (%s)", name.c_str());
	 }
	 else {
		 printf("SUCCESS!\n");
	 }

	 ComPtr<ID3D12Resource> newVBuffer;
	 m_modelVertexAndNum2 = { newVBuffer, 0 };
	 m_modelVBV2 = D3D12_VERTEX_BUFFER_VIEW();


	 ComPtr<ID3D12Resource> newIBuffer;
	 m_modelIBV2 = D3D12_INDEX_BUFFER_VIEW();
	 m_modelIndexAndNum2 = { newIBuffer, 0 };

	 std::vector<float> allVertexData = {};
	 std::vector<UINT> allIndexData = {};

	 UINT allVertexDataSize = 0;
	 UINT allIndexDataSize = 0;
	 for (auto& mesh : m_TestModel2.meshes) {
		 for (auto& prim : mesh.primitives) {

				 const tinygltf::Accessor& vertexAccessor = m_TestModel2.accessors[prim.attributes.at("POSITION")];
				 const tinygltf::Accessor& indexAccessor = m_TestModel2.accessors[prim.indices];

				 const tinygltf::BufferView& vertexBufferView = m_TestModel2.bufferViews[vertexAccessor.bufferView];
				 const tinygltf::BufferView& indexBufferView = m_TestModel2.bufferViews[indexAccessor.bufferView];

				 // Calculate the size of the vertex and index data.
				 UINT vertexDataSize = vertexAccessor.count * vertexAccessor.ByteStride(vertexBufferView);
				 UINT indexDataSize = indexAccessor.count * indexAccessor.ByteStride(indexBufferView);
				 

		 //vertexDataSize = static_cast<UINT>(vertexAccessor.count * sizeof(Vertex));
		 //indexDataSize = static_cast<UINT>(indexAccessor.count * sizeof(UINT));
				 

				 float* vertexData = reinterpret_cast<float*>(&m_TestModel2.buffers[vertexBufferView.buffer].data[vertexBufferView.byteOffset + vertexAccessor.byteOffset]);
				 //MAKE SIZE CHECKING FOR DATA TYPE
				 UINT* indexData = reinterpret_cast<UINT*>(&m_TestModel2.buffers[indexBufferView.buffer].data[indexBufferView.byteOffset + indexAccessor.byteOffset]);

				 
				 for (int i = 0; i < vertexAccessor.count; i++) {
					 allVertexData.push_back(vertexData[i]);
				 }
				 for (int i = 0; i < indexAccessor.count; i++) {
					 allIndexData.push_back(indexData[i] + m_modelVertexAndNum2.second);
				 }

				 m_modelVertexAndNum2.second += vertexAccessor.count;
				 m_modelIndexAndNum2.second += indexAccessor.count;
				 allVertexDataSize += vertexDataSize;
				 allIndexDataSize += indexDataSize;
			 
		 }
	 }

	 ThrowIfFailed(m_device->CreateCommittedResource(
		 &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		 D3D12_HEAP_FLAG_NONE,
		 &CD3DX12_RESOURCE_DESC::Buffer(allVertexDataSize),
		 D3D12_RESOURCE_STATE_GENERIC_READ,
		 nullptr,
		 IID_PPV_ARGS(&m_modelVertexAndNum2.first)));
	 // Copy the triangle data to the vertex buffer.
	 UINT8* pVertexDataBegin;
	 CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
	 ThrowIfFailed(m_modelVertexAndNum2.first->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
	 memcpy(pVertexDataBegin, &allVertexData[0], allVertexDataSize);
	 m_modelVertexAndNum2.first->Unmap(0, nullptr);
	 // Initialize the vertex buffer view.
	 m_modelVBV2.BufferLocation = m_modelVertexAndNum2.first->GetGPUVirtualAddress();
	 m_modelVBV2.StrideInBytes = sizeof(Vertex);
	 m_modelVBV2.SizeInBytes = allVertexDataSize;

	 // Indices
	 ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		 D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(allIndexDataSize),
		 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_modelIndexAndNum2.first)));
	 // Copy the triangle data to the index buffer.
	 UINT8* pIndexDataBegin;
	 ThrowIfFailed(m_modelIndexAndNum2.first->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
	 memcpy(pIndexDataBegin, &allIndexData[0], allIndexDataSize);
	 m_modelIndexAndNum2.first->Unmap(0, nullptr);
	 // Initialize the index buffer view.
	 m_modelIBV2.BufferLocation = m_modelIndexAndNum2.first->GetGPUVirtualAddress();
	 m_modelIBV2.Format = DXGI_FORMAT_R32_UINT;
	 m_modelIBV2.SizeInBytes = allIndexDataSize;
 }