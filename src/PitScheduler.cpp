#include "PitScheduler.h"
#include <algorithm>

std::vector<PhaseMetrics> PitScheduler::generatePhases(BlockGrid& grid, 
                                                       const PitOptimizationParams& baseParams,
                                                       float minRF, 
                                                       int numSteps,
                                                       std::function<void(float)> progress) 
{
    std::vector<PhaseMetrics> phases;
    float rfStep = (1.0f - minRF) / std::max(1, numSteps - 1);
    
    for (int i = 0; i < grid.totalBlocks(); ++i) {
        grid.blocks[i].phase = -1;
    }

    for (int step = 0; step < numSteps; ++step) {
        float rf = minRF + step * rfStep;
        if (step == numSteps - 1) rf = 1.0f; 
        
        PitOptimizationParams p = baseParams;
        p.oreValuePerTonne *= rf;

        LGOptimizer lg;
        lg.optimize(grid, p, nullptr);

        PhaseMetrics pm;
        pm.phaseNumber = step + 1;
        pm.revenueFactor = rf;
        pm.blocksTotal = 0;
        pm.oreBlocks = 0;
        pm.totalValue = 0.f;

        for (int i = 0; i < grid.totalBlocks(); ++i) {
            Block& b = grid.blocks[i];
            if (b.state == LGState::IN_PIT) {
                if (b.phase == -1) {
                    b.phase = pm.phaseNumber;
                }
                if (b.phase == pm.phaseNumber) {
                    pm.blocksTotal++;
                    if (b.grade >= p.cutOffGrade) pm.oreBlocks++;
                    pm.totalValue += b.value; // Store the subset value
                }
            }
        }
        
        if (pm.blocksTotal > 0) {
            phases.push_back(pm);
        }
        if (progress) progress((float)(step + 1) / numSteps);
    }
    return phases;
}

void PitScheduler::scheduleMining(BlockGrid& grid, 
                                  const PitOptimizationParams& econ,
                                  const SchedulingParams& schedParams,
                                  CashFlowAnalysis& cfa,
                                  std::function<void(float)> progress) 
{
    int maxPhase = 0;
    for (int i = 0; i < grid.totalBlocks(); ++i) {
        grid.blocks[i].mineYear = -1;
        if (grid.blocks[i].phase > maxPhase) maxPhase = grid.blocks[i].phase;
    }

    int currentYear = 1;
    float minedThisYear = 0.f;
    float milledThisYear = 0.f;
    double revThisYear = 0.0;
    double opexThisYear = 0.0;

    cfa.clear();

    std::vector<double> revenues;
    std::vector<double> opexes;

    auto advanceYear = [&]() {
        currentYear++;
        minedThisYear = 0.f;
        milledThisYear = 0.f;
        revThisYear = 0.0;
        opexThisYear = 0.0;
    };

    int totalBlocksToMine = 0;
    for (int i = 0; i < grid.totalBlocks(); ++i) {
        if (grid.blocks[i].phase > 0) totalBlocksToMine++;
    }
    
    int minedCount = 0;

    for (int p = 1; p <= maxPhase; ++p) {
        std::vector<int> phaseBlocks;
        for (int i = 0; i < grid.totalBlocks(); ++i) {
            if (grid.blocks[i].phase == p) phaseBlocks.push_back(i);
        }
        
        // Sort bench-by-bench (top-down) greedy scheduling
        std::sort(phaseBlocks.begin(), phaseBlocks.end(), [&](int a, int b) {
            return grid.blocks[a].iy < grid.blocks[b].iy;
        });

        for (int bIdx : phaseBlocks) {
            Block& b = grid.blocks[bIdx];
            bool isOre = b.grade >= econ.cutOffGrade;
            float blockTonnes = econ.blockTonnes;
            
            // Increment year if capacity hit
            if (minedThisYear + blockTonnes > schedParams.maxMiningCapacityPerYear ||
               (isOre && milledThisYear + blockTonnes > schedParams.maxMillingCapacityPerYear)) {   
                revenues.push_back(revThisYear);
                opexes.push_back(opexThisYear);
                advanceYear();
            }

            b.mineYear = currentYear;
            b.state = LGState::MINED;
            minedThisYear += blockTonnes;
            
            float miningCost = blockTonnes * econ.miningCostPerTonne;
            float procCost = isOre ? (blockTonnes * econ.processingCostPerTonne) : 0.f;
            float rev = isOre ? (b.grade * econ.oreValuePerTonne * blockTonnes) : 0.f;
            
            if (isOre) milledThisYear += blockTonnes;
            
            revThisYear += rev;
            opexThisYear += (miningCost + procCost);
            
            minedCount++;
            if (progress && (minedCount % 1000 == 0)) {
                progress((float)minedCount / totalBlocksToMine);
            }
        }
    }

    if (minedThisYear > 0) {
        revenues.push_back(revThisYear);
        opexes.push_back(opexThisYear);
    }

    cfa.setAnnualRevenues(revenues);
    cfa.setAnnualOpex(opexes);
    cfa.generate();
    
    if (progress) progress(1.0f);
}
