#pragma once
#include <vector>
#include "Types.h"
#include "AppState.h" // SlopeRegion tanımı için gerekli

class LGOptimizer {
public:
    void optimize(std::vector<Block>& blocks,
        int nx, int ny, int nz,
        float blockSize,
        const std::vector<SlopeRegion>& slopeRegions);

    void build(std::vector<Block>& blocks,
        int nx, int ny, int nz,
        float blockSize,
        const std::vector<SlopeRegion>& slopeRegions);

    double solve();
    void applyResult(std::vector<Block>& blocks);

    double optimalValue() const { return _positiveSum - _maxFlow; }
    double positiveSum()  const { return _positiveSum; }
    double maxFlow()      const { return _maxFlow; }

private:
    struct Edge { int to, rev; float cap; };

    int _numNodes = 0, _S = 0, _T = 0;
    std::vector<std::vector<Edge>> _graph;
    std::vector<int>  _level, _iter;
    double _positiveSum = 0.0, _maxFlow = 0.0;

    void  addEdge(int u, int v, float cap);
    bool  bfs();
    float dfs(int v, float pushed);
};