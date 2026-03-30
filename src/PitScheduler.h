#pragma once
#include "Types.h"
#include "LGOptimizer.h"
#include "CashFlowAnalysis.h"
#include <vector>
#include <functional>

struct SchedulingParams {
    float maxMiningCapacityPerYear = 15000000.f; // tonnes (e.g., 15 Mtpa)
    float maxMillingCapacityPerYear = 5000000.f;  // tonnes (e.g., 5 Mtpa)
};

struct PhaseMetrics {
    int   phaseNumber;
    float revenueFactor;
    int   blocksTotal;
    int   oreBlocks;
    float totalValue;
};

class PitScheduler {
public:
    PitScheduler() = default;
    ~PitScheduler() = default;

    // Generate nested pits by scaling revenue factor
    std::vector<PhaseMetrics> generatePhases(BlockGrid& grid, 
                                             const PitOptimizationParams& baseParams,
                                             float minRF = 0.5f, 
                                             int numSteps = 6,
                                             std::function<void(float)> progress = nullptr);

    // Schedule blocks marked in phases respecting mining vs milling capacities
    void scheduleMining(BlockGrid& grid, 
                        const PitOptimizationParams& econ,
                        const SchedulingParams& schedParams,
                        CashFlowAnalysis& cfa,
                        std::function<void(float)> progress = nullptr);
};
