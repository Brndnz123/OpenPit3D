#include "LGOptimizer.h"
#include <queue>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cmath>
#include <glm/glm.hpp>

void LGOptimizer::addEdge(int u, int v, float cap) {
    _graph[u].push_back({ v, (int)_graph[v].size(), cap });
    _graph[v].push_back({ u, (int)_graph[u].size() - 1, 0.f });
}

bool LGOptimizer::bfs() {
    _level.assign(_numNodes, -1);
    std::queue<int> q;
    _level[_S] = 0; q.push(_S);
    while (!q.empty()) {
        int v = q.front(); q.pop();
        for (const auto& e : _graph[v])
            if (e.cap > 1e-7f && _level[e.to] < 0) {
                _level[e.to] = _level[v] + 1;
                q.push(e.to);
            }
    }
    return _level[_T] >= 0;
}

float LGOptimizer::dfs(int v, float pushed) {
    if (v == _T || pushed < 1e-7f) return pushed;
    for (int& i = _iter[v]; i < (int)_graph[v].size(); ++i) {
        Edge& e = _graph[v][i];
        if (e.cap < 1e-7f || _level[e.to] != _level[v] + 1) continue;
        float d = dfs(e.to, std::min(pushed, e.cap));
        if (d > 1e-7f) {
            e.cap -= d;
            _graph[e.to][e.rev].cap += d;
            return d;
        }
    }
    return 0.f;
}

void LGOptimizer::build(std::vector<Block>& blocks,
    int nx, int ny, int nz,
    float blockSize,
    const std::vector<SlopeRegion>& slopeRegions) {
    const int N = (int)blocks.size();
    _numNodes = N + 2; _S = N; _T = N + 1;
    _graph.assign(_numNodes, {});
    _positiveSum = 0.0;

    auto idx = [&](int bx, int by, int bz) -> int {
        return bx + bz * nx + by * nx * nz;
        };

    float INF = 0.f;
    for (const auto& b : blocks) if (b.value > 0.f) INF += b.value;
    INF = std::max(INF, 1.f) * 10.f;

    long edgeCount = 0, depCount = 0;

    // 1. Düğüm değerlerini (S-T) grafiğe ekle
    for (int i = 0; i < N; ++i) {
        const Block& b = blocks[i];
        if (b.value > 0.f) {
            addEdge(_S, b.id, b.value);
            _positiveSum += b.value;
        }
        else {
            addEdge(b.id, _T, -b.value);
        }
        ++edgeCount;
    }

    // 2. Koni Bağımlılıkları (Eliptik)
    float minAngle = 85.f;
    for (const auto& r : slopeRegions) {
        minAngle = std::min({ minAngle, r.angleN, r.angleS, r.angleE, r.angleW });
    }
    double minAlphaRad = std::max(10.f, minAngle) * (3.1415926535f / 180.0);
    double maxR_metre = blockSize / std::tan(minAlphaRad);
    const int scanRadius = (int)std::ceil(maxR_metre / blockSize) + 1;

    for (int i = 0; i < N; ++i) {
        const Block& b = blocks[i];
        if (b.iy == 0) continue;

        const int iy_upper = b.iy - 1;

        float currentDepth = b.iy * blockSize;
        const SlopeRegion* activeReg = &slopeRegions.front();
        for (const auto& reg : slopeRegions) {
            if (currentDepth >= reg.fromDepth && currentDepth <= reg.toDepth) {
                activeReg = &reg;
                break;
            }
        }

        auto calcR = [&](float angleDeg) {
            float rad = std::clamp(angleDeg, 10.f, 85.f) * (3.1415926535f / 180.f);
            return blockSize / std::tan(rad);
            };

        float Rx_east = calcR(activeReg->angleE);
        float Rx_west = calcR(activeReg->angleW);
        float Rz_south = calcR(activeReg->angleS);
        float Rz_north = calcR(activeReg->angleN);

        for (int dix = -scanRadius; dix <= scanRadius; ++dix) {
            for (int diz = -scanRadius; diz <= scanRadius; ++diz) {
                if (dix == 0 && diz == 0) {
                    addEdge(b.id, idx(b.ix, iy_upper, b.iz), INF);
                    ++edgeCount; ++depCount;
                    continue;
                }

                float Rx = (dix > 0) ? Rx_east : Rx_west;
                float Rz = (diz > 0) ? Rz_south : Rz_north;

                float dx_norm = (dix * blockSize) / Rx;
                float dz_norm = (diz * blockSize) / Rz;

                if ((dx_norm * dx_norm + dz_norm * dz_norm) <= 1.0f + 1e-6f) {
                    int ax = b.ix + dix;
                    int az = b.iz + diz;
                    if (ax < 0 || ax >= nx || az < 0 || az >= nz) continue;

                    addEdge(b.id, idx(ax, iy_upper, az), INF);
                    ++edgeCount; ++depCount;
                }
            }
        }
    }

    std::cout << "[LG] Dugum: " << _numNodes
        << "  Kenar: " << edgeCount
        << "  Bagimlilik: " << depCount
        << "  scanR=" << scanRadius << " blok\n"
        << "[LG] Pozitif toplam = " << _positiveSum << "\n";
}

double LGOptimizer::solve() {
    auto t0 = std::chrono::high_resolution_clock::now();
    _maxFlow = 0.0;
    while (bfs()) {
        _iter.assign(_numNodes, 0);
        while (true) {
            float f = dfs(_S, (float)_positiveSum * 10.f);
            if (f < 1e-7f) break;
            _maxFlow += f;
        }
    }
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t0).count();
    std::cout << "[LG] Akis=" << _maxFlow
        << "  Optimal=" << optimalValue()
        << "  Sure=" << ms << "ms\n";
    return _maxFlow;
}

void LGOptimizer::applyResult(std::vector<Block>& blocks) {
    std::vector<bool> reach(_numNodes, false);
    std::queue<int> q;
    q.push(_S); reach[_S] = true;
    while (!q.empty()) {
        int v = q.front(); q.pop();
        for (const auto& e : _graph[v])
            if (!reach[e.to] && e.cap > 1e-7f) {
                reach[e.to] = true;
                q.push(e.to);
            }
    }
    int nIn = 0, nOut = 0;
    for (auto& b : blocks) {
        if (reach[b.id]) { b.state = LGState::IN_PIT;    ++nIn; }
        else { b.state = LGState::DISCARDED;  ++nOut; }
    }
    std::cout << "[LG] In-pit: " << nIn << "  Discarded: " << nOut << "\n";
}

void LGOptimizer::optimize(std::vector<Block>& blocks,
    int nx, int ny, int nz,
    float blockSize,
    const std::vector<SlopeRegion>& slopeRegions) {
    build(blocks, nx, ny, nz, blockSize, slopeRegions);
    solve();
    applyResult(blocks);
}