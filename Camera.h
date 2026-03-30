#pragma once
#include <glm/glm.hpp>
#include "Types.h"

class Camera {
public:
    glm::vec3 target{ 0.0f, 0.0f, 0.0f };
    float radius = 500.0f;
    float theta = 0.0f;
    float phi = 1.0f;
    float fov = 45.0f;
    float near_ = 1.0f;
    float far_ = 5000.0f;

    void orbit(float dTheta, float dPhi);
    void pan(float dx, float dy);
    void zoom(float delta);
    void applyPreset(ViewPreset p);
    glm::vec3 position() const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;
private:
    void clampPhi();
};