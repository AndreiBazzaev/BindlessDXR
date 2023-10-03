#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include "Model.h"
class ResourceManager {
public:
	void RegisterModel(const std::string& name, Model model) {
		m_loadedModels.emplace(name, model);
	}
	Model* ResourceManager::GetModel(const std::string& name) {
		auto it = m_loadedModels.find(name);
		return (it != m_loadedModels.end()) ? &it->second : nullptr;
	}
private:
	std::unordered_map<std::string, Model> m_loadedModels;
};