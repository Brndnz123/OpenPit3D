// SceneController.h
#pragma once
#include <thread>
#include <memory>
#include "AppState.h"
#include "BlockModel.h"
#include "LGOptimizer.h"
#include "PitGenerator.h"
#include "PitScheduler.h"
#include "TextDrillholeData.h"

class SceneController {
public:
    explicit SceneController(AppState& state);
    ~SceneController();

    // Called every frame on the UI thread —
    // detects worker completion and swaps results into live state.
    void tick();

    // ── Worker launchers ──────────────────────────────────────────────────────
    bool importTextFiles(const std::string& collarsCsv,
                         const std::string& surveysCsv,
                         const std::string& samplesCsv);

    void startKriging(const BlockModel::VariogramModel&     variogram,
                      const BlockModel::SearchParams&       search,
                      const BlockModel::BlockKrigingParams& bk);

    void startLGOptimization(const PitOptimizationParams& params);

    // Generate pit shell + topo meshes on a background thread.
    // Safe to call as soon as AppStage::PIT_SHELL.
    void startMeshGeneration();

    // Generate pushback phases + mine schedule + DCF.
    void startScheduling(const PitOptimizationParams& econ,
                         const SchedulingParams&      sched,
                         float minRF   = 0.5f,
                         int   numSteps = 6);

    // Cancellation
    void cancelKriging();
    void cancelLG();

    const TextDrillholeData* data() const { return data_.get(); }

private:
    AppState& state_;
    std::unique_ptr<TextDrillholeData> data_;

    std::thread krigingThread_;
    std::thread lgThread_;
    std::thread meshThread_;
    std::thread schedThread_;

    void krigingWorker(BlockModel::VariogramModel     variogram,
                       BlockModel::SearchParams       search,
                       BlockModel::BlockKrigingParams bk);
    void lgWorker     (PitOptimizationParams params);
    void meshWorker   ();
    void schedWorker  (PitOptimizationParams econ,
                       SchedulingParams      sched,
                       float minRF, int numSteps);

    static void joinThread(std::thread& t);
};
