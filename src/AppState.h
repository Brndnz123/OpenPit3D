// AppState.h
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "Types.h"
#include "LGOptimizer.h"
#include "MeshModel.h"
#include "PitGenerator.h"
#include "CashFlowAnalysis.h"

struct EconomicParams {
    float oreValuePerTonne       = 45.f;
    float miningCostPerTonne     =  8.f;
    float processingCostPerTonne = 12.f;
    float blockTonnes            = 8000.f;
    float cutOffGrade            = 0.5f;
    float discountRate           = 0.08f;
};

struct KrigingWorkerState {
    std::atomic<bool>  running  { false };
    std::atomic<bool>  done     { false };
    std::atomic<bool>  cancel   { false };
    std::atomic<float> progress { 0.f  };
    std::atomic<int>   blocksEstimated { 0 };
    std::mutex         resultMutex;
    std::vector<Block> resultBlocks;
    std::string        errorMsg;
    void reset() {
        running.store(false); done.store(false); cancel.store(false);
        progress.store(0.f); blocksEstimated.store(0);
        std::lock_guard<std::mutex> lk(resultMutex);
        resultBlocks.clear(); errorMsg.clear();
    }
};

struct LGWorkerState {
    std::atomic<bool>  running  { false };
    std::atomic<bool>  done     { false };
    std::atomic<bool>  cancel   { false };
    std::atomic<float> progress { 0.f  };
    std::mutex         resultMutex;
    std::vector<Block> resultBlocks;
    double             totalPitValue  = 0.0;
    int                blocksInPit    = 0;
    double             strippingRatio = 0.0;
    std::string        errorMsg;
    void reset() {
        running.store(false); done.store(false); cancel.store(false);
        progress.store(0.f);
        std::lock_guard<std::mutex> lk(resultMutex);
        resultBlocks.clear(); errorMsg.clear();
        totalPitValue = 0.0; blocksInPit = 0; strippingRatio = 0.0;
    }
};

// ── Mesh generation worker (PitGenerator runs on a thread) ───────────────────
struct MeshWorkerState {
    std::atomic<bool>  running  { false };
    std::atomic<bool>  done     { false };
    std::atomic<float> progress { 0.f  };
    std::mutex         resultMutex;
    MeshModel          pitResult;    // Ready to upload() on the GL thread
    MeshModel          topoResult;
    int                pitFaces  = 0;
    int                topoFaces = 0;
    std::string        errorMsg;
    void reset() {
        running.store(false); done.store(false); progress.store(0.f);
        std::lock_guard<std::mutex> lk(resultMutex);
        pitResult  = MeshModel{};
        topoResult = MeshModel{};
        pitFaces = topoFaces = 0;
        errorMsg.clear();
    }
};

// ── Scheduling worker ────────────────────────────────────────────────────────
struct ScheduleWorkerState {
    std::atomic<bool>  running  { false };
    std::atomic<bool>  done     { false };
    std::atomic<float> progress { 0.f  };
    std::mutex         resultMutex;
    std::vector<Block> resultBlocks;       // Blocks with mineYear assigned
    CashFlowAnalysis   resultCFA;          // Fully generated DCF model
    std::vector<PhaseMetrics> phases;      // Phase breakdown
    std::string        errorMsg;
    void reset() {
        running.store(false); done.store(false); progress.store(0.f);
        std::lock_guard<std::mutex> lk(resultMutex);
        resultBlocks.clear(); phases.clear(); errorMsg.clear();
        resultCFA = CashFlowAnalysis{};
    }
};

struct AppState {
    AppStage              stage          = AppStage::EMPTY;
    EconomicParams        econParams     = {};
    PitOptimizationParams optParams      = {};
    RenderSettings        renderSettings = {};

    KrigingWorkerState  krigingState;
    LGWorkerState       lgState;
    MeshWorkerState     meshState;
    ScheduleWorkerState schedState;

    std::mutex gridMutex;
    BlockGrid  grid;

    // Live meshes (GPU objects — only touch from the GL/UI thread)
    MeshModel pitMesh;
    MeshModel topoMesh;

    // Scheduling results exposed to UI
    std::vector<PhaseMetrics>   phases;
    CashFlowAnalysis            scheduledCFA;
    bool                        hasSchedule = false;

    AppState() = default;
    AppState(const AppState&)            = delete;
    AppState& operator=(const AppState&) = delete;
};
