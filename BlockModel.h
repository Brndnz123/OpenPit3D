#pragma once
#include <vector>
#include <unordered_map>
#include <tuple>
#include <cstdint>
#include <cmath>
#include <string>
#include <glm/glm.hpp>
#include "Types.h"
#include "PitGenerator.h"
#include "DrillholeDatabase.h"
#include "AppState.h"

// ── Voksel uzamsal hash indeksi ───────────────────────────────────────────────
class SpatialIndex {
public:
    explicit SpatialIndex(float cellSize) : _cs(cellSize) {}
    void insert(int sampleIdx, glm::vec3 pos);
    std::vector<int> query(glm::vec3 pos, int r = 1) const;
private:
    float _cs;
    std::unordered_map<uint64_t, std::vector<int>> _cells;
    uint64_t key(int cx, int cy, int cz) const {
        return ((uint64_t)(cx + 10000) << 42) |
               ((uint64_t)(cy + 10000) << 21) |
               ((uint64_t)(cz + 10000));
    }
    std::tuple<int, int, int> toCell(glm::vec3 p) const {
        return { (int)std::floor(p.x / _cs),
                 (int)std::floor(p.y / _cs),
                 (int)std::floor(p.z / _cs) };
    }
};

// ── BlockModel ────────────────────────────────────────────────────────────────
class BlockModel {
public:
    BlockModel(int nx, int ny, int nz, float blockSize);

    // Veri yüklendikten sonra grid yeniden boyutlandırılabilir.
    // Mevcut blokları siler ve yeni grid oluşturur.
    void reinitialize(int nx, int ny, int nz, float blockSize);

    // Tenör kestirim:
    //   IDW           — Inverse Distance Weighting (OpenMP ile paralel)
    //   NEAREST_NEIGHBOR — En yakın örnek (hızlı, kaba)
    // Kullanılacak yöntem AppState::opt.estimMethod ile belirlenir.
    void estimateFromDatabase(const DrillholeDatabase& db,
                              const EconomicParams&    econ,
                              const PitOptimizationParams& opt);

    // CSV raporu (sadece IN_PIT ve MINED bloklar)
    bool generateCSVReport(const std::string& filepath,
                           const EconomicParams& econ,
                           float rockDensity = 2.7f) const;

    const std::vector<Block>& getBlocks() const { return _blocks; }
    std::vector<Block>&       getBlocks()       { return _blocks; }

    int   nx()        const { return _nx; }
    int   ny()        const { return _ny; }
    int   nz()        const { return _nz; }
    float blockSize() const { return _blockSize; }

    void resetStates();

private:
    std::vector<Block> _blocks;
    int   _nx, _ny, _nz;
    float _blockSize;

    void buildGrid();   // _blocks vektörünü nx*ny*nz blokla doldurur

    void runIDW(const std::vector<WorldPoint>& samples,
                const EconomicParams& econ,
                float searchRadius, int power);

    void runNearestNeighbor(const std::vector<WorldPoint>& samples,
        const EconomicParams& econ);

    void runKriging(const std::vector<WorldPoint>& samples,
        const EconomicParams& econ,
        const PitOptimizationParams& opt);

    static constexpr float IDW_POWER   = 2.f;
    static constexpr float SEARCH_MULT = 6.f;
};
