#include "UI.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <string>

void UI::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

void UI::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void UI::newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void UI::render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UI::drawMainWindow(AppState& state, SceneController* scene) {
    ImGui::Begin("OpenPit3D Kontrol Paneli");

    ImGui::Text("Veri Durumu: %s", scene->dataReady() ? "Yuklendi" : "Bekleniyor (Dosya Surukle)");
    ImGui::Separator();

    // Görsel Ayarlar
    ImGui::Text("Gorsel Ayarlar");
    ImGui::Checkbox("Sondajlari Goster", &state.render.showDrillholes);
    ImGui::Checkbox("Bloklari Goster", &state.render.showBlocks);
    ImGui::Checkbox("Wireframe", &state.render.showWireframe);
    ImGui::Checkbox("Grid", &state.render.showGrid);
    ImGui::Separator();

    // Geoteknik Şev Bölgeleri
    ImGui::Text("Geoteknik Bolgeler (Derinlik ve Yon Bazli Sev)");
    for (size_t i = 0; i < state.opt.slopeRegions.size(); ++i) {
        auto& reg = state.opt.slopeRegions[i];
        ImGui::PushID((int)i);
        ImGui::Text("Bolge %d", (int)i + 1);
        ImGui::InputFloat("Baslangic Der. (m)", &reg.fromDepth);
        ImGui::InputFloat("Bitis Der. (m)", &reg.toDepth);
        ImGui::SliderFloat("Kuzey Duvari (-Z)", &reg.angleN, 20.f, 85.f);
        ImGui::SliderFloat("Guney Duvari (+Z)", &reg.angleS, 20.f, 85.f);
        ImGui::SliderFloat("Dogu Duvari (+X)", &reg.angleE, 20.f, 85.f);
        ImGui::SliderFloat("Bati Duvari (-X)", &reg.angleW, 20.f, 85.f);

        if (ImGui::Button("Bu Bolgeyi Sil")) {
            state.opt.slopeRegions.erase(state.opt.slopeRegions.begin() + i);
            --i;
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (ImGui::Button("Yeni Bolge Ekle", ImVec2(-1, 0))) {
        state.opt.slopeRegions.push_back({ 0.f, 100.f, 45.f, 45.f, 45.f, 45.f });
    }
    ImGui::Separator();

    // Kestirim Ayarları
    ImGui::Text("Kestirim (Estimation) Parametreleri");
    const char* methods[] = { "IDW", "Nearest Neighbor", "Ordinary Kriging" };
    int currentMethod = (int)state.opt.estimMethod;
    if (ImGui::Combo("Yontem", &currentMethod, methods, IM_ARRAYSIZE(methods))) {
        state.opt.estimMethod = (EstimationMethod)currentMethod;
    }

    if (state.opt.estimMethod == EstimationMethod::IDW) {
        ImGui::SliderInt("IDW Ussu", &state.opt.idwPower, 1, 5);
    }
    else if (state.opt.estimMethod == EstimationMethod::KRIGING) {
        ImGui::InputFloat("Nugget (C0)", &state.opt.variogramNugget);
        ImGui::InputFloat("Partial Sill (C)", &state.opt.variogramSill);
        ImGui::InputFloat("Range (a) (m)", &state.opt.variogramRange);
        ImGui::SliderInt("Max Ornek", &state.opt.krigingMaxSample, 4, 32);
    }
    ImGui::InputFloat("Arama Yaricapi (m)", &state.opt.searchRadius);
    ImGui::Separator();

    // Optimizasyon Parametreleri
    ImGui::Text("Ekonomik Parametreler");
    ImGui::InputFloat("Metal Fiyati ($/t)", &state.econ.metalPrice);
    ImGui::InputFloat("Maden Maliyeti ($/t)", &state.econ.miningCost);
    ImGui::InputFloat("Proses Maliyeti ($/t)", &state.econ.processCost);

    float cutOff = state.econ.computeCutoffGrade();
    if (cutOff >= FLT_MAX * 0.5f) ImGui::TextColored(ImVec4(1, 0, 0, 1), "Hesaplanan Cut-Off: Ekonomik Degil!");
    else ImGui::Text("Hesaplanan Cut-Off: %.3f %%", cutOff);
    ImGui::Separator();

    // Aksiyon Butonlari
    if (!scene->dataReady()) ImGui::BeginDisabled();

    std::string estBtnText = "Kestirimi Baslat (" + std::string(methods[currentMethod]) + ")";
    if (ImGui::Button(estBtnText.c_str(), ImVec2(-1, 30))) {
        state.events.runEstimation = true;
    }

    if (scene->lgRunning()) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "LG Optimizasyonu Calisiyor...");
    }
    else {
        if (ImGui::Button("LG Optimizasyonunu Baslat", ImVec2(-1, 30))) {
            state.events.runOptimization = true;
        }
    }

    if (scene->currentStage() == AppStage::PIT_SHELL) {
        ImGui::Checkbox("Bench Simulasyonu (Animasyon)", &state.animating);
        if (ImGui::Button("CSV Raporu Cikart", ImVec2(-1, 30))) {
            state.events.exportReport = true;
        }
    }

    if (!scene->dataReady()) ImGui::EndDisabled();

    ImGui::End();
}