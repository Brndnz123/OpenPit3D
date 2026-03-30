// Camera.cpp
#include "Camera.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

Camera::Camera() = default;

// ─────────────────────────────────────────────────────────────────────────────
//  viewMatrix
//  Converts spherical (yaw, pitch, distance) + target into a look-at matrix.
// ─────────────────────────────────────────────────────────────────────────────
glm::mat4 Camera::viewMatrix() const {
    float yawR   = glm::radians(yaw_);
    float pitchR = glm::radians(pitch_);

    // Eye position in world space
    glm::vec3 eye = target_ + glm::vec3(
        distance_ * std::cos(pitchR) * std::sin(yawR),
        distance_ * std::sin(pitchR),           // positive pitch = looking from above
        distance_ * std::cos(pitchR) * std::cos(yawR)
    );

    return glm::lookAt(eye, target_, glm::vec3(0.f, 1.f, 0.f));
}

glm::vec3 Camera::position() const {
    float yawR   = glm::radians(yaw_);
    float pitchR = glm::radians(pitch_);
    return target_ + glm::vec3(
        distance_ * std::cos(pitchR) * std::sin(yawR),
        distance_ * std::sin(pitchR),
        distance_ * std::cos(pitchR) * std::cos(yawR)
    );
}

// ─────────────────────────────────────────────────────────────────────────────
//  GLFW callbacks
// ─────────────────────────────────────────────────────────────────────────────
void Camera::onMouseButton(int button, int action, double xpos, double ypos) {
    bool pressed = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_LEFT)  leftDrag_  = pressed;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) rightDrag_ = pressed;
    if (pressed) { lastX_ = xpos; lastY_ = ypos; }
}

void Camera::onMouseMove(double xpos, double ypos) {
    double dx = xpos - lastX_;
    double dy = ypos - lastY_;
    lastX_ = xpos;
    lastY_ = ypos;

    if (leftDrag_) {
        // Orbit: horizontal drag → yaw, vertical drag → pitch
        yaw_   += (float)dx * ORBIT_SPEED;
        pitch_ -= (float)dy * ORBIT_SPEED;   // screen Y is inverted
        pitch_  = std::clamp(pitch_, MIN_PITCH, MAX_PITCH);
    }

    if (rightDrag_) {
        // Pan: move target in the camera's local XZ plane
        // Scale pan speed by distance so far-away scenes don't feel sluggish
        float scale = distance_ * PAN_SPEED * 0.001f;

        float yawR = glm::radians(yaw_);
        // Camera right vector (no pitch component so pan stays horizontal)
        glm::vec3 right(std::cos(yawR), 0.f, -std::sin(yawR));
        glm::vec3 up(0.f, 1.f, 0.f);

        target_ -= right * (float)dx * scale;
        target_ += up    * (float)dy * scale;
    }
}

void Camera::onScroll(double yoffset) {
    // Dolly: zoom in/out by a fraction of current distance (feels natural)
    distance_ *= (1.f - (float)yoffset * ZOOM_SPEED);
    distance_  = std::clamp(distance_, MIN_DIST, MAX_DIST);
}

void Camera::onKey(int key, int action) {
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        // Reset to default view
        target_   = {0.f, 0.f, 0.f};
        yaw_      = 45.f;
        pitch_    = -30.f;
        distance_ = 800.f;
    }
}
