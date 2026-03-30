// Types.h
#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <unordered_map>
#include <utility>

enum class LGState : uint8_t { ACTIVE, IN_PIT, DISCARDED, MINED };
enum class BlockColorMode { FLAT, GRADE, ECONOMIC };
enum class AppStage {
    EMPTY, DRILLHOLES, ESTIMATED, OPTIMIZING, PIT_SHELL
};
enum class ViewPreset { FREE, TOP, SIDE, SECTION };
enum class EstimationMethod { IDW, NEAREST_NEIGHBOR, KRIGING };

struct Block {
    int       id       = 0;
    int       ix       = 0, iy = 0, iz = 0;
    glm::vec3 worldPos{};
    float     grade    = 0.f;
    float     value    = 0.f;
    LGState   state    = LGState::ACTIVE;
    int       phase    = -1;
    int       mineYear = -1;
};

struct RenderSettings {
    bool  showWireframe  = false;
    bool  showBlocks     = true;
    bool  showDrillholes = true;
    bool  showGrid       = true;
    bool  showAxes       = true;
    float ambientStr     = 0.20f;
    float diffuseStr     = 0.80f;
};

// ── Morton Code ───────────────────────────────────────────────────────────────
inline uint64_t splitBy3(uint32_t a) {
    uint64_t x = a & 0x1fffff;
    x = (x | x << 32) & 0x1f00000000ffff;
    x = (x | x << 16) & 0x1f0000ff0000ff;
    x = (x | x << 8)  & 0x100f00f00f00f00f;
    x = (x | x << 4)  & 0x10c30c30c30c30c3;
    x = (x | x << 2)  & 0x1249249249249249;
    return x;
}
inline uint64_t encodeMorton(int x, int y, int z) {
    return splitBy3((uint32_t)x) | (splitBy3((uint32_t)y) << 1) | (splitBy3((uint32_t)z) << 2);
}

// ─────────────────────────────────────────────────────────────────────────────
//  BlockGrid — Sparse Spatial Hash Map
// ─────────────────────────────────────────────────────────────────────────────
struct BlockGrid {
    int   nx = 0, ny = 0, nz = 0;
    float blockSizeX = 20.f;
    float blockSizeY = 20.f;
    float blockSizeZ = 20.f;
    float originX = 0.f;
    float originY = 0.f;
    float originZ = 0.f;

    std::vector<Block>                    blocks;
    std::unordered_map<uint64_t, int>     spatialIndex;

    // FIX 6: clear() both containers before rebuild so re-allocation works
    void allocate(int x, int y, int z) {
        nx = x; ny = y; nz = z;
        blocks.clear();          // ← was missing
        spatialIndex.clear();    // ← was missing
        blocks.reserve((size_t)x * y * z);
        int id = 0;
        for (int iy = 0; iy < ny; ++iy)
        for (int iz = 0; iz < nz; ++iz)
        for (int ix = 0; ix < nx; ++ix)
            addBlock(id++, ix, iy, iz);
    }

    void addBlock(int id, int ix, int iy, int iz) {
        uint64_t code = encodeMorton(ix, iy, iz);
        if (spatialIndex.count(code)) return;
        Block b;
        b.id  = id; b.ix = ix; b.iy = iy; b.iz = iz;
        b.worldPos = glm::vec3(
            originX + (ix + 0.5f) * blockSizeX,
            originY - (iy + 0.5f) * blockSizeY,
            originZ + (iz + 0.5f) * blockSizeZ
        );
        b.state = LGState::ACTIVE;
        int newIdx = (int)blocks.size();
        blocks.push_back(std::move(b));
        spatialIndex[code] = newIdx;
    }

    inline bool valid(int ix, int iy, int iz) const {
        return ix >= 0 && ix < nx && iy >= 0 && iy < ny && iz >= 0 && iz < nz;
    }

    inline int index(int ix, int iy, int iz) const {
        if (!valid(ix, iy, iz)) return -1;
        auto it = spatialIndex.find(encodeMorton(ix, iy, iz));
        return (it != spatialIndex.end()) ? it->second : -1;
    }

    inline Block* at(int ix, int iy, int iz) {
        int i = index(ix, iy, iz);
        return (i >= 0) ? &blocks[i] : nullptr;
    }
    inline const Block* at(int ix, int iy, int iz) const {
        int i = index(ix, iy, iz);
        return (i >= 0) ? &blocks[i] : nullptr;
    }

    inline void coords(int flatIdx, int& ox, int& oy, int& oz) const {
        if (flatIdx >= 0 && flatIdx < (int)blocks.size()) {
            ox = blocks[flatIdx].ix; oy = blocks[flatIdx].iy; oz = blocks[flatIdx].iz;
        } else { ox = 0; oy = 0; oz = 0; }
    }

    std::vector<int> queryRadius(const glm::vec3& q, float radius) const {
        std::vector<int> result;
        int rx = (int)std::ceil(radius / blockSizeX);
        int ry = (int)std::ceil(radius / blockSizeY);
        int rz = (int)std::ceil(radius / blockSizeZ);
        int cx = (int)std::floor((q.x - originX) / blockSizeX);
        int cy = (int)std::floor((originY - q.y) / blockSizeY);
        int cz = (int)std::floor((q.z - originZ) / blockSizeZ);
        float r2 = radius * radius;
        for (int dy = -ry; dy <= ry; ++dy)
        for (int dz = -rz; dz <= rz; ++dz)
        for (int dx = -rx; dx <= rx; ++dx) {
            int idx = index(cx+dx, cy+dy, cz+dz);
            if (idx >= 0) {
                glm::vec3 d = blocks[idx].worldPos - q;
                if (glm::dot(d,d) <= r2) result.push_back(idx);
            }
        }
        return result;
    }

    int totalBlocks() const { return (int)blocks.size(); }
};
