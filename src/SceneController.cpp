// SceneController.cpp
#include "SceneController.h"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <omp.h>

SceneController::SceneController(AppState& state) : state_(state) {}

SceneController::~SceneController() {
    if (state_.krigingState.running.load()) state_.krigingState.cancel.store(true);
    if (state_.lgState.running.load())      state_.lgState.cancel.store(true);
    joinThread(krigingThread_);
    joinThread(lgThread_);
    joinThread(meshThread_);
    joinThread(schedThread_);
}

void SceneController::joinThread(std::thread& t) {
    if (t.joinable()) t.join();
}

// ─────────────────────────────────────────────────────────────────────────────
//  importTextFiles
// ─────────────────────────────────────────────────────────────────────────────

bool SceneController::importTextFiles(const std::string& collarsCsv,
                                       const std::string& surveysCsv,
                                       const std::string& samplesCsv)
{
    try {
        data_ = std::make_unique<TextDrillholeData>();
        if (!collarsCsv.empty()) data_->importCollarsCSV(collarsCsv);
        if (!surveysCsv.empty()) data_->importSurveysCSV(surveysCsv);
        if (!samplesCsv.empty()) data_->importSamplesCSV(samplesCsv);
        data_->desurveyAll();
        state_.stage = AppStage::DRILLHOLES;
        std::cout << "[SceneController] Imported "
                  << data_->desurveyed().size() << " desurveyed samples.\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[SceneController] Import error: " << e.what() << '\n';
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  tick — called every frame on the UI/GL thread
// ─────────────────────────────────────────────────────────────────────────────

void SceneController::tick() {
    // ── Kriging ───────────────────────────────────────────────────────────────
    if (state_.krigingState.done.load()) {
        state_.krigingState.done.store(false);
        joinThread(krigingThread_);
        std::lock_guard<std::mutex> lk(state_.krigingState.resultMutex);
        if (state_.krigingState.errorMsg.empty()) {
            std::lock_guard<std::mutex> glk(state_.gridMutex);
            state_.grid.blocks = std::move(state_.krigingState.resultBlocks);
            state_.stage = AppStage::ESTIMATED;
            std::cout << "[SceneController] Kriging complete → ESTIMATED\n";
        } else {
            std::cerr << "[SceneController] Kriging error: "
                      << state_.krigingState.errorMsg << '\n';
        }
        state_.krigingState.running.store(false);
    }

    // ── LG ────────────────────────────────────────────────────────────────────
    if (state_.lgState.done.load()) {
        state_.lgState.done.store(false);
        joinThread(lgThread_);
        {
            std::lock_guard<std::mutex> lk(state_.lgState.resultMutex);
            if (state_.lgState.errorMsg.empty()) {
                std::lock_guard<std::mutex> glk(state_.gridMutex);
                state_.grid.blocks = std::move(state_.lgState.resultBlocks);
                state_.stage = AppStage::PIT_SHELL;
                std::cout << "[SceneController] LG complete → PIT_SHELL"
                          << "  $" << state_.lgState.totalPitValue
                          << "  blocks=" << state_.lgState.blocksInPit
                          << "  SR=" << state_.lgState.strippingRatio << ":1\n";
            } else {
                std::cerr << "[SceneController] LG error: "
                          << state_.lgState.errorMsg << '\n';
            }
        }
        state_.lgState.running.store(false);

        // Auto-kick off mesh generation once LG is done
        if (state_.stage == AppStage::PIT_SHELL)
            startMeshGeneration();
    }

    // ── Mesh generation ───────────────────────────────────────────────────────
    if (state_.meshState.done.load()) {
        state_.meshState.done.store(false);
        joinThread(meshThread_);
        {
            std::lock_guard<std::mutex> lk(state_.meshState.resultMutex);
            if (state_.meshState.errorMsg.empty()) {
                // Move CPU mesh data into AppState's live meshes,
                // then upload to GPU (must be done on the GL thread = here)
                state_.pitMesh  = std::move(state_.meshState.pitResult);
                state_.topoMesh = std::move(state_.meshState.topoResult);
                state_.pitMesh.upload();
                state_.topoMesh.upload();
                std::cout << "[SceneController] Mesh gen complete."
                          << " Pit faces=" << state_.pitMesh.indices.size()/3
                          << " Topo faces=" << state_.topoMesh.indices.size()/3 << '\n';
            } else {
                std::cerr << "[SceneController] Mesh error: "
                          << state_.meshState.errorMsg << '\n';
            }
        }
        state_.meshState.running.store(false);
    }

    // ── Scheduling ────────────────────────────────────────────────────────────
    if (state_.schedState.done.load()) {
        state_.schedState.done.store(false);
        joinThread(schedThread_);
        {
            std::lock_guard<std::mutex> lk(state_.schedState.resultMutex);
            if (state_.schedState.errorMsg.empty()) {
                std::lock_guard<std::mutex> glk(state_.gridMutex);
                state_.grid.blocks   = std::move(state_.schedState.resultBlocks);
                state_.phases        = state_.schedState.phases;
                state_.scheduledCFA  = std::move(state_.schedState.resultCFA);
                state_.hasSchedule   = true;
                std::cout << "[SceneController] Scheduling complete. "
                          << state_.phases.size() << " phases.\n";
            } else {
                std::cerr << "[SceneController] Schedule error: "
                          << state_.schedState.errorMsg << '\n';
            }
        }
        state_.schedState.running.store(false);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Kriging
// ─────────────────────────────────────────────────────────────────────────────

void SceneController::startKriging(
        const BlockModel::VariogramModel&     variogram,
        const BlockModel::SearchParams&       search,
        const BlockModel::BlockKrigingParams& bk)
{
    if (state_.krigingState.running.load()) return;
    if (!data_ || data_->desurveyed().empty()) {
        std::cerr << "[SceneController] Import drillhole files first.\n";
        return;
    }
    joinThread(krigingThread_);
    state_.krigingState.reset();
    state_.krigingState.running.store(true);
    state_.stage = AppStage::OPTIMIZING;
    krigingThread_ = std::thread(&SceneController::krigingWorker,
                                  this, variogram, search, bk);
}

void SceneController::krigingWorker(BlockModel::VariogramModel    variogram,
                                     BlockModel::SearchParams      search,
                                     BlockModel::BlockKrigingParams bk)
{
    try {
        BlockGrid workGrid;
        { std::lock_guard<std::mutex> lk(state_.gridMutex); workGrid = state_.grid; }

        BlockModel model;
        model.setVariogram(variogram); model.setSearchParams(search);
        model.setKrigingParams(bk);
        model.setSamples(&data_->desurveyed(), &data_->spatialIndex());

        int total = workGrid.totalBlocks();
        auto& ks  = state_.krigingState;

        #pragma omp parallel for schedule(dynamic, 64) default(none) \
            shared(workGrid, model, ks, total)
        for (int i = 0; i < total; ++i) {
            if (ks.cancel.load()) continue;
            Block& b = workGrid.blocks[i];
            if (b.state == LGState::ACTIVE) {
                double var = 0.0;
                b.grade = (float)model.estimateBlock(b, &var);
            }
            if (i % 64 == 0) {
                ks.blocksEstimated.fetch_add(64, std::memory_order_relaxed);
                ks.progress.store((float)i / (float)total, std::memory_order_relaxed);
            }
        }
        if (ks.cancel.load()) {
            std::lock_guard<std::mutex> lk(ks.resultMutex); ks.errorMsg = "Cancelled.";
        } else {
            std::lock_guard<std::mutex> lk(ks.resultMutex);
            ks.resultBlocks = std::move(workGrid.blocks); ks.progress.store(1.f);
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(state_.krigingState.resultMutex);
        state_.krigingState.errorMsg = e.what();
    }
    state_.krigingState.done.store(true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  LG
// ─────────────────────────────────────────────────────────────────────────────

void SceneController::startLGOptimization(const PitOptimizationParams& params) {
    if (state_.lgState.running.load()) return;
    if (state_.stage < AppStage::ESTIMATED) {
        std::cerr << "[SceneController] Run Kriging first.\n"; return;
    }
    joinThread(lgThread_);
    state_.lgState.reset();
    state_.lgState.running.store(true);
    state_.stage = AppStage::OPTIMIZING;
    lgThread_ = std::thread(&SceneController::lgWorker, this, params);
}

void SceneController::lgWorker(PitOptimizationParams params) {
    try {
        BlockGrid workGrid;
        { std::lock_guard<std::mutex> lk(state_.gridMutex); workGrid = state_.grid; }
        LGOptimizer optimizer;
        auto& ls = state_.lgState;
        optimizer.optimize(workGrid, params,
                           [&ls](float f){ ls.progress.store(f, std::memory_order_relaxed); });
        if (ls.cancel.load()) {
            std::lock_guard<std::mutex> lk(ls.resultMutex); ls.errorMsg = "Cancelled.";
        } else {
            std::lock_guard<std::mutex> lk(ls.resultMutex);
            ls.resultBlocks    = std::move(workGrid.blocks);
            ls.totalPitValue   = optimizer.totalPitValue();
            ls.blocksInPit     = optimizer.blocksInPit();
            ls.strippingRatio  = optimizer.strippingRatio();
            ls.progress.store(1.f);
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(state_.lgState.resultMutex);
        state_.lgState.errorMsg = e.what();
    }
    state_.lgState.done.store(true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mesh generation
// ─────────────────────────────────────────────────────────────────────────────

void SceneController::startMeshGeneration() {
    if (state_.meshState.running.load()) return;
    joinThread(meshThread_);
    state_.meshState.reset();
    state_.meshState.running.store(true);
    meshThread_ = std::thread(&SceneController::meshWorker, this);
}

void SceneController::meshWorker() {
    try {
        // Copy grid snapshot — PitGenerator only reads block states
        BlockGrid snap;
        { std::lock_guard<std::mutex> lk(state_.gridMutex); snap = state_.grid; }

        auto& ms = state_.meshState;
        PitGenerator gen;
        gen.generateTerrain(snap,
                            [&ms](float f){ ms.progress.store(f, std::memory_order_relaxed); });

        std::lock_guard<std::mutex> lk(ms.resultMutex);
        ms.pitResult  = std::move(gen.pitMesh);
        ms.topoResult = std::move(gen.topoMesh);
        ms.pitFaces   = gen.pitFaceCount();
        ms.topoFaces  = gen.topoFaceCount();
        ms.progress.store(1.f);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(state_.meshState.resultMutex);
        state_.meshState.errorMsg = e.what();
    }
    state_.meshState.done.store(true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Scheduling
// ─────────────────────────────────────────────────────────────────────────────

void SceneController::startScheduling(const PitOptimizationParams& econ,
                                       const SchedulingParams&      sched,
                                       float minRF, int numSteps)
{
    if (state_.schedState.running.load()) return;
    if (state_.stage < AppStage::PIT_SHELL) {
        std::cerr << "[SceneController] Run LG optimisation first.\n"; return;
    }
    joinThread(schedThread_);
    state_.schedState.reset();
    state_.schedState.running.store(true);
    schedThread_ = std::thread(&SceneController::schedWorker,
                                this, econ, sched, minRF, numSteps);
}

void SceneController::schedWorker(PitOptimizationParams econ,
                                   SchedulingParams      sched,
                                   float minRF, int numSteps)
{
    try {
        BlockGrid workGrid;
        { std::lock_guard<std::mutex> lk(state_.gridMutex); workGrid = state_.grid; }

        auto& ss = state_.schedState;
        auto progress = [&ss](float f){ ss.progress.store(f, std::memory_order_relaxed); };

        PitScheduler scheduler;

        // Phase 1: generate nested pushback phases (0..50%)
        auto phases = scheduler.generatePhases(workGrid, econ, minRF, numSteps,
                          [&progress](float f){ progress(f * 0.5f); });

        // Phase 2: schedule mining (50..100%)
        CashFlowAnalysis cfa;
        cfa.setDiscountRate(econ.miningCostPerTonne > 0 ? 0.08 : 0.08);   // default 8%
        scheduler.scheduleMining(workGrid, econ, sched, cfa,
                          [&progress](float f){ progress(0.5f + f * 0.5f); });

        std::lock_guard<std::mutex> lk(ss.resultMutex);
        ss.resultBlocks = std::move(workGrid.blocks);
        ss.phases       = std::move(phases);
        ss.resultCFA    = std::move(cfa);
        ss.progress.store(1.f);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(state_.schedState.resultMutex);
        state_.schedState.errorMsg = e.what();
    }
    state_.schedState.done.store(true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Cancellation
// ─────────────────────────────────────────────────────────────────────────────

void SceneController::cancelKriging() { state_.krigingState.cancel.store(true); }
void SceneController::cancelLG()      { state_.lgState.cancel.store(true); }
