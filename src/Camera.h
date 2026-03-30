// Camera.h
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ─────────────────────────────────────────────────────────────────────────────
//  Camera — arcball orbit around a focal target
//
//  Controls (hooked up in main.cpp via GLFW callbacks):
//    Left-drag   → orbit (yaw / pitch)
//    Right-drag  → pan target point
//    Scroll      → zoom (dolly)
//    R key       → reset to default view
// ─────────────────────────────────────────────────────────────────────────────
class Camera {
public:
    Camera();

    // GLFW callback entry points (call from lambdas in main.cpp)
    void onMouseButton(int button, int action, double xpos, double ypos);
    void onMouseMove  (double xpos, double ypos);
    void onScroll     (double yoffset);
    void onKey        (int key, int action);

    glm::mat4 viewMatrix() const;

    // Read back for frustum culler
    glm::vec3 position() const;

private:
    // Spherical coordinates around target
    glm::vec3 target_  = {0.f, 0.f, 0.f};
    float     yaw_     = 45.f;     // degrees
    float     pitch_   = -30.f;    // degrees (negative = above horizon)
    float     distance_= 800.f;    // metres

    // Drag state
    bool   leftDrag_  = false;
    bool   rightDrag_ = false;
    double lastX_     = 0.0;
    double lastY_     = 0.0;

    static constexpr float ORBIT_SPEED = 0.4f;   // degrees per pixel
    static constexpr float PAN_SPEED   = 0.5f;   // metres per pixel (scaled by dist)
    static constexpr float ZOOM_SPEED  = 0.08f;  // fraction of distance per scroll tick
    static constexpr float MIN_DIST    = 10.f;
    static constexpr float MAX_DIST    = 20000.f;
    static constexpr float MIN_PITCH   = -89.f;
    static constexpr float MAX_PITCH   =  89.f;
};
