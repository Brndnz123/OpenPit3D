#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

static constexpr float PHI_MIN = 0.05f;
static constexpr float PHI_MAX = glm::radians(88.0f);
static constexpr float RADIUS_MIN = 30.0f;
static constexpr float RADIUS_MAX = 3000.0f;

void Camera::clampPhi() {
    phi = std::clamp(phi, PHI_MIN, PHI_MAX);
}

void Camera::orbit(float dTheta, float dPhi) {
    theta += dTheta;
    phi += dPhi;
    clampPhi();
}

void Camera::pan(float dx, float dy) {
    glm::vec3 pos = position();
    glm::vec3 fwd = glm::normalize(target - pos);
    glm::vec3 right = glm::normalize(glm::cross(fwd, { 0,1,0 }));
    glm::vec3 up = glm::cross(right, fwd);
    float scale = radius * 0.001f;
    target -= right * dx * scale;
    target += up * dy * scale;
}

void Camera::zoom(float delta) {
    radius = std::clamp(radius + delta * radius * 0.001f, RADIUS_MIN, RADIUS_MAX);
}

glm::vec3 Camera::position() const {
    return target + glm::vec3{
        radius * std::sin(phi) * std::sin(theta),
        radius * std::cos(phi),
        radius * std::sin(phi) * std::cos(theta)
    };
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position(), target, { 0,1,0 });
}

glm::mat4 Camera::projMatrix(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, near_, far_);
}

void Camera::applyPreset(ViewPreset p) {
    target = { 0.0f, -50.0f, 0.0f };
    switch (p) {
    case ViewPreset::FREE:
        theta = glm::radians(45.0f);
        phi = glm::radians(62.0f);
        radius = 800.0f;
        break;
    case ViewPreset::TOP:
        theta = 0.0f;
        phi = PHI_MIN;
        radius = 800.0f;
        break;
    case ViewPreset::SIDE:
        theta = 0.0f;
        phi = glm::radians(87.0f);
        radius = 750.0f;
        break;
    case ViewPreset::SECTION:
        theta = glm::radians(90.0f);
        phi = glm::radians(87.0f);
        radius = 680.0f;
        break;
    }
}