#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <string>
#include <algorithm>
#include <memory>

#include "Types.h"
#include "AppState.h"
#include "Camera.h"
#include "Renderer.h"
#include "PitGenerator.h"
#include "BlockModel.h"
#include "DrillholeDatabase.h"
#include "LGOptimizer.h"
#include "SceneController.h"
#include "UI.h"

// ─── Global durum ─────────────────────────────────────────────────────────────
static AppState           gState;
static Camera             gCamera;
static Renderer* gRenderer = nullptr;
static PitGenerator* gGen = nullptr;
static BlockModel* gBlockModel = nullptr;
static DrillholeDatabase* gDatabase = nullptr;
static LGOptimizer* gOptimizer = nullptr;
static SceneController* gScene = nullptr;

static int   gWinW = 1280, gWinH = 720;
static float gLastFrameTime = 0.f;

static bool gHasCollars = false;
static bool gHasSurveys = false;
static bool gHasSamples = false;

// ─── Dosya bırakma callback ───────────────────────────────────────────────────
void onDrop(GLFWwindow*, int count, const char** paths) {
    if (!gDatabase || !gBlockModel || !gScene) return;
    std::cout << "\n[Drop] " << count << " dosya alındı.\n";

    for (int i = 0; i < count; ++i) {
        std::string path = paths[i];
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower.find("collar") != std::string::npos) {
            if (gDatabase->loadCollars(path)) gHasCollars = true;
        }
        else if (lower.find("survey") != std::string::npos) {
            if (gDatabase->loadSurveys(path)) gHasSurveys = true;
        }
        else if (lower.find("sample") != std::string::npos) {
            if (gDatabase->loadSamples(path)) gHasSamples = true;
        }
        else {
            std::cout << "[Drop] Tanımlanamayan dosya: " << path << "\n";
        }
    }

    if (!gHasCollars || !gHasSamples) {
        std::cout << "[Drop] En az collar + sample gerekli.\n";
        return;
    }

    if (gScene->dataReady()) {
        gState.events.runEstimation = true;
    }
    else {
        gScene->onDataLoaded(gState);
    }
}

// ─── Pencere / giriş callback'leri ───────────────────────────────────────────
void onResize(GLFWwindow*, int w, int h) {
    gWinW = w; gWinH = h;
    if (gRenderer) gRenderer->setWindowSize(w, h);
}

void onKey(GLFWwindow* win, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (key == GLFW_KEY_ESCAPE) { glfwSetWindowShouldClose(win, GLFW_TRUE); return; }
}

void onMouseButton(GLFWwindow* win, int button, int action, int) {
    double x, y; glfwGetCursorPos(win, &x, &y);
    gState.lastX = x; gState.lastY = y;
    if (button == GLFW_MOUSE_BUTTON_LEFT)  gState.leftDown = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_RIGHT) gState.rightDown = (action == GLFW_PRESS);
}

void onMouseMove(GLFWwindow*, double x, double y) {
    float dx = (float)(x - gState.lastX), dy = (float)(y - gState.lastY);
    gState.lastX = x; gState.lastY = y;
    if (gState.leftDown)  gCamera.orbit(-dx * 0.004f, dy * 0.004f);
    if (gState.rightDown) gCamera.pan(-dx, dy);
}

void onScroll(GLFWwindow*, double, double dy) {
    gCamera.zoom((float)-dy * 30.f);
}

// ─── Giriş noktası ───────────────────────────────────────────────────────────
int main() {
    if (!glfwInit()) { std::cerr << "[Main] glfwInit başarısız.\n"; return -1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(gWinW, gWinH, "OpenPit3D — LG Optimizer v4.0", nullptr, nullptr);
    if (!window) {
        std::cerr << "[Main] Pencere oluşturulamadı.\n";
        glfwTerminate(); return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::cerr << "[Main] GLEW başarısız.\n"; return -1; }

    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onMouseMove);
    glfwSetScrollCallback(window, onScroll);
    glfwSetFramebufferSizeCallback(window, onResize);
    glfwSetDropCallback(window, onDrop);

    // İŞTE BURADA: Pencere oluşturulduktan sonra ImGui başlatılıyor
    UI::init(window);

    // ── Sistem nesneleri ──────────────────────────────────────────────────────
    static Renderer          renderer;
    static PitGenerator      gen(42);
    static DrillholeDatabase db;
    static BlockModel        pitGrid(20, 10, 20, 20.f);
    static LGOptimizer       optimizer;
    static SceneController   scene;

    gRenderer = &renderer;
    gGen = &gen;
    gDatabase = &db;
    gBlockModel = &pitGrid;
    gOptimizer = &optimizer;
    gScene = &scene;

    renderer.init();
    renderer.setWindowSize(gWinW, gWinH);
    scene.setup(&renderer, &gCamera, &gen, &pitGrid, &db, &optimizer);
    gCamera.applyPreset(ViewPreset::FREE);
    gCamera.radius = 800.f;

    std::cout << "[Main] Hazır — collar / survey / sample dosyalarını pencereye sürükleyin.\n\n";

    // ── Ana döngü ─────────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        float now = (float)glfwGetTime();
        [[maybe_unused]] float dt = now - gLastFrameTime;
        gLastFrameTime = now;

        static ViewPreset lastView = ViewPreset::FREE;
        if (gState.view != lastView) {
            gCamera.applyPreset(gState.view);
            lastView = gState.view;
        }

        if (gState.animating && gState.stage != AppStage::PIT_SHELL)
            gCamera.orbit(dt * 0.3f, 0.f);

        // Olayları işle
        scene.tick(gState);

        // ImGui Çerçevesini Başlat
        UI::newFrame();
        UI::drawMainWindow(gState, &scene);

        // 3D Çizimi Yap
        float aspect = (gWinH > 0) ? (float)gWinW / (float)gWinH : 1.f;
        renderer.render(gCamera, aspect, gState.render);

        // ImGui'yi Çiz
        UI::render();

        glfwSwapBuffers(window);
    }

    UI::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}