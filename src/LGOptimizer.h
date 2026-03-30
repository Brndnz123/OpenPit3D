// LGOptimizer.h
#pragma once
#include <vector>
#include <atomic>
#include <functional>
#include "Types.h"

// ─────────────────────────────────────────────────────────────────────────────
//  PitOptimizationParams
// ─────────────────────────────────────────────────────────────────────────────

struct PitOptimizationParams {
    // Overall pit slope angle (degrees from horizontal).
    // Used to compute the precedence cone: to mine a block at bench iy,
    // you must first mine all blocks within the cone above it.
    float slopeAngleDeg = 45.f;

    // Minimum ore grade that is economically worth mining (cut-off grade, %).
    // Blocks below this are assigned a stripping cost even if grade > 0.
    float cutOffGrade = 0.5f;

    // Economics (per tonne)
    float oreValuePerTonne   = 45.f;   // Revenue per % grade per tonne
    float miningCostPerTonne =  8.f;   // Total mining cost (ore + waste)
    float processingCostPerTonne = 12.f;
    float blockTonnes        = 8000.f; // Tonnes per 20x20x20m block (density ~1t/m³)

    // Maximum number of passes (iterations) for the LG parametric search.
    // Typically 1 pass is sufficient for the basic closure; increase for
    // revenue factor sensitivity analysis.
    int   maxPasses = 1;
};

// ─────────────────────────────────────────────────────────────────────────────
//  LGOptimizer — Lerchs-Grossmann Open Pit Optimisation
//
//  Algorithm
//  ─────────
//  We solve the Maximum Weight Closure problem on the pit precedence graph
//  using a min-cut / max-flow formulation (Picard & Queyranne, 1982).
//
//  Graph construction:
//    • Source S  → each block i with value v_i > 0  (capacity v_i)
//    • Each block i with value v_i < 0 → Sink T     (capacity |v_i|)
//    • Each precedence arc i→j                      (capacity ∞)
//
//  The min-cut separates blocks into "in the pit" (reachable from S after cut)
//  and "left out".  The cut value equals the total value sacrificed.
//
//  Max-flow is computed with a push-relabel algorithm (Goldberg-Tarjan, 1988),
//  which runs in O(V²√E) — practical for block models up to ~500k blocks.
//
//  Precedence Cone
//  ───────────────
//  For slope angle θ and block size b, to mine block (ix, iy, iz) you must
//  first mine all blocks in bench iy-1 that lie within horizontal distance
//    r = b / tan(θ)
//  of the block's column.  This generalises to all benches above.
//
//  Progress reporting
//  ──────────────────
//  Pass a callback to optimize() to receive [0,1] progress updates that can
//  drive an ImGui progress bar on the UI thread.
// ─────────────────────────────────────────────────────────────────────────────

class LGOptimizer {
public:
    LGOptimizer()  = default;
    ~LGOptimizer() = default;

    // ── Main entry point ──────────────────────────────────────────────────────
    //  Operates directly on grid.blocks, marking IN_PIT / DISCARDED.
    //  progressCallback(fraction) is called from the worker thread — must be
    //  thread-safe (typically writes to a std::atomic<float>).
    void optimize(BlockGrid&                        grid,
                  const PitOptimizationParams&      params,
                  std::function<void(float)>        progressCallback = nullptr);

    // ── Value calculation (also called by UI to preview economics) ────────────
    //  Sets block.value for all blocks in the grid.
    static void computeBlockValues(BlockGrid& grid,
                                   const PitOptimizationParams& params);

    // ── Results ───────────────────────────────────────────────────────────────
    double totalPitValue()    const { return totalPitValue_; }
    int    blocksInPit()      const { return blocksInPit_;   }
    int    wasteBlocksInPit() const { return wasteInPit_;    }
    double strippingRatio()   const {
        return (blocksInPit_ > 0)
             ? (double)wasteInPit_ / std::max(1, blocksInPit_ - wasteInPit_)
             : 0.0;
    }

private:
    double totalPitValue_ = 0.0;
    int    blocksInPit_   = 0;
    int    wasteInPit_    = 0;

    // ── Internal graph structures ─────────────────────────────────────────────
    struct Arc {
        int   to;
        float cap;     // Residual capacity
        float origCap; // Original capacity (for reset)
        int   rev;     // Index of reverse arc in adjacency list of node `to`
    };

    struct Node {
        int             height = 0;
        float           excess = 0.f;
        std::vector<Arc> arcs;
        int             current = 0;  // Current arc pointer (gap heuristic)
    };

    std::vector<Node> graph_;
    int               source_ = 0;
    int               sink_   = 0;
    int               numNodes_ = 0;

    void  buildGraph(const BlockGrid& grid, const PitOptimizationParams& params);
    float pushRelabel();                         // Returns max-flow value
    void  pushFlow(int u, Arc& arc, float delta);
    void  relabel(int u);
    void  initPreflow();
    void  discharge(int u);

    // Precedence cone: returns flat indices of blocks in bench iy-1
    // that must be mined before block (ix, iy, iz).
    std::vector<int> precedenceSet(const BlockGrid& grid,
                                   int ix, int iy, int iz,
                                   float slopeAngleDeg) const;

    static constexpr float INF_CAP = 1e12f;   // "Infinity" for precedence arcs
};
