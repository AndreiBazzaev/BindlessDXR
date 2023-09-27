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

#pragma once

#include "DXSample.h"
// #RTX includes for TLAS and BLAS
#include <dxcapi.h>
#include <vector>
#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"
#include "tiny_gltf/tiny_gltf.h"
// -----------------
using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12HelloTriangle : public DXSample
{
public:
	D3D12HelloTriangle(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

private:
	static const UINT FrameCount = 2;

	struct Vertex
	{
		XMFLOAT3 position;
		//XMFLOAT4 color;
		//Vertex(XMFLOAT4 pos, XMFLOAT4 /*n*/, XMFLOAT4 col)
		//	:position(pos.x, pos.y, pos.z), color(col)
		//{}
		//Vertex(XMFLOAT3 pos, XMFLOAT4 col)
		//	:position(pos), color(col)
		//{}
		Vertex(XMFLOAT3 pos)
			:position(pos) {}
	};
	struct HeapOffsets {
		uint32_t TlasIndex;
		uint32_t camIndex;
	};
	struct VertexM
	{
		XMFLOAT3 position;
	};
	// Pipeline objects.
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;
	ComPtr<ID3D12Device5> m_device; // We need Device 5 for raytracing #RTX
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	UINT m_rtvDescriptorSize;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForPreviousFrame();

	// We need to know if we can run RTX #RTX
	void CheckRaytracingSupport();
	// Used to switch between RTX and raster
	virtual void OnKeyUp(UINT8 key); 
	bool m_raster = true;
	// #RTX structure for storing AS buffers
	struct AccelerationStructureBuffers
	{
		ComPtr<ID3D12Resource> pScratch; // Scratch memory for AS builder 
		ComPtr<ID3D12Resource> pResult; // Where the AS is 
		ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
	};
	// #RTX required setup for 1 BLAS with one object and TLAS which contains this one BLAS
	ComPtr<ID3D12Resource> m_bottomLevelAS; // Storage for the bottom Level AS
	nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator; // Helper to create TLAS
	AccelerationStructureBuffers m_topLevelASBuffers;
	uint32_t m_TlasHeapIndex;
	std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_instances; // Stores BLASes  with the corresponding transforms

	/// Create the acceleration structure of an instance
	/// \param vVertexBuffers : pair of buffer and vertex count
	/// \return AccelerationStructureBuffers for TLAS
	AccelerationStructureBuffers
		CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers,
							std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers = {});
	/// Create the main acceleration structure that holds all instances of the scene
	/// \param instances : pair of BLAS and transform
	void CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances, bool updateOnly = false);
	/// Create all acceleration structures, bottom and top
	void CreateAccelerationStructures();

	// setup for SHADING #RTX ----------------------------------------
	ComPtr<ID3D12RootSignature> CreateRayGenSignature();
	ComPtr<ID3D12RootSignature> CreateMissSignature();
	ComPtr<ID3D12RootSignature> CreateHitSignature();
	// Gloabal RS creation 
	ComPtr<ID3D12RootSignature> CreateGlobalSignature();
	//-------------------
	void CreateRaytracingPipeline(); // PSO creation
	// Shader representation for #RTX
	ComPtr<IDxcBlob> m_rayGenLibrary;
	ComPtr<IDxcBlob> m_hitLibrary;
	ComPtr<IDxcBlob> m_missLibrary;
	// ------------------ Local RS
	ComPtr<ID3D12RootSignature> m_rayGenSignature;
	ComPtr<ID3D12RootSignature> m_hitSignature;
	ComPtr<ID3D12RootSignature> m_missSignature;
	// ------------------ Global RS
	ComPtr<ID3D12RootSignature> m_globalSignature;
	// Ray tracing pipeline state
	ComPtr<ID3D12StateObject> m_rtStateObject;
	// Ray tracing pipeline state properties, retaining the shader identifiers
	// to use in the Shader Binding Table
	ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;
	//-----------------------------------------------------------------
	ComPtr<ID3D12DescriptorHeap> m_CbvSrvUavHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_CbvSrvUavHandle;
	uint32_t m_CbvSrvUavIndex = 0;
	void CreateShaderResourceHeap(); 
	// ---------RESOURCES FOR SHADER PASS------------------------------
	void CreateRaytracingOutputBuffer();
	ComPtr<ID3D12Resource> m_outputResource; // similar to rtv in #RTX. Shaders write to this buffer
	uint32_t m_RTOutputHeapIndex;
	// ---------SBT for connectring Shaders and resources together-----
	// SBT is the CORE of the DXR, uniting the whole setup
	void CreateShaderBindingTable();
	nv_helpers_dx12::ShaderBindingTableGenerator m_sbtHelper;
	ComPtr<ID3D12Resource> m_sbtStorage;
	//---------------------------------------------------------------------
	// CAMERA SETUP - can be replaced for Perry cam later
	void CreateCameraBuffer();
	void UpdateCameraBuffer();
	ComPtr< ID3D12Resource > m_cameraBuffer;
	uint32_t m_camHeapIndex;
	uint32_t m_cameraBufferSize = 0;
	// CAMERA CONTROLS
	void OnButtonDown(UINT32 lParam); 
	void OnMouseMove(UINT8 wParam, UINT32 lParam);
	
	// Create a CBs for each instance (for example materials)
	void CreatePerInstanceConstantBuffers();
	std::vector<ComPtr<ID3D12Resource>> m_perInstanceConstantBuffers;
	//----------------------------------------
	// #RTX SHADOWS
	ComPtr<IDxcBlob> m_shadowLibrary;
	ComPtr<ID3D12RootSignature> m_shadowSignature;
	// timer - REMOVE
	uint32_t m_time = 0;
	// MODEL LOADING
	void LoadModel(const std::string& name);
	tinygltf::Model m_TestModel;

	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> m_modelVertexAndNum;
	std::vector <std::pair<ComPtr<ID3D12Resource>, uint32_t>> m_modelIndexAndNum;

	std::vector < D3D12_VERTEX_BUFFER_VIEW> m_modelVBVs;
	std::vector < D3D12_INDEX_BUFFER_VIEW> m_modelIBVs;
	DXGI_FORMAT m_modelIndexFormat = DXGI_FORMAT_R32_UINT;
	// MODEL LOADING 2.0
	void LoadModel2(const std::string& name);
	tinygltf::Model m_TestModel2;

	std::pair<ComPtr<ID3D12Resource>, uint32_t> m_modelVertexAndNum2;
	std::pair<ComPtr<ID3D12Resource>, uint32_t> m_modelIndexAndNum2;

	D3D12_VERTEX_BUFFER_VIEW m_modelVBV2;
	D3D12_INDEX_BUFFER_VIEW m_modelIBV2;
	DXGI_FORMAT m_modelIndexFormat2 = DXGI_FORMAT_R32_UINT;

	// Bindless
	uint32_t GetCBV_SRV_UAVDescriptorIndex(ID3D12Resource* resource, ID3D12DescriptorHeap* heap) const;

};