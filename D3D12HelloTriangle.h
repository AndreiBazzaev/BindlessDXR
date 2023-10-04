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
#include "Scene.h"
#include "ResourceManagerImprov.h"
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
	// Will need to be public to call from Model.cpp and Scene.cpp
	void LoadModelRecursive(const std::string& name, Model* model);
	void UploadScene(Scene* scene);
private:
	Model* LoadModelFromClass(ResourceManager* resManager, const std::string& name, std::vector<std::string>& hitGroups);

	// ------REMOVE - GAMEPLAY CALL SIMULATION------
	void MakeTestScene();
	Scene m_myScene;
	void MakeTestScene1();
	int m_currentScene = 0;
	int m_requestedScene = 0;
	Scene m_myScene1;
	ResourceManager m_resourceManager;

	void SwitchScenes();
	// ------------------------------------------
	static const UINT FrameCount = 2;
	struct Normal
	{
		XMFLOAT3 dir;
	};
	struct Vertex
	{
		XMFLOAT3 position;
	};
	// ---- New Model Loading------

	struct MaterialStruct {
		UINT hasNormals;
		UINT hasTangents;
		UINT hasColors;
		UINT hasTexcoords;

		UINT alphaMode; // 0 - "OPAQUE", 1 - "MASK", 2 - "BLEND"
		FLOAT alphaCutoff; // default 0.5
		UINT doubleSided;

		INT baseTextureIndex; // Index in heap
		INT baseTextureSamplerIndex; // Index in heap
		UINT texCoordIdBase;
		XMFLOAT4 baseColor;

		INT metallicRoughnessTextureIndex;
		INT metallicRoughnessTextureSamplerIndex;
		UINT texCoordIdMR;
		FLOAT metallicFactor;
		FLOAT roughnessFactor;


		INT occlusionTextureIndex;
		INT occlusionTextureSamplerIndex;
		UINT texCoordIdOcclusion;
		FLOAT strengthOcclusion; // look into tiny_gltf.h for formula

		INT normalTextureIndex;
		INT normalTextureSamplerIndex;
		UINT texCoordIdNorm;
		FLOAT scaleNormal; // look into tiny_gltf.h for formula

		INT emissiveTextureIndex;
		INT emissiveTextureSamplerIndex;
		UINT texCoordIdEmiss;
		XMFLOAT3 emisiveFactor;

	};
	void FillInfoPBR(tinygltf::Model& model, tinygltf::Primitive& prim, MaterialStruct* material, std::vector<uint32_t>& imageHeapIds);
	void LoadImageData(tinygltf::Model& model, std::vector<uint32_t>& imageHeapIds);
	uint32_t m_renderMode = 0;
	uint32_t m_numRenderModes = 10;
	// For now render modes
	// 0 - vertex colors
	// 1 - vertex normals
	// 2 - base color
	// 3 - metallic 
	// 4 - roughness
	// 5 - metallic roughness
	// 6 - occlusion
	// 7 - orm
	// 8 - normal map 
	// 9 - emissive
	//----------------------------
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
	void PopulateCommandList();
	void WaitForPreviousFrame();

	// We need to know if we can run RTX #RTX
	void CheckRaytracingSupport();
	// Used to switch between RTX and raster
	virtual void OnKeyUp(UINT8 key); 
	// #RTX structure for storing AS buffers
	struct AccelerationStructureBuffers
	{
		ComPtr<ID3D12Resource> pScratch; // Scratch memory for AS builder 
		ComPtr<ID3D12Resource> pResult; // Where the AS is 
		ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
	};
	std::vector<std::tuple<ComPtr<ID3D12Resource>, DirectX::XMMATRIX, UINT>> m_instances; // Stores BLASes  with the corresponding transforms and number of Hit groups

	AccelerationStructureBuffers
		CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers,
							std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers = {},
	std::vector<ComPtr<ID3D12Resource>> vTransformBuffers = {});
	// ---------     TLAS   ----------------------------------------
	/// Create the main acceleration structure that holds all instances of the scene
	/// param instances : tuple of BLAS, transform in world and hit group number
	void CreateTopLevelAS(const std::vector<std::tuple<ComPtr<ID3D12Resource>, DirectX::XMMATRIX, UINT>>& instances, bool updateOnly = false);
	void ReCreateAccelerationStructures();
	nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator; // Helper to create TLAS
	AccelerationStructureBuffers m_topLevelASBuffers;
	uint32_t m_TlasHeapIndex;
	bool m_TlasFirstBuild = true;
	//--------------------------------------------------------------
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

	ComPtr<ID3D12DescriptorHeap> m_SamplerHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SamplerHandle;
	uint32_t m_SamplerIndex = 0;

	void CreatHeaps();
	// Create all possible variations of GLTF texture samplers
	void FillInSamplerHeap();
	uint32_t GetSamplerHeapIndexFromGLTF(tinygltf::Sampler& sampler);
	// ---------RESOURCES FOR SHADER PASS------------------------------
	void CreateRaytracingOutputBuffer();
	ComPtr<ID3D12Resource> m_outputResource; // similar to rtv in #RTX. Shaders write to this buffer
	uint32_t m_RTOutputHeapIndex;
	// ---------SBT for connectring Shaders and resources together-----
	// SBT is the CORE of the DXR, uniting the whole setup
	void ReCreateShaderBindingTable(Scene* scene);
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
	uint32_t m_time;
	//----------------------------------------
	// #RTX SHADOWS
	ComPtr<IDxcBlob> m_shadowLibrary;
	ComPtr<ID3D12RootSignature> m_shadowSignature;
	// MODEL LOADING
	void BuildModelRecursive(tinygltf::Model& model, Model* modelData, uint64_t nodeIndex, XMMATRIX parentMat, std::vector <ComPtr<ID3D12Resource >>& transforms,
		std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& modelVertexAndNum, std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& modelIndexAndNum,
		std::vector<uint32_t>& primitiveIndexes, std::vector<uint32_t>& imageHeapIds);
	XMMATRIX GlmToXM_mat4(glm::mat4 gmat);
	// Bindless
	std::vector<uint32_t> m_AllHeapIndices;
	ComPtr<ID3D12Resource> m_HeapIndexBuffer;
};
