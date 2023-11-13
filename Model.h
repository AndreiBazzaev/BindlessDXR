#pragma once
#include <string>
#include <vector>
//class D3D12HelloTriangle;
class ResourceManager;
class Model {
public:
	//Model(D3D12HelloTriangle* app) {
	//	m_app = app;
	//}
	Model() = default;
	Model* LoadModel(ResourceManager* resManager, const std::string& name);
	uint64_t m_BlasPointer;
	uint32_t m_heapPointer;
	std::string m_name;
	// Remove later
	//D3D12HelloTriangle* m_app;

};