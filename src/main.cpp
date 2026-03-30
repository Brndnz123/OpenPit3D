// main.cpp
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <mutex>

#include "AppState.h"
#include "SceneController.h"
#include "Renderer.h"
#include "Camera.h"
#include "UI.h"

static constexpr int  WIN_W      = 1600;
static constexpr int  WIN_H      = 900;
static constexpr char WIN_TITLE[]= "OpenPit3D";
static constexpr char GLSL_VER[] = "#version 330 core";

// ── Globals for GLFW callbacks (camera pointer + ImGui pass-through flag) ────
static Camera*  g_camera  = nullptr;
static bool     g_imguiWantsInput = false;

static void glfwErrorCb(int code, const char* msg) {
    std::cerr << "[GLFW] Error " << code << ": " << msg << '\n';
}

#ifndef NDEBUG
static void APIENTRY glDebugCb(GLenum, GLenum type, GLuint, GLenum severity,
                                GLsizei, const GLchar* msg, const void*)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::cerr << "[GL] " << (type == GL_DEBUG_TYPE_ERROR ? "**ERR** " : "") << msg << '\n';
}
#endif

// FIX 10: GLFW camera callbacks wired to Camera methods
static void mouseButtonCb(GLFWwindow*, int btn, int action, int /*mods*/) {
    if (g_imguiWantsInput) return;   // Let ImGui eat mouse events over panels
    double x, y;
    glfwGetCursorPos(glfwGetCurrentContext(), &x, &y);
    if (g_camera) g_camera->onMouseButton(btn, action, x, y);
}

static void mouseMoveCb(GLFWwindow*, double x, double y) {
    if (g_imguiWantsInput) return;
    if (g_camera) g_camera->onMouseMove(x, y);
}

static void scrollCb(GLFWwindow*, double /*xoff*/, double yoff) {
    if (g_imguiWantsInput) return;
    if (g_camera) g_camera->onScroll(yoff);
}

static void keyCb(GLFWwindow* win, int key, int /*sc*/, int action, int /*mods*/) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(win, GLFW_TRUE);
    if (g_camera) g_camera->onKey(key, action);
}

int main(int /*argc*/, char** /*argv*/)
{
    glfwSetErrorCallback(glfwErrorCb);
    if (!glfwInit()) throw std::runtime_error("glfwInit() failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H, WIN_TITLE, nullptr, nullptr);
    if (!window) { glfwTerminate(); throw std::runtime_error("glfwCreateWindow() failed"); }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) throw std::runtime_error("glewInit() failed");

#ifndef NDEBUG
    if (GLEW_ARB_debug_output) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugCb, nullptr);
    }
#endif

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.12f, 0.12f, 0.14f, 1.f);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGuiStyle& style    = ImGui::GetStyle();
    style.WindowRounding = 4.f;
    style.FrameRounding  = 3.f;
    style.GrabRounding   = 3.f;
    style.WindowPadding  = {10.f, 10.f};
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(GLSL_VER);

    AppState        appState;
    SceneController scene(appState);
    Renderer        renderer;
    Camera          camera;
    UI              ui;

    // FIX 10: register camera with global pointer before setting callbacks
    g_camera = &camera;
    glfwSetMouseButtonCallback(window, mouseButtonCb);
    glfwSetCursorPosCallback  (window, mouseMoveCb);
    glfwSetScrollCallback     (window, scrollCb);
    glfwSetKeyCallback        (window, keyCb);

    renderer.init();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // FIX 10: update ImGui input-capture flag each frame
        g_imguiWantsInput = io.WantCaptureMouse;

        scene.tick();

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        float aspect = (fbH > 0) ? (float)fbW / (float)fbH : 1.f;

        glm::mat4 proj = glm::perspective(glm::radians(45.f), aspect, 1.f, 20000.f);
        glm::mat4 vp   = proj * camera.viewMatrix();

        glViewport(0, 0, fbW, fbH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        {
            std::lock_guard<std::mutex> lk(appState.gridMutex);

            // FIX 7: pass live RenderSettings from AppState (UI writes to this)
            renderer.beginFrame(vp, appState.renderSettings);

            if (!appState.grid.blocks.empty())
                renderer.renderBlocks(appState.grid.blocks,
                                      appState.renderSettings.showBlocks
                                          ? BlockColorMode::GRADE
                                          : BlockColorMode::FLAT);

            // Render pit shell and topo meshes (showGrid/showAxes repurposed as pit/topo toggles)
            if (appState.renderSettings.showGrid && appState.pitMesh.isUploaded())
                renderer.renderMesh(appState.pitMesh, false);
            if (appState.renderSettings.showAxes && appState.topoMesh.isUploaded())
                renderer.renderMesh(appState.topoMesh, false);

            if (scene.data() && appState.renderSettings.showDrillholes)
                renderer.renderDrillholes(scene.data()->desurveyed());

            renderer.endFrame();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui.render(appState, scene, renderer, camera);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
