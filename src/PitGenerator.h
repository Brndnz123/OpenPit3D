// PitGenerator.h
#pragma once
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include "Types.h"
#include "MeshModel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  PitGenerator
//
//  Extracts two triangle meshes from a BlockGrid after LG optimisation:
//
//  1. Pit shell mesh (pitMesh)
//     Triangulates every exposed face between an IN_PIT block and either
//     a DISCARDED block or the grid boundary.  This gives the walls, floor,
//     and rim of the open pit.  Vertices are coloured by bench depth (blue
//     at surface → orange at depth) so the benching is immediately visible.
//
//  2. Topographic surface mesh (topoMesh)
//     A height-field quad grid covering the full XZ extent of the block
//     model.  The surface elevation at each column (ix, iz) is set to the
//     top face of the highest block in that column, regardless of state.
//     This gives context for the surrounding terrain.  Uses a grey-green
//     colour ramp.
//
//  Both meshes are stored as MeshModel objects so the Renderer can draw them
//  with a single `renderMesh()` call each.
//
//  All geometry is generated on the CPU in O(N) time and is suitable for
//  grids up to ~10M blocks before memory becomes a concern.
// ─────────────────────────────────────────────────────────────────────────────

class PitGenerator {
public:
    PitGenerator() = default;

    // ── Main entry point ──────────────────────────────────────────────────────
    //  Clears and rebuilds both meshes from the current grid state.
    //  Call after LG optimisation completes (AppStage::PIT_SHELL).
    //  progressCallback(0..1) optional — safe to call from a worker thread.
    void generateTerrain(const BlockGrid&           grid,
                         std::function<void(float)> progress = nullptr);

    // ── Output meshes (ready to upload + draw) ────────────────────────────────
    MeshModel pitMesh;   // Pit shell — walls, floor, rim
    MeshModel topoMesh;  // Surrounding terrain surface

    // ── Stats ─────────────────────────────────────────────────────────────────
    int pitFaceCount()  const { return pitFaceCount_;  }
    int topoFaceCount() const { return topoFaceCount_; }

private:
    int pitFaceCount_  = 0;
    int topoFaceCount_ = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────

    // Depth-based bench colour (blue surface → orange deep)
    static glm::vec4 benchColor(int benchIndex, int totalBenches);

    // Terrain colour (green-grey height-field)
    static glm::vec4 topoColor(float normalizedHeight);

    // Emit a face quad as two triangles into a mesh.
    // verts: 4 coplanar positions in CCW order (as seen from outside).
    // color: face colour.
    static void emitQuad(MeshModel& mesh,
                         const glm::vec3& v0, const glm::vec3& v1,
                         const glm::vec3& v2, const glm::vec3& v3,
                         const glm::vec4& color);
};
