#pragma once
#include "Model.h"
#include <glm/glm.hpp>
class GameObject {
public:

	glm::mat4 m_transform;
	Model* m_model;
};