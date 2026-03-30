// LGOptimizer.cpp
#define _USE_MATH_DEFINES
#include "LGOptimizer.h"
#include <cmath>
#include <numeric>    // FIX 8: was included twice
#include <algorithm>
#include <queue>
#include <iostream>
#include <omp.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void LGOptimizer::computeBlockValues(BlockGrid& grid, const PitOptimizationParams& p) {
    #pragma omp parallel for schedule(static) default(none) shared(grid, p)
    for (int i = 0; i < grid.totalBlocks(); ++i) {
        Block& b = grid.blocks[i];
        if (b.grade >= p.cutOffGrade) {
            float revenue   = b.grade * p.oreValuePerTonne * p.blockTonnes;
            float totalCost = (p.miningCostPerTonne + p.processingCostPerTonne) * p.blockTonnes;
            b.value = revenue - totalCost;
        } else {
            b.value = -p.miningCostPerTonne * p.blockTonnes;
        }
    }
}

std::vector<int> LGOptimizer::precedenceSet(const BlockGrid& grid,
                                             int ix, int iy, int iz,
                                             float baseSlopeAngleDeg) const
{
    std::vector<int> result;
    if (iy <= 0) return result;

    const float minSlope = std::min(35.f, baseSlopeAngleDeg);
    const float maxTan   = std::tan(minSlope * (float)M_PI / 180.f);
    if (maxTan < 1e-4f) return result;

    float maxReachX = grid.blockSizeY / (maxTan * grid.blockSizeX);
    float maxReachZ = grid.blockSizeY / (maxTan * grid.blockSizeZ);
    int rx = std::max(0, (int)std::ceil(maxReachX));
    int rz = std::max(0, (int)std::ceil(maxReachZ));

    int upperBench = iy - 1;
    for (int dz = -rz; dz <= rz; ++dz)
    for (int dx = -rx; dx <= rx; ++dx) {
        int predIdx = grid.index(ix+dx, upperBench, iz+dz);
        if (predIdx < 0) continue;

        float slopeAngleDeg = baseSlopeAngleDeg;
        if (dx != 0 || dz != 0) {
            float bearing = std::atan2((float)dx, (float)dz) * 180.f / (float)M_PI;
            if (bearing < 0) bearing += 360.f;
            if (bearing > 315.f || bearing <= 45.f)
                slopeAngleDeg = std::max(15.f, baseSlopeAngleDeg - 10.f);
            else if (bearing > 135.f && bearing <= 225.f)
                slopeAngleDeg = std::min(75.f, baseSlopeAngleDeg + 15.f);
        }

        float tanSlope = std::tan(slopeAngleDeg * (float)M_PI / 180.f);
        float reachX = grid.blockSizeY / (tanSlope * grid.blockSizeX);
        float reachZ = grid.blockSizeY / (tanSlope * grid.blockSizeZ);

        float ex = (float)dx / (reachX + 0.5f);
        float ez = (float)dz / (reachZ + 0.5f);
        if (ex*ex + ez*ez <= 1.0f)
            result.push_back(predIdx);
    }
    return result;
}

void LGOptimizer::buildGraph(const BlockGrid& grid, const PitOptimizationParams& params) {
    int N = grid.totalBlocks();
    numNodes_ = N + 2; source_ = 0; sink_ = 1;
    graph_.assign(numNodes_, Node{});

    auto addArc = [&](int u, int v, float cap) {
        int fwd = (int)graph_[u].arcs.size();
        int rev = (int)graph_[v].arcs.size();
        graph_[u].arcs.push_back({ v, cap, cap, rev });
        graph_[v].arcs.push_back({ u, 0.f, 0.f, fwd });
    };

    for (int i = 0; i < N; ++i) {
        float v = grid.blocks[i].value;
        int   node = i + 2;
        if (v > 0.f)       addArc(source_, node,  v);
        else if (v < 0.f)  addArc(node,   sink_, -v);
    }

    for (int i = 0; i < grid.totalBlocks(); ++i) {
        const Block& b = grid.blocks[i];
        int blockNode = i + 2;
        auto preds = precedenceSet(grid, b.ix, b.iy, b.iz, params.slopeAngleDeg);
        for (int predIdx : preds)
            addArc(blockNode, predIdx + 2, INF_CAP);
    }
}

void LGOptimizer::initPreflow() {
    for (auto& n : graph_) { n.height = 0; n.excess = 0.f; n.current = 0; }
    graph_[source_].height = numNodes_;
    for (auto& arc : graph_[source_].arcs) {
        if (arc.cap <= 0.f) continue;
        float delta = arc.cap; arc.cap = 0.f;
        graph_[arc.to].arcs[arc.rev].cap += delta;
        graph_[arc.to].excess  += delta;
        graph_[source_].excess -= delta;
    }
}

void LGOptimizer::pushFlow(int u, Arc& arc, float delta) {
    arc.cap -= delta;
    graph_[arc.to].arcs[arc.rev].cap += delta;
    graph_[u].excess      -= delta;
    graph_[arc.to].excess += delta;
}

void LGOptimizer::relabel(int u) {
    int minH = 2 * numNodes_;
    for (const auto& arc : graph_[u].arcs)
        if (arc.cap > 1e-9f) minH = std::min(minH, graph_[arc.to].height + 1);
    graph_[u].height = minH;
}

void LGOptimizer::discharge(int u) {
    while (graph_[u].excess > 1e-9f) {
        auto& arcs = graph_[u].arcs;
        int&  cur  = graph_[u].current;
        if (cur >= (int)arcs.size()) { relabel(u); cur = 0; }
        else {
            Arc& arc = arcs[cur];
            if (arc.cap > 1e-9f && graph_[u].height == graph_[arc.to].height + 1) {
                float delta = std::min(graph_[u].excess, arc.cap);
                pushFlow(u, arc, delta);
            } else ++cur;
        }
    }
}

float LGOptimizer::pushRelabel() {
    initPreflow();
    auto cmp = [&](int a, int b){ return graph_[a].height < graph_[b].height; };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);
    for (int u = 2; u < numNodes_; ++u)
        if (graph_[u].excess > 1e-9f) pq.push(u);
    while (!pq.empty()) {
        int u = pq.top(); pq.pop();
        if (graph_[u].excess <= 1e-9f) continue;
        if (u == source_ || u == sink_) continue;
        int oldH = graph_[u].height;
        discharge(u);
        if (graph_[u].height != oldH)
            for (const auto& arc : graph_[u].arcs)
                if (arc.cap > 0.f && arc.to != source_ && arc.to != sink_)
                    if (graph_[arc.to].excess > 1e-9f) pq.push(arc.to);
        if (graph_[u].excess > 1e-9f) pq.push(u);
    }
    return graph_[sink_].excess;
}

void LGOptimizer::optimize(BlockGrid& grid, const PitOptimizationParams& params,
                            std::function<void(float)> progress)
{
    if (progress) progress(0.02f);
    computeBlockValues(grid, params);
    if (progress) progress(0.08f);
    buildGraph(grid, params);
    if (progress) progress(0.10f);
    pushRelabel();
    if (progress) progress(0.85f);

    std::vector<bool> visited(numNodes_, false);
    std::queue<int>   bfsQ;
    bfsQ.push(source_); visited[source_] = true;
    while (!bfsQ.empty()) {
        int u = bfsQ.front(); bfsQ.pop();
        for (const auto& arc : graph_[u].arcs)
            if (!visited[arc.to] && arc.cap > 1e-9f)
                { visited[arc.to] = true; bfsQ.push(arc.to); }
    }

    totalPitValue_ = 0.0; blocksInPit_ = 0; wasteInPit_ = 0;
    for (int i = 0; i < grid.totalBlocks(); ++i) {
        Block& b = grid.blocks[i];
        if (visited[i + 2]) {
            b.state = LGState::IN_PIT;
            totalPitValue_ += b.value;
            ++blocksInPit_;
            if (b.value < 0.f) ++wasteInPit_;
        } else {
            b.state = LGState::DISCARDED;
        }
    }
    if (progress) progress(1.0f);
}
