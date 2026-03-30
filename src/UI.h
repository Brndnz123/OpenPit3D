// UI.h
#pragma once
#include "AppState.h"
#include "SceneController.h"
#include "Renderer.h"
#include "Camera.h"
#include "CashFlowAnalysis.h"
#include "BlockModel.h"
#include "PitScheduler.h"

class UI {
public:
    UI()  = default;
    ~UI() = default;

    void render(AppState& state, SceneController& scene,
                Renderer& renderer, Camera& camera);

private:
    void drawToolbar      (AppState& state, SceneController& scene);
    void drawBlockModel   (AppState& state, SceneController& scene);
    void drawOptimisation (AppState& state, SceneController& scene);
    void drawScheduling   (AppState& state, SceneController& scene);   // NEW
    void drawFinancials   (AppState& state);
    void drawRenderPanel  (AppState& state, Renderer& renderer, BlockColorMode& cm);
    void drawStatsOverlay (const Renderer& renderer, const AppState& state);
    void drawProgressModal(AppState& state, SceneController& scene);

    VariogramModel        varModel_;
    SearchParams          searchParams_;
    BlockKrigingParams    krigingParams_;
    PitOptimizationParams pitParams_;
    SchedulingParams      schedParams_;

    CashFlowAnalysis cashFlow_;
    bool             cashFlowDirty_ = true;

    BlockColorMode colorMode_       = BlockColorMode::GRADE;
    bool           showPitMesh_     = true;
    bool           showTopoMesh_    = true;
    bool           showMeshWire_    = false;

    char collarsPathBuf_[512] = "collars.txt";
    char surveysPathBuf_[512] = "surveys.txt";
    char samplesPathBuf_[512] = "samples.txt";
    std::string importStatus_;
};
