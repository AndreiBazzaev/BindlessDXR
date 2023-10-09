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
#ifndef ROUND_UP
#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))
#endif
#include <glm/gtx/transform.hpp>
#include <iostream>
#include <locale>
#include <codecvt>
#include "stb_image/stb_image.h"
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
	// #RTX---------------------------------------------------------------
	// Check the raytracing capabilities of the device 
	CheckRaytracingSupport();
	CreatHeaps();
	FillInSamplerHeap();
	// Allocate the buffer for output #RTX image
	CreateRaytracingOutputBuffer();
	//ThrowIfFailed(m_commandList->Close());
	CreateRaytracingPipeline();

	//-----COMPUTE INIT------
	CreateMipMapPSO();
	//----------------------
	// Camera
	nv_helpers_dx12::CameraManip.setWindowSize(GetWidth(), GetHeight());
	nv_helpers_dx12::CameraManip.setLookat(glm::vec3(1.5f, 1.5f, 1.5f), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	CreateCameraBuffer();
	//--------------------------------------------------------------------

	MakeTestScene();

}
// --------------ROOT SIGNATURES----------------------------
// Create #RTX RAYGEN Root Signature (empty, as we use bindless)
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateRayGenSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	return rsc.Generate(m_device.Get(), true);
}
// Create #RTX  HIT root signture (is empty for now, as we use bindless)
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateHitSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	return rsc.Generate(m_device.Get(), true);
}
// Create #RTX  MISS root signture (is empty for now)
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateMissSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc; 
	return rsc.Generate(m_device.Get(), true);
}
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateGlobalSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	// List of all heap indexes
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0, 1);
	// Render mode
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, 0, 1);
	return rsc.Generate(m_device.Get(), false);
}

ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateMipMapSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	// Source
	D3D12_DESCRIPTOR_RANGE rangeSRV{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0,};
	// Dest
	D3D12_DESCRIPTOR_RANGE rangeUAV{ D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0, };
	rsc.AddHeapRangesParameter({ rangeSRV, rangeUAV });
	
	return rsc.Generate(m_device.Get(), true);
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
	m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Assets/Shaders/RayGen.hlsl");
	m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Assets/Shaders/Miss.hlsl");
	m_hitLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Assets/Shaders/Hit.hlsl");

	// SHADING-----------------------
	m_shadowLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Assets/Shaders/ShadowRay.hlsl");
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
	pipeline.AddLibrary(m_hitLibrary.Get(), { L"ClosestHit", L"ShadedClosestHit"});

	// To be used, each DX12 shader needs a root signature defining which
	// parameters and buffers will be accessed.
	m_rayGenSignature = CreateRayGenSignature();
	m_missSignature = CreateMissSignature();
	m_hitSignature = CreateHitSignature();


	// Hit group for the triangles, with a shader simply interpolating vertex
	// colors
	pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");
	pipeline.AddHitGroup(L"ShadedHitGroup", L"ShadedClosestHit");
	pipeline.AddHitGroup(L"ShadowHitGroup", L"ShadowClosestHit");
	// The following section associates the root signature to each shader. Note
 // that we can explicitly show that some shaders share the same root signature
 // (eg. Miss and ShadowMiss). Note that the hit shaders are now only referred
 // to as hit groups, meaning that the underlying intersection, any-hit and
 // closest-hit shaders share the same root signature.
	pipeline.AddRootSignatureAssociation(m_rayGenSignature.Get(), { L"RayGen" });
	pipeline.AddRootSignatureAssociation(m_shadowSignature.Get(),
		{ L"ShadowHitGroup" });
	pipeline.AddRootSignatureAssociation(m_missSignature.Get(),
		{ L"Miss", L"ShadowMiss" });
	pipeline.AddRootSignatureAssociation(m_hitSignature.Get(),
		{ L"HitGroup", L"ShadedHitGroup" });



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
// ----------Other PSOs-----------------------------------
void D3D12HelloTriangle::CreateMipMapPSO() {
	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	m_MipMapRootSignature = CreateMipMapSignature();
	psoDesc.pRootSignature = m_MipMapRootSignature.Get();
	ComPtr<ID3DBlob> computeShader = nv_helpers_dx12::CompileShader(L"Assets\\ComputeShaders\\CreateMip.hlsl", nullptr, "main", "cs_5_1");
	psoDesc.CS = { computeShader->GetBufferPointer(), computeShader->GetBufferSize() };
	m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_MipMapPSO));
}
// --------- RT Output - buffer from which we copy data to the Render Target ----
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
// --------- Create CBV SRV UAV heap
void D3D12HelloTriangle::CreatHeaps() { 
	m_CbvSrvUavHeap = nv_helpers_dx12::CreateDescriptorHeap( m_device.Get(), 65536, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true); 
	m_CbvSrvUavHandle = m_CbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();

	m_SamplerHeap = nv_helpers_dx12::CreateDescriptorHeap(m_device.Get(), 1000, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true);
	m_SamplerHandle = m_SamplerHeap->GetCPUDescriptorHandleForHeapStart();
}
// --------- For a particular scene recreate an SBT - shader + resource bindings for each BLAS
void D3D12HelloTriangle::ReCreateShaderBindingTable(Scene* scene) {
	
	m_sbtHelper.Reset();
	// MAKE THESE SHADER DATA RETRIEVED FROM SCENE
	m_sbtHelper.AddRayGenerationProgram(L"RayGen", {  });
	m_sbtHelper.AddMissProgram(L"Miss", {});
	m_sbtHelper.AddMissProgram(L"ShadowMiss", {});

	for (int i = 0; i < scene->m_sceneObjects.size(); i++) {
		for (auto& hitGroup : scene->m_sceneObjects[i].m_model->m_hitGroups) {
			std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			const std::wstring hitName = converter.from_bytes(hitGroup);
			m_sbtHelper.AddHitGroup(hitName, {});
		}
	}
	uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();
	m_sbtStorage = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	if (!m_sbtStorage) {
		throw std::logic_error("Could not allocate the shader binding table");
	}
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
// Sets the scene to the proper one after the button callback
void D3D12HelloTriangle::SwitchScenes() {
	if (m_currentScene != m_requestedScene) {
		ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));
		m_currentScene = m_requestedScene;

		switch (m_currentScene) {
		case(0):
			MakeTestScene();
			break;
		case(1):
			MakeTestScene1();
			break;
		}
		WaitForPreviousFrame();
	}
}
// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
	// Switches the active scene if this is required
	SwitchScenes();

	UpdateCameraBuffer();

	// ANIMATE 
	m_time++;
	std::get<1>(m_instances[0]) = XMMatrixScaling(1.0003f, 1.0003f, 1.0003f) * XMMatrixRotationAxis({ 0.f, 1.f, 0.f }, static_cast<float>(m_time) / 50.0f) * XMMatrixTranslation(1.f, 0.0f * cosf(m_time / 20.f), -1.f);
	//std::get<1>(m_instances[1]) = XMMatrixScaling(0.04f, 0.04f, 0.04f) * XMMatrixRotationAxis({ 0.f, 0.f, 1.f }, static_cast<float>(m_time) / 50.0f) * XMMatrixTranslation(0.f, 0.1f * cosf(m_time / 20.f), 1.f);
	std::get<1>(m_instances[2]) = XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixRotationAxis({ 0.f, -1.f, 0.f }, static_cast<float>(m_time) / 50.0f) * XMMatrixTranslation(-1.f, 0.1f * cosf(m_time / 20.f) + 0.5f, 0.f);
	/*
	std::get<1>(m_instances[3]) = XMMatrixScaling(0.003f, 0.00001f, 0.003f) * XMMatrixTranslation(0.f, - 1.f, 0.f);
	std::get<1>(m_instances[4]) = XMMatrixScaling(0.0006f, 0.0006f, 0.0006f);*/
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

// Switches Active Scenes
void D3D12HelloTriangle::OnKeyUp(UINT8 key)
{ 
	if (key == VK_SPACE) {
		m_requestedScene += 1;
		m_requestedScene = m_requestedScene % 2;
	}
	if (key == VK_LEFT) {
		m_renderMode -= 1;
		if (m_renderMode < 0)
			m_renderMode = m_numRenderModes - 1;
	}
	if (key == VK_RIGHT) {
		m_renderMode += 1;
		if (m_renderMode >= m_numRenderModes)
			m_renderMode = 0;
	}
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
	
	std::vector<ID3D12DescriptorHeap*> heaps = { m_CbvSrvUavHeap.Get(), m_SamplerHeap.Get()};
	m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

	// Set global RS
	m_commandList->SetComputeRootSignature(m_globalSignature.Get());

	// RT output should be writable in sahders
	CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	m_commandList->ResourceBarrier(1, &transition);
	D3D12_DISPATCH_RAYS_DESC desc = {};
	
	// All rayGen sahders first
	uint32_t rayGenerationSectionSizeInBytes = m_sbtHelper.GetRayGenSectionSize();
	desc.RayGenerationShaderRecord.StartAddress = m_sbtStorage->GetGPUVirtualAddress();
	desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;
	
	// All miss shaedrs - second
	uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
	desc.MissShaderTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
	desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
	desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();
	
	// All hit groups - third
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

	// Heap indexes for Bindless rendering
	m_commandList->SetComputeRootShaderResourceView(0, m_HeapIndexBuffer.Get()->GetGPUVirtualAddress());
	m_commandList->SetComputeRoot32BitConstant(1, m_renderMode, 0);
	// ----------DRAWING ------------------------------------------
	// Dispatch the rays and write to the raytracing output
	m_commandList->DispatchRays(&desc);
	//-----------COPY OUTPUT TO RTV + RETURN STATES----------------
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
D3D12HelloTriangle::AccelerationStructureBuffers
D3D12HelloTriangle::CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers,
										std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers,
										std::vector<ComPtr<ID3D12Resource>> vTransformBuffers) {

	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS; 
	// Adding all vertex buffers and not transforming their position for now
	for (size_t i = 0; i < vVertexBuffers.size(); i++) {
		if (vTransformBuffers.size() > 0) {
			if (i < vIndexBuffers.size() && vIndexBuffers[i].second > 0)
				bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0, vVertexBuffers[i].second, sizeof(Vertex), vIndexBuffers[i].first.Get(), 0, vIndexBuffers[i].second, vTransformBuffers[i].Get(), 0, true);
			else
				bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0, vVertexBuffers[i].second, sizeof(Vertex), 0, 0, 0, vTransformBuffers[i].Get(), 0, true);
		}
		else {
			if (i < vIndexBuffers.size() && vIndexBuffers[i].second > 0)
				bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0, vVertexBuffers[i].second, sizeof(Vertex), vIndexBuffers[i].first.Get(), 0, vIndexBuffers[i].second, nullptr, 0, true);
			else
				bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0, vVertexBuffers[i].second, sizeof(Vertex), 0, 0, 0);

		}
	}
	UINT64 scratchSizeInBytes = 0; 
	UINT64 resultSizeInBytes = 0; 
	bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes, &resultSizeInBytes); 
	AccelerationStructureBuffers buffers; 
	buffers.pScratch = nv_helpers_dx12::CreateBuffer( m_device.Get(), scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, nv_helpers_dx12::kDefaultHeapProps); 
	buffers.pResult = nv_helpers_dx12::CreateBuffer( m_device.Get(), resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nv_helpers_dx12::kDefaultHeapProps); 
	bottomLevelAS.Generate(m_commandList.Get(), buffers.pScratch.Get(), buffers.pResult.Get(), false, nullptr); 
	return buffers;
}
// tuple of bottom level AS,  matrix of the instance and number of hit groups
void D3D12HelloTriangle::CreateTopLevelAS(const std::vector<std::tuple<ComPtr<ID3D12Resource>, DirectX::XMMATRIX, UINT>>& instances, 
	bool updateOnly) {
	if (!updateOnly)
	{
		// Gather all the instances into the builder helper 
		for (size_t i = 0; i < instances.size(); i++)
		{
			m_topLevelASGenerator.AddInstance(std::get<0>(instances[i]).Get(), std::get<1>(instances[i]), static_cast<UINT>(i),
				// Hit group id refers to the order in which we added Hit Groups to SBT
				static_cast<UINT>(std::get<2>(instances[i]) * i)); //2 is for 2 shaders - hit and shadow hit
		}
		UINT64 scratchSize, resultSize, instanceDescsSize;
		m_topLevelASGenerator.ComputeASBufferSizes(m_device.Get(), true, &scratchSize, &resultSize, &instanceDescsSize);
		m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(m_device.Get(), scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, nv_helpers_dx12::kDefaultHeapProps);
		m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer(m_device.Get(), resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nv_helpers_dx12::kDefaultHeapProps);
		m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(m_device.Get(), instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
		
	}
	// After all the buffers are allocated, or if only an update is required, we 
		// can build the acceleration structure. Note that in the case of the update 
		// we also pass the existing AS as the 'previous' AS, so that it can be 
		// refitted in place.
	m_topLevelASGenerator.Generate(m_commandList.Get(), m_topLevelASBuffers.pScratch.Get(), m_topLevelASBuffers.pResult.Get(), m_topLevelASBuffers.pInstanceDesc.Get());
	
}
void D3D12HelloTriangle::ReCreateAccelerationStructures() {

	CreateTopLevelAS(m_instances);
	// First we create a resource with a heap pointer
	if (m_TlasFirstBuild) {
		m_TlasHeapIndex = nv_helpers_dx12::CreateBufferView(m_device.Get(), nullptr, m_topLevelASBuffers.pResult->GetGPUVirtualAddress(),
			m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::AS);
		m_TlasFirstBuild = true;	
	}
	// Then we can just use the same heap pointer but rebuild the resource
	else {
		nv_helpers_dx12::ChangeASResourceLoaction(m_device.Get(), m_topLevelASBuffers.pResult->GetGPUVirtualAddress(), m_CbvSrvUavHeap.Get(),
			m_TlasHeapIndex);
	}

	m_commandList->Close();
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
	m_fenceValue++;
	m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
	m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
	WaitForSingleObject(m_fenceEvent, INFINITE);
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));
}
// Camera----REWRITE FOR A PROPER CAM--------------------------------------------------------------
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
 // -------------Loads GLTF from file, launches recursion and stores BLAS
 void D3D12HelloTriangle::LoadModelRecursive(const std::string& name, Model* model)
 {
	 tinygltf::TinyGLTF context;
	 std::string error;
	 std::string warning;
	 tinygltf::Model m_TestModel;
	 if (!context.LoadASCIIFromFile(&m_TestModel, &error, &warning, name)) {
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
	 // Data for BLAS creation
	 std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> modelVertexAndNum;
	 std::vector <std::pair<ComPtr<ID3D12Resource>, uint32_t>> modelIndexAndNum;
	 std::vector <ComPtr<ID3D12Resource >> transforms;
	 std::vector<uint32_t> primitiveIndexes = { 0 };
	 std::vector<uint32_t> imageIndexes;

	 // The first data for the model - its primitive indexes buffer
	  // Update Primitive Buffer according to the new data
	 ComPtr<ID3D12Resource> primBuffer;
	 CD3DX12_RANGE readRange(0, 0);
	 primBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), sizeof(uint32_t) * primitiveIndexes.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	 UINT8* pPrimiBegin;
	 ThrowIfFailed(primBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pPrimiBegin)));
	 memcpy(pPrimiBegin, &primitiveIndexes[0], sizeof(uint32_t) * primitiveIndexes.size());
	 primBuffer->Unmap(0, nullptr);
	 // -------------Primitive Heap Upload------------------------
	 model->m_heapPointer = nv_helpers_dx12::CreateBufferView(m_device.Get(), primBuffer.Get(),primBuffer->GetGPUVirtualAddress(),
		 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::SRV_BUFFER, sizeof(uint32_t));
	 primitiveIndexes.clear();
	 //----------------------------------------------------
	 // ---------------Load Images To Heap--------------------
	 LoadImageData(m_TestModel, imageIndexes);
	 // ---------------Upload model data to GPU
	 auto& scene = m_TestModel.scenes[m_TestModel.defaultScene];
	 for (size_t i = 0; i < scene.nodes.size(); i++) {
		 BuildModelRecursive(m_TestModel, model, scene.nodes[i], XMMatrixIdentity(), transforms, modelVertexAndNum, modelIndexAndNum, primitiveIndexes, imageIndexes);
	 }
	 
	 // --------Update Primitive Buffer according to the new data
	 ComPtr<ID3D12Resource> newPrimBuffer;
	 newPrimBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), sizeof(uint32_t) * primitiveIndexes.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	 UINT8* pNewPrimiBegin;
	 ThrowIfFailed(newPrimBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pNewPrimiBegin)));
	 memcpy(pNewPrimiBegin, &primitiveIndexes[0], sizeof(uint32_t) * primitiveIndexes.size());
	 newPrimBuffer->Unmap(0, nullptr);
	 
	 // ---------------Heap Data Update------------------------
	 nv_helpers_dx12::ChangeSRVResourceLoaction(m_device.Get(), newPrimBuffer.Get(), m_CbvSrvUavHeap.Get(), model->m_heapPointer, sizeof(uint32_t));

	 AccelerationStructureBuffers AS = CreateBottomLevelAS(modelVertexAndNum, modelIndexAndNum, transforms);
	 ComPtr<ID3D12Resource> m_modelBLASBuffer = AS.pResult;
	 model->m_BlasPointer = reinterpret_cast<UINT64>(m_modelBLASBuffer.Get());
 }


 void D3D12HelloTriangle::BuildModelRecursive(tinygltf::Model& model, Model* modelData, uint64_t nodeIndex, XMMATRIX parentMat, std::vector <ComPtr<ID3D12Resource >>& transforms,
	 std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& modelVertexAndNum, std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& modelIndexAndNum, 
	 std::vector<uint32_t>& primitiveIndexes, std::vector<uint32_t>& imageHeapIds) {
	 HRESULT hr = S_OK;
	 // get the needed node
	 auto& glTFNode = model.nodes[nodeIndex];
	 XMMATRIX modelSpaceTrans = parentMat;
	 bool hasMesh = glTFNode.mesh >= 0;
	 ComPtr<ID3D12Resource> transBuffer;
	 // Build Matrix
	 {
		 // Build a constant buffer for node transform matrix
		 transBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), sizeof(XMMATRIX), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

		 // if GLTF node has a pre-specified matrix, use it
		 if (glTFNode.matrix.size() == 16) {
			 // transform GLTF vector with 16 values to GLM mat4
			 XMMATRIX nodeMat = nv_helpers_dx12::ConvertGLTFMatrixToXMMATRIX(&glTFNode.matrix[0]);
			 // apply parent-child matrix transform
			 modelSpaceTrans = parentMat * nodeMat;
		 }
		 else {
			 XMMATRIX trMat = XMMatrixIdentity();
			 XMMATRIX rotMat = XMMatrixIdentity();
			 XMMATRIX scMat = XMMatrixIdentity();
			 // There is no madel matrix
			 // Assume Trans x Rotate x Scale order
			 if (glTFNode.translation.size() == 3) {
				 trMat = XMMatrixTranslation(glTFNode.translation[0], glTFNode.translation[1], glTFNode.translation[2]);
			 }
			 if (glTFNode.rotation.size() == 4) {
				 DirectX::XMVECTOR quaternion = DirectX::XMVectorSet(glTFNode.rotation[0], glTFNode.rotation[1], glTFNode.rotation[2], glTFNode.rotation[3]);
				 rotMat = DirectX::XMMatrixRotationQuaternion(DirectX::XMQuaternionNormalize(quaternion));
			 }
			 if (glTFNode.scale.size() == 3) {
				 scMat = XMMatrixScaling(glTFNode.scale[0], glTFNode.scale[1], glTFNode.scale[2]);
			 }
			 modelSpaceTrans = scMat * rotMat * trMat * parentMat;
		 }
		 uint8_t* pData;
		 ThrowIfFailed(transBuffer->Map(0, nullptr, (void**)&pData));
		 memcpy(pData, &modelSpaceTrans, sizeof(XMMATRIX));
		 transBuffer->Unmap(0, nullptr);
	 }
	 // Build Primitive data
	 {
		 if (hasMesh) {
			 auto& mesh = model.meshes[glTFNode.mesh];
			 for (auto& prim : mesh.primitives) {

				 transforms.push_back(transBuffer);

				 ComPtr<ID3D12Resource> newVBuffer;
				 modelVertexAndNum.push_back({ newVBuffer, 0 });

				 ComPtr<ID3D12Resource> newIBuffer;
				 modelIndexAndNum.push_back({ newIBuffer, 0 });

				 const tinygltf::Accessor& vertexAccessor = model.accessors[prim.attributes.at("POSITION")];
				 const tinygltf::Accessor& indexAccessor = model.accessors[prim.indices];

				 const tinygltf::BufferView& vertexBufferView = model.bufferViews[vertexAccessor.bufferView];
				 const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];


				 UINT vertexDataSize = vertexAccessor.count * vertexAccessor.ByteStride(vertexBufferView);
				 UINT indexDataSize = indexAccessor.count * sizeof(UINT);

				 const float* vertexData = reinterpret_cast<const float*>(&model.buffers[vertexBufferView.buffer].data[vertexBufferView.byteOffset + vertexAccessor.byteOffset]);

				 std::vector<UINT> indexData;
				 switch (indexAccessor.componentType) {
					 case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					 {
						 const UINT8* indexLoadData8 = reinterpret_cast<const UINT8*>(&model.buffers[indexBufferView.buffer].data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
						 for (int i = 0; i < indexAccessor.count; i++) {
							 UINT id = indexLoadData8[i];
							 indexData.push_back(id);
						 }
					 }
					 break;
					 case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					 {
						 const UINT16* indexLoadData16 = reinterpret_cast<const UINT16*>(&model.buffers[indexBufferView.buffer].data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
						 for (int i = 0; i < indexAccessor.count; i++) {
							 UINT id = indexLoadData16[i];
							 indexData.push_back(id);
						 }
					 }
					 break;
					 case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					 {
						 const UINT32* indexLoadData32 = reinterpret_cast<UINT32*>(&model.buffers[indexBufferView.buffer].data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
						 for (int i = 0; i < indexAccessor.count; i++) {
							 UINT id = indexLoadData32[i];
							 indexData.push_back(id);
						 }
					 }
					 break;
				 }
				 modelVertexAndNum.back().second = vertexAccessor.count;
				 modelIndexAndNum.back().second = indexAccessor.count;

				 modelVertexAndNum.back().first = nv_helpers_dx12::CreateBuffer(m_device.Get(), vertexDataSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
				 UINT8* pVertexDataBegin;
				 CD3DX12_RANGE readRange(0, 0);
				 ThrowIfFailed(modelVertexAndNum.back().first->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
				 memcpy(pVertexDataBegin, vertexData, vertexDataSize);
				 modelVertexAndNum.back().first->Unmap(0, nullptr);


				 modelIndexAndNum.back().first = nv_helpers_dx12::CreateBuffer(m_device.Get(), indexDataSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
				 // Copy the triangle data to the index buffer.
				 UINT8* pIndexDataBegin;
				 ThrowIfFailed(modelIndexAndNum.back().first->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
				 memcpy(pIndexDataBegin, &indexData[0], indexDataSize);
				 modelIndexAndNum.back().first->Unmap(0, nullptr);


				
				 /*
				 -------Primitive in heap---------
				 Material
				 Transform
				 Positions
				 Normals  (optional)
				 Tangents (optional)
				 Colors   (optional)
				 TexCoords (optional)
				 Indexes
				 */
				
				 //
				 MaterialStruct primMat;
				 // Fill in and Upload material data
				 {
					 // Check which model data we have
					 {
						 // -----------------Normals --------------------------
						 if (prim.attributes.find("NORMAL") != prim.attributes.end())
							 primMat.hasNormals = 1;
						 else
							 primMat.hasNormals = 0;
						 // -----------------Tangents --------------------------
						 if (prim.attributes.find("TANGENT") != prim.attributes.end())
							 primMat.hasTangents = 1;
						 else
							 primMat.hasTangents = 0;
						 // -----------------Colors --------------------------
						 if (prim.attributes.find("COLOR_0") != prim.attributes.end())
							 primMat.hasColors = 1;
						 else
							 primMat.hasColors = 0;
						 // -----------------Texcoords --------------------------
						 // we will cover a wide range of texture coords
						 primMat.hasTexcoords = 0;
						 for (int i = 0; i < 10; i++) {
							 std::string name = "TEXCOORD_" + std::to_string(i);
							 if (prim.attributes.find(name.c_str()) != prim.attributes.end())
								 primMat.hasTexcoords += 1;
						 }
					 }
					 FillInfoPBR(model, prim, &primMat, imageHeapIds);
					 //----------------Create material Buffer + Push to Heap-----------------------
					 {
						 ComPtr<ID3D12Resource> newMatBuffer;
						 newMatBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), sizeof(MaterialStruct), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
						 UINT8* pMaterialBegin;
						 ThrowIfFailed(newMatBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMaterialBegin)));
						 memcpy(pMaterialBegin, &primMat, sizeof(MaterialStruct));
						 newMatBuffer->Unmap(0, nullptr);
						 // ---------------Heap Upload------------------------
						 // WE START PRIMITIVE DATA IN A HEAP FROM MATERIAL OF THE FIRST PRIMITIVE
						 primitiveIndexes.push_back(
							 nv_helpers_dx12::CreateBufferView(m_device.Get(), newMatBuffer.Get(), newMatBuffer->GetGPUVirtualAddress(),
								 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::SRV_BUFFER, sizeof(MaterialStruct)));
					 }
				 }
				 // Upload Transform to Heap
				 nv_helpers_dx12::CreateBufferView(m_device.Get(), transBuffer.Get(), transBuffer->GetGPUVirtualAddress(),
					 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::SRV_BUFFER, sizeof(XMMATRIX));
				 // Upload Positions to Heap
				 nv_helpers_dx12::CreateBufferView(m_device.Get(), modelVertexAndNum.back().first.Get(), modelVertexAndNum.back().first->GetGPUVirtualAddress(),
					 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::SRV_BUFFER, sizeof(XMFLOAT3));
				 // Fill in and Upload to Heap arbitrary Vertex data
				 {
					 // -----------------Normals --------------------------
					 if (primMat.hasNormals == 1) {
						 ComPtr<ID3D12Resource> newNormalBuffer;
						 const tinygltf::Accessor& normalAccessor = model.accessors[prim.attributes.at("NORMAL")];
						 const tinygltf::BufferView& normalBufferView = model.bufferViews[normalAccessor.bufferView];
						 UINT normalDataSize = normalAccessor.count * normalAccessor.ByteStride(normalBufferView);
						 const float* normalData = reinterpret_cast<const float*>(&model.buffers[normalBufferView.buffer].data[normalBufferView.byteOffset + normalAccessor.byteOffset]);

						 newNormalBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), normalDataSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
						 UINT8* pNormalBegin;
						 ThrowIfFailed(newNormalBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pNormalBegin)));
						 memcpy(pNormalBegin, normalData, normalDataSize);
						 newNormalBuffer->Unmap(0, nullptr);
						 // --------Upload to Heap-----------
						 nv_helpers_dx12::CreateBufferView(m_device.Get(), newNormalBuffer.Get(), newNormalBuffer->GetGPUVirtualAddress(),
							 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::SRV_BUFFER, sizeof(XMFLOAT3));
					 }
					 //------------------------------------------------------

					 // -----------------Tangents --------------------------
					 if (primMat.hasTangents == 1) {
						 ComPtr<ID3D12Resource> newTangentBuffer;
						 const tinygltf::Accessor& tangentAccessor = model.accessors[prim.attributes.at("TANGENT")];
						 const tinygltf::BufferView& tangentBufferView = model.bufferViews[tangentAccessor.bufferView];
						 UINT tangentDataSize = tangentAccessor.count * tangentAccessor.ByteStride(tangentBufferView);
						 const float* tangentData = reinterpret_cast<const float*>(&model.buffers[tangentBufferView.buffer].data[tangentBufferView.byteOffset + tangentAccessor.byteOffset]);

						 newTangentBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), tangentDataSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
						 UINT8* pTangentBegin;
						 ThrowIfFailed(newTangentBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pTangentBegin)));
						 memcpy(pTangentBegin, tangentData, tangentDataSize);
						 newTangentBuffer->Unmap(0, nullptr);
						 // --------Upload to Heap-----------
						 nv_helpers_dx12::CreateBufferView(m_device.Get(), newTangentBuffer.Get(), newTangentBuffer->GetGPUVirtualAddress(),
							 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::SRV_BUFFER, sizeof(XMFLOAT4));
					 }
					 //------------------------------------------------------

					 // -----------------Colors --------------------------
					 if (primMat.hasColors == 1) {
						 ComPtr<ID3D12Resource> newColorBuffer;
						 const tinygltf::Accessor& colorAccessor = model.accessors[prim.attributes.at("COLOR_0")];
						 const tinygltf::BufferView& colorBufferView = model.bufferViews[colorAccessor.bufferView];
						 UINT colorDataSize = colorAccessor.count * colorAccessor.ByteStride(colorBufferView);
						 const float* colorData = reinterpret_cast<const float*>(&model.buffers[colorBufferView.buffer].data[colorBufferView.byteOffset + colorAccessor.byteOffset]);

						 newColorBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), colorDataSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
						 UINT8* pColorBegin;
						 ThrowIfFailed(newColorBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pColorBegin)));
						 memcpy(pColorBegin, colorData, colorDataSize);
						 newColorBuffer->Unmap(0, nullptr);
						 // --------Upload to Heap-----------
						 nv_helpers_dx12::CreateBufferView(m_device.Get(), newColorBuffer.Get(), newColorBuffer->GetGPUVirtualAddress(),
							 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::SRV_BUFFER, sizeof(XMFLOAT4));
					 }
					 //------------------------------------------------------

					 // -----------------Texcoords --------------------------
					 for (int i = 0; i < primMat.hasTexcoords; i++) {
						 ComPtr<ID3D12Resource> newTexcoordBuffer;
						 std::string name = "TEXCOORD_" + std::to_string(i);
						 const tinygltf::Accessor& texcoordAccessor = model.accessors[prim.attributes.at(name.c_str())];
						 const tinygltf::BufferView& texcoordBufferView = model.bufferViews[texcoordAccessor.bufferView];
						 UINT texcoordDataSize = texcoordAccessor.count * texcoordAccessor.ByteStride(texcoordBufferView);
						 const float* texcoordData = reinterpret_cast<const float*>(&model.buffers[texcoordBufferView.buffer].data[texcoordBufferView.byteOffset + texcoordAccessor.byteOffset]);

						 newTexcoordBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), texcoordDataSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
						 UINT8* pTexcoordBegin;
						 ThrowIfFailed(newTexcoordBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pTexcoordBegin)));
						 memcpy(pTexcoordBegin, texcoordData, texcoordDataSize);
						 newTexcoordBuffer->Unmap(0, nullptr);
						 // --------Upload to Heap-----------
						 nv_helpers_dx12::CreateBufferView(m_device.Get(), newTexcoordBuffer.Get(), newTexcoordBuffer->GetGPUVirtualAddress(),
							 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::SRV_BUFFER, sizeof(XMFLOAT2));
					 }
					 //------------------------------------------------------
				 }
				 //----------------Indices
				 nv_helpers_dx12::CreateBufferView(m_device.Get(), modelIndexAndNum.back().first.Get(), modelIndexAndNum.back().first->GetGPUVirtualAddress(),
					 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::SRV_BUFFER, sizeof(UINT));
			 }
		 }

	 }

	 // continue with node's children (we pass paren's model matrix to get the correct transform for children)
	 for (size_t i = 0; i < glTFNode.children.size(); i++) {
		 BuildModelRecursive(model, modelData, glTFNode.children[i], modelSpaceTrans, transforms, modelVertexAndNum, modelIndexAndNum, primitiveIndexes, imageHeapIds);
	 }
 }
 
 void D3D12HelloTriangle::LoadImageData(tinygltf::Model& model, std::vector<uint32_t>& imageHeapIds) {
	 for (auto& image : model.images) {
		 ComPtr<ID3D12Resource> texture;
		 // check format - we later should be able to know if it is SRBB or not, idk how
		 DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
		 switch (image.component) {
			 case(1):
				 if (image.bits == 8)
					 format = DXGI_FORMAT_R8_UNORM;
				 if (image.bits == 16)
					 format = DXGI_FORMAT_R16_UNORM;
				 if (image.bits == 32)
					 format = DXGI_FORMAT_R32_UINT;
				 break;
			 case(2):
				 if (image.bits == 8)
					 format = DXGI_FORMAT_R8G8_UNORM;
				 if (image.bits == 16)
					 format = DXGI_FORMAT_R16G16_UNORM;
				 if (image.bits == 32)
					 format = DXGI_FORMAT_R32G32_UINT;
				 break;
			 case(3):
				 if (image.bits == 32)
					 format = DXGI_FORMAT_R32G32B32_UINT;
				 break;
			 case(4):
				 if (image.bits == 8)
					 format = DXGI_FORMAT_R8G8B8A8_UNORM;
				 if (image.bits == 16)
					 format = DXGI_FORMAT_R16G16B16A16_UNORM;
				 if (image.bits == 32)
					 format = DXGI_FORMAT_R32G32B32A32_UINT;
				 break;
		 }
		 //uint32_t imageSize = image.width * image.height * image.component * image.bits / 8;
		 uint32_t mipsNum = 1;// +glm::log2(glm::max(image.width, image.height));
		 texture = nv_helpers_dx12::CreateTextureBuffer(m_device.Get(), image.width, image.height, mipsNum, format, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, nv_helpers_dx12::kDefaultHeapProps);

		 // questionable if I need an upload buffer here
		 // COPY TEXTURE BUFFER
		 {
			 // create a footprint  for the texture to know its properties
			 D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
			 UINT rowCount;
			 UINT64 rowSize;
			 UINT64 size;
			 m_device->GetCopyableFootprints(&texture->GetDesc(), 0, mipsNum, 0,
				 &footprint, &rowCount, &rowSize, &size);
			 ComPtr<ID3D12Resource> textureUploader;
			 textureUploader = nv_helpers_dx12::CreateBuffer(m_device.Get(), size, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
			 UINT8* pTextureDataBegin;
			 CD3DX12_RANGE readRange(0, 0);
			 ThrowIfFailed(textureUploader->Map(0, &readRange, reinterpret_cast<void**>(&pTextureDataBegin)));

			 int properRowPitch = rowSize;
			 if (properRowPitch <= 256)
				 properRowPitch = 256;
			 else if (properRowPitch % 256 != 0)
				 properRowPitch = properRowPitch + (256 - properRowPitch % 256);

			 for (UINT i = 0; i != rowCount; ++i) {
				 memcpy(static_cast<uint8_t*>(pTextureDataBegin) + properRowPitch * i,
					 &image.image[0] + image.width * image.component * i,
					 image.width * image.component);
			 }
			 D3D12_TEXTURE_COPY_LOCATION dstCopyLocation = {};
			 dstCopyLocation.pResource = texture.Get();
			 dstCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			 dstCopyLocation.SubresourceIndex = 0;

			 D3D12_TEXTURE_COPY_LOCATION srcCopyLocation = {};
			 srcCopyLocation.pResource = textureUploader.Get();
			 srcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			 srcCopyLocation.PlacedFootprint = footprint;

			 m_commandList->CopyTextureRegion(&dstCopyLocation, 0, 0, 0,
				 &srcCopyLocation, nullptr);
			 textureUploader->Unmap(0, nullptr);

			 /*
			 UINT subresourceIndex = D3D12CalcSubresource(mipLevel, arraySlice, numMips); 
			 D3D12_SUBRESOURCE_DATA textureData = {}; 
			 textureData.pData = // pointer to your data ; 
			 textureData.RowPitch = // row pitch of your data ; 
			 textureData.SlicePitch =// size of your data ; 
			 // Use UpdateSubresources or similar method to update the specific mip  level
			 UpdateSubresources(deviceContext, textureResource, stagingResource, subresourceIndex, 0, 1, &textureData);
			 */
		 }
		 imageHeapIds.push_back(nv_helpers_dx12::CreateBufferView(m_device.Get(), texture.Get(), NULL,
			 m_CbvSrvUavHandle, m_CbvSrvUavIndex, nv_helpers_dx12::TEXTURE));
	 }
 }
 void D3D12HelloTriangle::FillInfoPBR(tinygltf::Model& model, tinygltf::Primitive& prim, MaterialStruct* material, std::vector<uint32_t>& imageHeapIds) {

	 tinygltf::Material materialGLTF = model.materials[prim.material];

	 material->alphaCutoff = FLOAT(materialGLTF.alphaCutoff);
	 if (materialGLTF.alphaMode == "OPAQUE")
		 material->alphaMode = 0;
	 if (materialGLTF.alphaMode == "MASK")
		 material->alphaMode = 1;
	 if (materialGLTF.alphaMode == "BLEND")
		 material->alphaMode = 2;
	 material->doubleSided = UINT(materialGLTF.doubleSided);

	 // TEXTURE DATA
	 //----BASE COLOR----------------
	 {
		 // get GLTF index
		 int textureIndexGLTF = materialGLTF.pbrMetallicRoughness.baseColorTexture.index;
		 if (textureIndexGLTF >= 0) {
			 // make it into heap index
			 material->baseTextureIndex = imageHeapIds[model.textures[textureIndexGLTF].source];
			 // get GLTF index
			 material->baseTextureSamplerIndex = model.textures[textureIndexGLTF].sampler;
			 // make it heap index
			 material->baseTextureSamplerIndex = GetSamplerHeapIndexFromGLTF(model.samplers[material->baseTextureSamplerIndex]);
		 }
		 else {
			 // ADD DEFAULT TEXTURE LOADING => HERE ASSIGN DEFAULT TEXTUES
			 material->baseTextureIndex = -1;
			 material->baseTextureSamplerIndex = -1;
		 }
		 material->texCoordIdBase = materialGLTF.pbrMetallicRoughness.baseColorTexture.texCoord;
		 material->baseColor = XMFLOAT4{
		 FLOAT(materialGLTF.pbrMetallicRoughness.baseColorFactor[0]),
		 FLOAT(materialGLTF.pbrMetallicRoughness.baseColorFactor[1]),
		 FLOAT(materialGLTF.pbrMetallicRoughness.baseColorFactor[2]),
		 FLOAT(materialGLTF.pbrMetallicRoughness.baseColorFactor[3]) };
	 }
	 //---Metallic Roughness---------------------
	 {
		 // get GLTF index
		 int textureIndexGLTF = materialGLTF.pbrMetallicRoughness.metallicRoughnessTexture.index;
		 if (textureIndexGLTF >= 0) {
			 // make it into heap index
			 material->metallicRoughnessTextureIndex = imageHeapIds[model.textures[textureIndexGLTF].source];
			 // get GLTF index
			 material->metallicRoughnessTextureSamplerIndex = model.textures[textureIndexGLTF].sampler;
			 // make it heap index
			 material->metallicRoughnessTextureSamplerIndex = GetSamplerHeapIndexFromGLTF(model.samplers[material->metallicRoughnessTextureSamplerIndex]);
		 }
		 else {
			 // ADD DEFAULT TEXTURE LOADING => HERE ASSIGN DEFAULT TEXTUES
			 material->metallicRoughnessTextureIndex = -1;
			 material->metallicRoughnessTextureSamplerIndex = -1;
		 }
		 material->texCoordIdMR = materialGLTF.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;
		 material->metallicFactor = FLOAT(materialGLTF.pbrMetallicRoughness.metallicFactor);
		 material->roughnessFactor = FLOAT(materialGLTF.pbrMetallicRoughness.roughnessFactor);
	 }

	//----OCCLUSION------------------------------
	 {
		 // get GLTF index
		 int textureIndexGLTF = materialGLTF.occlusionTexture.index;
		 if (textureIndexGLTF >= 0) {
			 // make it into heap index
			 material->occlusionTextureIndex = imageHeapIds[model.textures[textureIndexGLTF].source];
			 // get GLTF index
			 material->occlusionTextureSamplerIndex = model.textures[textureIndexGLTF].sampler;
			 // make it heap index
			 material->occlusionTextureSamplerIndex = GetSamplerHeapIndexFromGLTF(model.samplers[material->occlusionTextureSamplerIndex]);
		 }
		 else {
			 // ADD DEFAULT TEXTURE LOADING => HERE ASSIGN DEFAULT TEXTUES
			 material->occlusionTextureIndex = -1;
			 material->occlusionTextureSamplerIndex = -1;
		 }
		 material->texCoordIdOcclusion = materialGLTF.occlusionTexture.texCoord;
		 material->strengthOcclusion = FLOAT(materialGLTF.occlusionTexture.strength);
	 }

	 //----NORMAL-----------------------------
	 {
		 // get GLTF index
		 int textureIndexGLTF = materialGLTF.normalTexture.index;
		 if (textureIndexGLTF >= 0) {
			 // make it into heap index
			 material->normalTextureIndex = imageHeapIds[model.textures[textureIndexGLTF].source];
			 // get GLTF index
			 material->normalTextureSamplerIndex = model.textures[textureIndexGLTF].sampler;
			 // make it heap index
			 material->normalTextureSamplerIndex = GetSamplerHeapIndexFromGLTF(model.samplers[material->normalTextureSamplerIndex]);
		 }
		 else {
			 // ADD DEFAULT TEXTURE LOADING => HERE ASSIGN DEFAULT TEXTUES
			 material->normalTextureIndex = -1;
			 material->normalTextureSamplerIndex = -1;
		 }
		 material->texCoordIdNorm = materialGLTF.normalTexture.texCoord;
		 material->scaleNormal = FLOAT(materialGLTF.normalTexture.scale);
	 }

	 //---EMISSIVE-------------------
	 {
		 // get GLTF index
		 int textureIndexGLTF = materialGLTF.emissiveTexture.index;
		 if (textureIndexGLTF >= 0) {
			 // make it into heap index
			 material->emissiveTextureIndex = imageHeapIds[model.textures[textureIndexGLTF].source];
			 // get GLTF index
			 material->emissiveTextureSamplerIndex = model.textures[textureIndexGLTF].sampler;
			 // make it heap index
			 material->emissiveTextureSamplerIndex = GetSamplerHeapIndexFromGLTF(model.samplers[material->emissiveTextureSamplerIndex]);
		 }
		 else {
			 // ADD DEFAULT TEXTURE LOADING => HERE ASSIGN DEFAULT TEXTUES
			 material->emissiveTextureIndex = -1;
			 material->emissiveTextureSamplerIndex = -1;
		 }
		 material->texCoordIdEmiss = materialGLTF.emissiveTexture.texCoord;
		 material->emisiveFactor = XMFLOAT3{
			 FLOAT(materialGLTF.emissiveFactor[0]),
			 FLOAT(materialGLTF.emissiveFactor[1]),
			 FLOAT(materialGLTF.emissiveFactor[2])
		 };
	 }
	 
 }
 // Move to model.cpp?
 Model* D3D12HelloTriangle::LoadModelFromClass(ResourceManager* resManager, const std::string& name, std::vector<std::string>& hitGroups)
 {
	 Model model;
	 model.m_name = name;
	 if (resManager->GetModel(name) == nullptr) {
		LoadModelRecursive(model.m_name, &model);
		 for (auto& hitGroup : hitGroups) {
			 model.m_hitGroups.push_back(hitGroup);
		 }
		 resManager->RegisterModel(model.m_name, model);
	 }
	// DISCUSSION - THIS WAY WE WON'T BE ABLE TO HAVE DIFFERENT SHADER GROUPS FOR THE SAME MODELS
	// DO WE WANT THIS? WHEN WILL WE USE THIS?
	return resManager->GetModel(name);
 }
 // Move to scene.cpp?
 void D3D12HelloTriangle::UploadScene(Scene* scene)
 {
	 // Sync with model data uploading
	 m_commandList->Close();
	 ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	 m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
	 m_fenceValue++;
	 m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
	 m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
	 WaitForSingleObject(m_fenceEvent, INFINITE);
	 ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));
	 // Clear
	 m_instances.clear();
	 m_AllHeapIndices.clear();
	 m_topLevelASGenerator.ClearInstances();

	 // -----------------------------------
	 // FILL in Model Data
	 for (int i = 0; i < scene->m_sceneObjects.size(); i++) {

		 ComPtr<ID3D12Resource> BlasResource = reinterpret_cast<ID3D12Resource*>(scene->m_sceneObjects[i].m_model->m_BlasPointer);
		 m_instances.push_back({ BlasResource, GlmToXM_mat4(scene->m_sceneObjects[i].m_transform), scene->m_sceneObjects[i].m_model->m_hitGroups.size() });
	 }
	 // Update TLAS
	 ReCreateAccelerationStructures();
	 // Update SBT
	 ReCreateShaderBindingTable(scene);

	 // Fill in indexes, used for any set of models
	 m_AllHeapIndices.push_back(m_RTOutputHeapIndex);
	 m_AllHeapIndices.push_back(m_TlasHeapIndex);
	 m_AllHeapIndices.push_back(m_camHeapIndex);
	 // Fill in model indexes
	 for (int i = 0; i < scene->m_sceneObjects.size(); i++) {
		 m_AllHeapIndices.push_back(scene->m_sceneObjects[i].m_model->m_heapPointer);
	 }
	 // Upload HEAP INDEXES buffer to gpu
	 m_HeapIndexBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), sizeof(uint32_t) * m_AllHeapIndices.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	 uint32_t* pData;
	 CD3DX12_RANGE readRange(0, 0);
	 m_HeapIndexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));
	 memcpy(pData, m_AllHeapIndices.data(), sizeof(uint32_t) * m_AllHeapIndices.size()); m_HeapIndexBuffer->Unmap(0, nullptr);
	 // Close cmd list
	 ThrowIfFailed(m_commandList->Close());
 }
 // Move this to helper?
 XMMATRIX D3D12HelloTriangle::GlmToXM_mat4(glm::mat4 gmat) {
	 XMMATRIX xmat;
	 const glm::mat4& mat = gmat;
	 memcpy(&xmat.r->m128_f32[0], glm::value_ptr(mat), 16 * sizeof(float));
	 return xmat;
 }

 void D3D12HelloTriangle::FillInSamplerHeap() {

	 D3D12_SAMPLER_DESC samplerDesc = {};
	 for (int i = 0; i < 12; i++) {
		 switch (i) {
			 case (0):
				 samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
				 break;
			 case(1):
				 samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
				 break;
			 case(2):
				 samplerDesc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
				 break;
			 case(3):
				 samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
				 break;
			 case(4):
				 samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
				 break;
			 case(5):
				 samplerDesc.Filter = D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
				 break;
			 case(6):
				 samplerDesc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
				 break;
			 case(7):
				 samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
				 break;
			 case(8):
				 samplerDesc.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
				 break;
			 case(9):
				 samplerDesc.Filter = D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
				 break;
			 case(10):
				 samplerDesc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
				 break;
			 case(11):
				 samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
				 break;
		 }
		 for (int j = 0; j < 3; j++) {
			 switch (j) {
				 case (0):
					 samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
					 break;
				 case(1):
					 samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
					 break;
				 case(2):
					 samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
					 break;
			 }
			 for (int w = 0; w < 3; w++) {
				 switch (w) {
				 case (0):
					 samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
					 break;
				 case(1):
					 samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
					 break;
				 case(2):
					 samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
					 break;
				 }
				 samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
				 samplerDesc.MaxLOD = 256;

				 D3D12_SAMPLER_DESC buildSamplerDesc = samplerDesc;
				 m_device->CreateSampler(&buildSamplerDesc, m_SamplerHandle);
				 m_SamplerHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				 m_SamplerIndex++;
			 }
		 }
	 }
 }
 uint32_t D3D12HelloTriangle::GetSamplerHeapIndexFromGLTF(tinygltf::Sampler& sampler) {
	 D3D12_SAMPLER_DESC samplerDesc = {};
	 // setup filters for the sampler based on the samplers m_From the GLTF model
	 int filterMultiplier = 0;
	 int adressUMultiplier = 0;
	 int adressVMultiplier = 0;
	 switch (sampler.minFilter) {
	 case TINYGLTF_TEXTURE_FILTER_NEAREST:
		 if (sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
			 filterMultiplier = 0;
		 else
			 filterMultiplier = 1;
		 break;
	 case TINYGLTF_TEXTURE_FILTER_LINEAR:
		 if (sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
			 filterMultiplier = 2;
		 else
			 filterMultiplier = 3;
		 break;
	 case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
		 if (sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
			 filterMultiplier = 4;
		 else
			 filterMultiplier = 5;
		 break;
	 case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
		 if (sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
			 filterMultiplier = 6;
		 else
			 filterMultiplier = 7;
		 break;
	 case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
		 if (sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
			 filterMultiplier = 8;
		 else
			 filterMultiplier = 9;
		 break;
	 case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
		 if (sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
			 filterMultiplier = 10;
		 else
			 filterMultiplier = 11;
		 break;
	 default:
		 filterMultiplier = 0;
		 break;
	 }
	 
	switch (sampler.wrapS) {
		 case TINYGLTF_TEXTURE_WRAP_REPEAT:
			 adressUMultiplier = 0;
			 break;
		 case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
			 adressUMultiplier = 1;
			 break;
		 case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
			 adressUMultiplier = 2;
			 break;
	}
	switch (sampler.wrapT) {
	case TINYGLTF_TEXTURE_WRAP_REPEAT:
		adressVMultiplier = 0;
		break;
	case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
		adressVMultiplier = 1;
		break;
	case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
		adressVMultiplier = 2;
		break;
	}
	return filterMultiplier * 9 + adressUMultiplier * 3 + adressVMultiplier; // Mimicking the way we've put them in the heap
 }


 //void D3D12HelloTriangle::GenerateMips(uint32_t textureHeapIndex) {
	// // Setup resources
	// m_commandList->SetComputeRootSignature(m_MipMapRootSignature.Get());
	// m_commandList->SetPipelineState(m_MipMapPSO.Get());
	// D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_CbvSrvUavHeap.Get()->GetGPUDescriptorHandleForHeapStart();
	// srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
	// 
	// // Create UAV
	// ComPtr<ID3D12Resource> textureUAV = nv_helpers_dx12::CreateTextureBuffer(m_device.Get(), image.width, image.height, mipsNum, format, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, nv_helpers_dx12::kDefaultHeapProps);

	// m_commandList->SetComputeRootDescriptorTable(1, threshholdedTexture->m_UAVHandleGPU);

	// const float threadGroupSize = 8.0;
	// // Calculate the number of thread groups needed to cover the texture
	// int numGroupsX = int(ceil(float(threshholdedTexture->m_Width) / threadGroupSize));
	// int numGroupsY = int(ceil(float(threshholdedTexture->m_Height) / threadGroupSize));



	// // Dispatch the shader with the calculated number of thread groups
	// cmdList->Dispatch(numGroupsX, numGroupsY, 1);
 //}

 // Gameplay code simulation------------------------------
 void D3D12HelloTriangle::MakeTestScene()
 {
	
	 GameObject a, b, c;
	 a.m_model = LoadModelFromClass(&m_resourceManager, "Assets/cars2/scene.gltf", std::vector<std::string>{ "ShadedHitGroup", "ShadowHitGroup" });
	 b.m_model = LoadModelFromClass(&m_resourceManager, "Assets/Sponza/Sponza.gltf", std::vector<std::string>{ "ShadedHitGroup", "ShadowHitGroup" });
	 c.m_model = LoadModelFromClass(&m_resourceManager, "Assets/Helmet/DamagedHelmet.gltf", std::vector<std::string>{ "ShadedHitGroup", "ShadowHitGroup" });
	// b.m_model = LoadModelFromClass(&m_resourceManager, "Assets/Helmet/DamagedHelmet.gltf", std::vector<std::string>{ "HitGroup", "ShadowHitGroup" });

	 a.m_transform = glm::scale(glm::vec3(1.f));
	 b.m_transform = glm::scale(glm::vec3(0.4f)) * glm::translate(glm::vec3(0.f, 0.f, 0.f));
	 c.m_transform = glm::scale(glm::vec3(0.5f)) * glm::translate(glm::vec3(1.f, 0.f, 0.f));

	 m_myScene.m_sceneObjects.clear();
	 m_myScene.AddGameObject(a);
	 m_myScene.AddGameObject(b);
	 m_myScene.AddGameObject(c);
	 UploadScene(&m_myScene);
 }
 void D3D12HelloTriangle::MakeTestScene1()
 {
	 GameObject a, b, c, d;
	 Model am, bm, cm, dm;

	 a.m_model = LoadModelFromClass(&m_resourceManager, "Assets/car/scene.gltf", std::vector<std::string>{ "ShadedHitGroup", "ShadowHitGroup" });
	 b.m_model = LoadModelFromClass(&m_resourceManager, "Assets/Cube/Cube.gltf", std::vector<std::string>{ "ShadedHitGroup", "ShadowHitGroup" });
	 c.m_model = LoadModelFromClass(&m_resourceManager, "Assets/cars2/scene.gltf", std::vector<std::string>{ "ShadedHitGroup", "ShadowHitGroup" });
	 d.m_model = LoadModelFromClass(&m_resourceManager, "Assets/Sponza/Sponza.gltf", std::vector<std::string>{ "ShadedHitGroup", "ShadowHitGroup" });

	 a.m_transform = glm::scale(glm::vec3(0.04f));
	 b.m_transform = glm::scale(glm::vec3(0.5f)) * glm::translate(glm::vec3(2.f, 0.f, 0.f));
	 c.m_transform = glm::scale(glm::vec3(0.04f)) * glm::translate(glm::vec3(1.f, 20.f, 0.f));
	 d.m_transform = glm::scale(glm::vec3(0.04f)) * glm::translate(glm::vec3(-1.f, 0.f, 0.f));

	 m_myScene1.m_sceneObjects.clear();
	 m_myScene1.AddGameObject(a);
	 m_myScene1.AddGameObject(b);
	 m_myScene1.AddGameObject(c);
	 m_myScene1.AddGameObject(d);
	 UploadScene(&m_myScene1);
 }
 // ------------------------------------------------------