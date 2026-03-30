#pragma once
#include "AppState.h"
#include "SceneController.h"

struct GLFWwindow;

class UI {
public:
    static void init(GLFWwindow* window);
    static void shutdown();
    static void newFrame();
    static void render();

    // Tüm kontrol panelini çizen ana fonksiyon
    static void drawMainWindow(AppState& state, SceneController* scene);
};