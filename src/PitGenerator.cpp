// PitGenerator.cpp
#include "PitGenerator.h"
#include <algorithm>
#include <limits>

// ─────────────────────────────────────────────────────────────────────────────
//  Color helpers
// ─────────────────────────────────────────────────────────────────────────────

glm::vec4 PitGenerator::benchColor(int bench, int total) {
    float t = (total > 1) ? std::clamp((float)bench / (float)(total - 1), 0.f, 1.f)
                          : 0.f;
    // Surface (bench 0) → sky blue; deepest bench → warm orange
    glm::vec3 top   (0.35f, 0.65f, 0.85f);   // light blue
    glm::vec3 mid   (0.75f, 0.75f, 0.60f);   // sandy
    glm::vec3 deep  (0.80f, 0.42f, 0.18f);   // terracotta

    glm::vec3 c = (t < 0.5f)
        ? glm::mix(top,  mid,  t * 2.f)
        : glm::mix(mid,  deep, (t - 0.5f) * 2.f);
    return glm::vec4(c, 1.f);
}

glm::vec4 PitGenerator::topoColor(float h) {
    h = std::clamp(h, 0.f, 1.f);
    // Low → dark grey rock; high → muted green grass
    glm::vec3 rock (0.40f, 0.38f, 0.36f);
    glm::vec3 grass(0.45f, 0.55f, 0.35f);
    return glm::vec4(glm::mix(rock, grass, h), 0.80f);   // Slightly transparent
}

// ─────────────────────────────────────────────────────────────────────────────
//  emitQuad
//
//  Adds two CCW triangles for a quad face.  Vertices should be supplied in
//  CCW winding order as seen from outside the pit:
//
//   v0 --- v3
//   |   ╲  |
//   |    ╲ |
//   v1 --- v2
//
//  Triangle 1: v0 v1 v2
//  Triangle 2: v0 v2 v3
//
//  The face normal is computed as cross(v1-v0, v2-v0) and applied to all
//  four vertices.  computeNormals() will later smooth-average these if called.
// ─────────────────────────────────────────────────────────────────────────────

void PitGenerator::emitQuad(MeshModel& mesh,
                             const glm::vec3& v0, const glm::vec3& v1,
                             const glm::vec3& v2, const glm::vec3& v3,
                             const glm::vec4& color)
{
    glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v3 - v0));

    GLuint base = (GLuint)mesh.vertices.size();
    mesh.vertices.push_back({ v0, n, color });
    mesh.vertices.push_back({ v1, n, color });
    mesh.vertices.push_back({ v2, n, color });
    mesh.vertices.push_back({ v3, n, color });

    // Triangle 1
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    // Triangle 2
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

// ─────────────────────────────────────────────────────────────────────────────
//  generateTerrain
//
//  For each block in the grid we check its 6 neighbours.  A face is emitted
//  when:
//    • The block is IN_PIT AND the neighbour is DISCARDED, ACTIVE, or absent
//      (grid boundary).  This traces the exact pit shell surface.
//    • For the top face (+Y direction) of any IN_PIT block whose bench is 0
//      (surface bench) we also check upward so the rim is closed.
//
//  Face orientation convention:
//    Normals point FROM the pit INTO the surrounding rock — i.e. outward from
//    the excavated volume.  This matches standard mine survey convention and
//    gives correct back-face culling when GL_CULL_FACE is enabled.
//
//  Topo surface:
//    For each (ix, iz) column, the highest block's top-face centre gives the
//    surface elevation.  We build a connected quad grid so normals interpolate
//    smoothly across the terrain.
// ─────────────────────────────────────────────────────────────────────────────

void PitGenerator::generateTerrain(const BlockGrid& grid,
                                    std::function<void(float)> progress)
{
    pitMesh.vertices.clear();
    pitMesh.indices.clear();
    topoMesh.vertices.clear();
    topoMesh.indices.clear();
    pitFaceCount_  = 0;
    topoFaceCount_ = 0;

    if (grid.totalBlocks() == 0) return;

    const float bx = grid.blockSizeX;
    const float by = grid.blockSizeY;
    const float bz = grid.blockSizeZ;

    // ── Pit shell ─────────────────────────────────────────────────────────────
    //
    //  Six face directions and their outward normal + quad vertex offsets.
    //  Each entry: { normal direction, four corner offsets in CCW order
    //               when viewed from outside }
    //
    //  We use half-block offsets from the block centre.

    struct FaceDesc {
        int   nx, ny, nz;           // Neighbour direction (+1 or -1 on one axis)
        glm::vec3 c[4];             // Corner offsets (CCW from outside)
    };

    // Half-extents
    const float hx = bx * 0.5f;
    const float hy = by * 0.5f;
    const float hz = bz * 0.5f;

    // Corners of a unit cube scaled by half-extents.
    // +X face (neighbour is ix+1): normal = +X, visible from +X side
    static const FaceDesc FACES[6] = {
        // +X
        { +1,0,0, {{ {+hx,-hy,-hz}, {+hx,+hy,-hz}, {+hx,+hy,+hz}, {+hx,-hy,+hz} }} },
        // -X
        { -1,0,0, {{ {-hx,-hy,+hz}, {-hx,+hy,+hz}, {-hx,+hy,-hz}, {-hx,-hy,-hz} }} },
        // +Y (top)
        {  0,+1,0, {{ {-hx,+hy,-hz}, {-hx,+hy,+hz}, {+hx,+hy,+hz}, {+hx,+hy,-hz} }} },
        // -Y (bottom / floor)
        {  0,-1,0, {{ {-hx,-hy,+hz}, {-hx,-hy,-hz}, {+hx,-hy,-hz}, {+hx,-hy,+hz} }} },
        // +Z
        {  0,0,+1, {{ {+hx,-hy,+hz}, {+hx,+hy,+hz}, {-hx,+hy,+hz}, {-hx,-hy,+hz} }} },
        // -Z
        {  0,0,-1, {{ {-hx,-hy,-hz}, {-hx,+hy,-hz}, {+hx,+hy,-hz}, {+hx,-hy,-hz} }} },
    };

    int total  = grid.totalBlocks();
    int processed = 0;

    for (int i = 0; i < total; ++i) {
        const Block& b = grid.blocks[i];
        if (b.state != LGState::IN_PIT) { ++processed; continue; }

        glm::vec4 col = benchColor(b.iy, grid.ny);

        for (const auto& face : FACES) {
            int ni = grid.index(b.ix + face.nx, b.iy + face.ny, b.iz + face.nz);
            bool neighbourIsOpen = false;
            if (ni < 0) {
                // Grid boundary — always emit
                neighbourIsOpen = true;
            } else {
                LGState ns = grid.blocks[ni].state;
                // Expose face when neighbour is outside the pit
                neighbourIsOpen = (ns == LGState::DISCARDED || ns == LGState::ACTIVE);
            }

            if (neighbourIsOpen) {
                glm::vec3 c = b.worldPos;
                emitQuad(pitMesh,
                         c + face.c[0], c + face.c[1],
                         c + face.c[2], c + face.c[3],
                         col);
                ++pitFaceCount_;
            }
        }

        if (progress && (processed % 5000 == 0))
            progress(0.1f + 0.6f * (float)processed / (float)total);
        ++processed;
    }

    if (progress) progress(0.70f);

    // ── Topographic surface ───────────────────────────────────────────────────
    //
    //  Build a height-field from the top face of the highest block in each
    //  (ix, iz) column.  Surface Y = worldPos.y + blockSizeY/2.
    //
    //  We then build a connected quad mesh over the NX×NZ grid so normals
    //  interpolate smoothly.

    // Find the world Y of the top face in each column
    std::vector<float> heightMap((size_t)grid.nx * grid.nz,
                                 std::numeric_limits<float>::lowest());

    for (const Block& b : grid.blocks) {
        size_t col = (size_t)b.ix + (size_t)b.iz * grid.nx;
        float topY = b.worldPos.y + hy;   // Top face of this block
        if (topY > heightMap[col]) heightMap[col] = topY;
    }

    // Fill any empty columns (no blocks) with the surface origin
    float surfaceY = grid.originY;
    for (auto& h : heightMap) if (h == std::numeric_limits<float>::lowest()) h = surfaceY;

    // Normalise heights for colour mapping
    float minH = *std::min_element(heightMap.begin(), heightMap.end());
    float maxH = *std::max_element(heightMap.begin(), heightMap.end());
    float rangeH = (maxH > minH) ? (maxH - minH) : 1.f;

    // Build vertex grid (nx × nz vertices, one per column corner)
    // We'll index them as [iz * nx + ix]
    auto topoBase = (GLuint)topoMesh.vertices.size();

    for (int iz = 0; iz < grid.nz; ++iz) {
        for (int ix = 0; ix < grid.nx; ++ix) {
            float h    = heightMap[(size_t)ix + (size_t)iz * grid.nx];
            float worldX = grid.originX + ix * bx;
            float worldZ = grid.originZ + iz * bz;
            float tNorm  = (h - minH) / rangeH;

            MeshVertex v;
            v.pos    = { worldX, h, worldZ };
            v.normal = { 0.f, 1.f, 0.f };   // computeNormals() will fix this
            v.color  = topoColor(tNorm);
            topoMesh.vertices.push_back(v);
        }
    }

    // Build indices — two triangles per quad cell
    for (int iz = 0; iz < grid.nz - 1; ++iz) {
        for (int ix = 0; ix < grid.nx - 1; ++ix) {
            GLuint i00 = topoBase + (GLuint)(iz     * grid.nx + ix    );
            GLuint i10 = topoBase + (GLuint)(iz     * grid.nx + ix + 1);
            GLuint i01 = topoBase + (GLuint)((iz+1) * grid.nx + ix    );
            GLuint i11 = topoBase + (GLuint)((iz+1) * grid.nx + ix + 1);

            // Triangle 1: top-left, bottom-left, bottom-right
            topoMesh.indices.push_back(i00);
            topoMesh.indices.push_back(i01);
            topoMesh.indices.push_back(i11);
            // Triangle 2: top-left, bottom-right, top-right
            topoMesh.indices.push_back(i00);
            topoMesh.indices.push_back(i11);
            topoMesh.indices.push_back(i10);
            ++topoFaceCount_;
        }
    }

    if (progress) progress(0.90f);

    // Compute smooth normals for both meshes
    pitMesh.computeNormals();
    topoMesh.computeNormals();

    if (progress) progress(1.0f);
}
