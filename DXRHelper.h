/******************************************************************************
 * Copyright 1998-2018 NVIDIA Corp. All Rights Reserved.
 *****************************************************************************/

#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <d3d12.h>
#include "DXSampleHelper.h"
#include <dxcapi.h>

#include <vector>

namespace nv_helpers_dx12
{
    enum BufferType {
        CBV, SRV, UAV, AS
};
//--------------------------------------------------------------------------------------------------
//
//
inline ID3D12Resource* CreateBuffer(ID3D12Device* m_device, uint64_t size,
                                    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState,
                                    const D3D12_HEAP_PROPERTIES& heapProps)
{
  D3D12_RESOURCE_DESC bufDesc = {};
  bufDesc.Alignment = 0;
  bufDesc.DepthOrArraySize = 1;
  bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufDesc.Flags = flags;
  bufDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufDesc.Height = 1;
  bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufDesc.MipLevels = 1;
  bufDesc.SampleDesc.Count = 1;
  bufDesc.SampleDesc.Quality = 0;
  bufDesc.Width = size;

  ID3D12Resource* pBuffer;
  ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                  initState, nullptr, IID_PPV_ARGS(&pBuffer)));
  return pBuffer;
}
inline uint32_t CreateBufferView(ID3D12Device* device, ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS GpuAdress, D3D12_CPU_DESCRIPTOR_HANDLE& handleRef, uint32_t& heapIndex, BufferType type)
{
    switch (type) {
        case CBV: {
            // Describe and create a constant buffer view for the camera
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
            cbvDesc.BufferLocation = GpuAdress;
            cbvDesc.SizeInBytes = resource->GetDesc().Width;
            device->CreateConstantBufferView(&cbvDesc, handleRef);
            break;
        }
        case SRV: {
            assert("SRV VIEW NOT IMPLEMENTED!");
            break;
        }
        case UAV: {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, handleRef);
            break;
        }
        case AS: {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.RaytracingAccelerationStructure.Location = GpuAdress;
            device->CreateShaderResourceView(nullptr, &srvDesc, handleRef);
            break;
        }
    }
    handleRef.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    heapIndex += 1;
    return heapIndex - 1;
}

#ifndef ROUND_UP
#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))
#endif

// Specifies a heap used for uploading. This heap type has CPU access optimized
// for uploading to the GPU.
static const D3D12_HEAP_PROPERTIES kUploadHeapProps = {
    D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};

// Specifies the default heap. This heap type experiences the most bandwidth for
// the GPU, but cannot provide CPU access.
static const D3D12_HEAP_PROPERTIES kDefaultHeapProps = {
    D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};

//--------------------------------------------------------------------------------------------------
// Compile a HLSL file into a DXIL library
//
IDxcBlob* CompileShaderLibrary(LPCWSTR fileName)
{
  static IDxcCompiler* pCompiler = nullptr;
  static IDxcLibrary* pLibrary = nullptr;
  static IDxcIncludeHandler* dxcIncludeHandler;

  HRESULT hr;

  // Initialize the DXC compiler and compiler helper
  if (!pCompiler)
  {
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void **)&pCompiler));
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void **)&pLibrary));
    ThrowIfFailed(pLibrary->CreateIncludeHandler(&dxcIncludeHandler));
  }
  // Open and read the file
  std::ifstream shaderFile(fileName);
  if (shaderFile.good() == false)
  {
    throw std::logic_error("Cannot find shader file");
  }
  std::stringstream strStream;
  strStream << shaderFile.rdbuf();
  std::string sShader = strStream.str();

  // Create blob from the string
  IDxcBlobEncoding* pTextBlob;
  ThrowIfFailed(pLibrary->CreateBlobWithEncodingFromPinned(
      (LPBYTE)sShader.c_str(), (uint32_t)sShader.size(), 0, &pTextBlob));

  // Compile
  IDxcOperationResult* pResult;
  ThrowIfFailed(pCompiler->Compile(pTextBlob, fileName, L"", L"lib_6_6", nullptr, 0, nullptr, 0,
                                   dxcIncludeHandler, &pResult));

  // Verify the result
  HRESULT resultCode;
  ThrowIfFailed(pResult->GetStatus(&resultCode));
  if (FAILED(resultCode))
  {
    IDxcBlobEncoding* pError;
    hr = pResult->GetErrorBuffer(&pError);
    if (FAILED(hr))
    {
      throw std::logic_error("Failed to get shader compiler error");
    }

    // Convert error blob to a string
    std::vector<char> infoLog(pError->GetBufferSize() + 1);
    memcpy(infoLog.data(), pError->GetBufferPointer(), pError->GetBufferSize());
    infoLog[pError->GetBufferSize()] = 0;

    std::string errorMsg = "Shader Compiler Error:\n";
    errorMsg.append(infoLog.data());

    MessageBoxA(nullptr, errorMsg.c_str(), "Error!", MB_OK);
    throw std::logic_error("Failed compile shader");
  }

  IDxcBlob* pBlob;
  ThrowIfFailed(pResult->GetResult(&pBlob));
  return pBlob;
}

//--------------------------------------------------------------------------------------------------
//
//
ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, uint32_t count,
                                           D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
{
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.NumDescriptors = count;
  desc.Type = type;
  desc.Flags =
      shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  
  ID3D12DescriptorHeap* pHeap;
  ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pHeap)));
  return pHeap;
}

} // namespace nv_helpers_dx12