#pragma once
#include "GameObject.h"
#include <vector>
//class D3D12HelloTriangle;
class Scene {
public:
	//Scene(D3D12HelloTriangle* app) {
		//m_app = app;
	//}
	Scene() = default;
	void UploadScene();
	void AddGameObject(GameObject& gameObject) {
		m_sceneObjects.push_back(gameObject);
	}
	// Remove later
	//D3D12HelloTriangle* m_app;
	std::vector<GameObject> m_sceneObjects;
	// ADD RAYGEN and  MISS sahders - as it makes more sence to describe them in scene
};