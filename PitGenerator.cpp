#include "PitGenerator.h"
#include "BlockModel.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <glm/gtc/constants.hpp>

PitGenerator::PitGenerator(unsigned int seed) : _seed(seed) {}

// ─── Noise helpers ────────────────────────────────────────────────────────────

float PitGenerator::noise2D(float x, float y, int seed) const {
    float n = std::sin(x * 127.1f + y * 311.7f + seed * 74.3f) * 43758.5453f;
    return n - std::floor(n);
}

float PitGenerator::smoothNoise(float x, float y, int seed) const {
    int ix = (int)std::floor(x), iy = (int)std::floor(y);
    float fx = x - ix, fy = y - iy;
    float ux = fx * fx * (3.f - 2.f * fx);
    float uy = fy * fy * (3.f - 2.f * fy);
    float a = noise2D((float)ix, (float)iy, seed);
    float b = noise2D((float)(ix + 1), (float)iy, seed);
    float c = noise2D((float)ix, (float)(iy + 1), seed);
    float d = noise2D((float)(ix + 1), (float)(iy + 1), seed);
    return a + (b - a) * ux + (c - a) * uy + (d - a + a - b - c + b) * ux * uy;
}

float PitGenerator::fractalNoise(float x, float y, int oct, int seed) const {
    float val = 0, amp = 1, freq = 1, maxV = 0;
    for (int i = 0; i < oct; i++) {
        val += smoothNoise(x * freq, y * freq, seed + i * 100) * amp;
        maxV += amp; amp *= 0.5f; freq *= 2.1f;
    }
    return val / maxV;
}

float PitGenerator::pointToSegmentDistance(glm::vec2 p, glm::vec2 a,
    glm::vec2 b, float& t) const {
    glm::vec2 ab = b - a, ap = p - a;
    float proj = glm::dot(ap, ab), len = glm::dot(ab, ab);
    if (len == 0.f) { t = 0; return glm::length(ap); }
    t = std::clamp(proj / len, 0.f, 1.f);
    return glm::length(p - (a + t * ab));
}

// ─── Flat-shading helper ──────────────────────────────────────────────────────
// Converts a smooth (shared-vertex) mesh to flat-shaded by duplicating
// vertices per triangle and computing one face normal per triangle.

PitMeshData PitGenerator::toFlatShaded(const PitMeshData& raw) const {
    PitMeshData flat;
    flat.vertices.reserve(raw.indices.size());
    flat.indices.reserve(raw.indices.size());
    for (size_t i = 0; i < raw.indices.size(); i += 3) {
        Vertex v0 = raw.vertices[raw.indices[i]];
        Vertex v1 = raw.vertices[raw.indices[i + 1]];
        Vertex v2 = raw.vertices[raw.indices[i + 2]];
        glm::vec3 n = glm::normalize(
            glm::cross(v1.position - v0.position, v2.position - v0.position));
        v0.normal = v1.normal = v2.normal = n;
        unsigned int base = (unsigned int)flat.vertices.size();
        flat.vertices.push_back(v0);
        flat.vertices.push_back(v1);
        flat.vertices.push_back(v2);
        flat.indices.push_back(base);
        flat.indices.push_back(base + 1);
        flat.indices.push_back(base + 2);
    }
    return flat;
}

// ─── generateFromBlocks ───────────────────────────────────────────────────────
//
//  Builds a terrain mesh whose surface exactly follows the LG-optimized pit.
//
//  HEIGHTMAP ALGORITHM per (ix, iz) column:
//
//    Scan iy = 0 → ny-1 (surface downward).
//
//    A block is "solid" if its state is DISCARDED or ACTIVE.
//    A block is "open"  if its state is IN_PIT  or MINED.
//
//    Find the FIRST SOLID block in the column:
//      → Surface Y = top face of that block = worldPos.y + blockSize * 0.5
//
//    If the entire column is open (pit centre / shaft):
//      → Surface Y = model floor = -(ny * blockSize)
//
//    If the entire column is solid (outside pit):
//      → Surface Y = iy=0 top face = ground level (y ≈ 0)
//      → Add gentle fractal noise so the surrounding terrain looks natural.
//
//  COLOR CODING:
//    Outside pit (all solid)    → grass green  with subtle noise shading
//    Pit wall (transition zone) → rock tan      (first solid after open zone)
//    Pit floor (all open)       → dark grey
//
PitMeshData PitGenerator::generateFromBlocks(const BlockModel& bm) {
    const int   nx = bm.nx();
    const int   ny = bm.ny();
    const int   nz = bm.nz();
    const float bs = bm.blockSize();
    const auto& blocks = bm.getBlocks();

    // Block index formula — must match BlockModel constructor:
    //   id = ix + iz * nx + iy * nx * nz
    auto blockIdx = [&](int ix, int iy, int iz) -> int {
        return ix + iz * nx + iy * nx * nz;
        };

    // Pre-compute Y extents
    const float groundY = bs * 0.5f - bs * 0.5f;   // = 0.0  (iy=0 top face)
    const float floorY = -(float)ny * bs;

    std::vector<float>     heights(nx * nz);
    std::vector<glm::vec3> colors(nx * nz);

    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            const int ci = ix + iz * nx;

            float     surfaceY = floorY;          // default: open shaft
            glm::vec3 col = { 0.30f, 0.28f, 0.25f };  // dark grey = floor
            bool      foundSolid = false;
            bool      topSolid = false;           // iy=0 is solid = outside pit

            for (int iy = 0; iy < ny; ++iy) {
                const Block& b = blocks[blockIdx(ix, iy, iz)];
                const bool solid = (b.state == LGState::DISCARDED ||
                    b.state == LGState::ACTIVE);

                if (solid) {
                    // First solid block = surface boundary
                    surfaceY = b.worldPos.y + bs * 0.5f;
                    foundSolid = true;
                    if (iy == 0) topSolid = true;
                    break;
                }
                // else: IN_PIT or MINED = open space, continue scanning down
            }

            if (!foundSolid) {
                // Entire column is open — pit centre / vertical shaft
                surfaceY = floorY;
                col = { 0.28f, 0.26f, 0.22f };  // dark pit floor
            }
            else if (topSolid) {
                // iy=0 is solid: column is entirely outside the pit.
                // Add gentle fractal noise so surrounding terrain undulates
                // naturally rather than sitting at a flat mathematical zero.
                const Block& top = blocks[blockIdx(ix, 0, iz)];
                float noise = fractalNoise(
                    top.worldPos.x * 0.005f,
                    top.worldPos.z * 0.005f,
                    4, _seed + 20) * 12.0f;
                surfaceY += noise;
                // Shade slightly by noise so it doesn't look completely flat
                float shade = 0.42f + noise * 0.004f;
                col = { shade * 0.90f, shade * 1.05f, shade * 0.82f };  // grassy
            }
            else {
                // Transition zone: some open blocks above the first solid.
                // This is the pit wall or bench face.
                // Color varies by depth to give a layered rock appearance.
                const Block& surfBlock = blocks[blockIdx(ix, 0, iz)];
                float depthFrac = std::clamp(
                    -surfaceY / ((float)ny * bs), 0.f, 1.f);
                col = glm::mix(
                    glm::vec3(0.70f, 0.62f, 0.50f),  // tan (shallow)
                    glm::vec3(0.45f, 0.38f, 0.30f),  // darker brown (deep)
                    depthFrac);
                // Suppress unused-variable warning on surfBlock
                (void)surfBlock;
            }

            heights[ci] = surfaceY;
            colors[ci] = col;
        }
    }

    // ── Build smooth mesh from heightmap ──────────────────────────────────────
    // Each (ix, iz) grid position becomes one vertex.
    // XZ position comes from the iy=0 block's world position in that column.

    PitMeshData raw;
    raw.vertices.resize(nx * nz);

    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            const int ci = ix + iz * nx;
            // Use iy=0 block for XZ reference (all blocks in column share XZ)
            const Block& ref = blocks[blockIdx(ix, 0, iz)];
            Vertex v;
            v.position = glm::vec3(ref.worldPos.x, heights[ci], ref.worldPos.z);
            v.normal = glm::vec3(0.f, 1.f, 0.f);  // replaced by toFlatShaded
            v.color = colors[ci];
            raw.vertices[ci] = v;
        }
    }

    // ── Triangulate as a regular quad grid ───────────────────────────────────
    for (int iz = 0; iz < nz - 1; ++iz) {
        for (int ix = 0; ix < nx - 1; ++ix) {
            unsigned int a = (unsigned int)(iz * nx + ix);
            unsigned int b = (unsigned int)(iz * nx + ix + 1);
            unsigned int c = (unsigned int)((iz + 1) * nx + ix);
            unsigned int d = (unsigned int)((iz + 1) * nx + ix + 1);
            // Two triangles per quad (CCW winding)
            raw.indices.push_back(a);
            raw.indices.push_back(b);
            raw.indices.push_back(c);
            raw.indices.push_back(c);
            raw.indices.push_back(b);
            raw.indices.push_back(d);
        }
    }

    PitMeshData result = toFlatShaded(raw);

    std::cout << "[PitGen] generateFromBlocks: "
        << result.vertices.size() << " verts from "
        << nx << "x" << nz << " block columns.\n";

    return result;
}

// ─── generateSurface (original parametric terrain) ───────────────────────────
// Used for the initial display before data is loaded.
// Unchanged from original.

PitMeshData PitGenerator::generateSurface(const PitParams& p, int resolution) {
    PitMeshData rawMesh;

    float faceAngleRad = p.faceAngle * glm::pi<float>() / 180.0f;
    float W_face = p.benchHeight / std::tan(faceAngleRad);
    float W_total = p.bermWidth + W_face;
    float D = p.benchCount * p.benchHeight;
    float maxRadius = p.bottomRadius + p.benchCount * W_total + 150.0f;
    float size = maxRadius * 2.0f;
    float step = size / (resolution - 1);

    rawMesh.vertices.resize(resolution * resolution);

    for (int i = 0; i < resolution; i++) {
        for (int j = 0; j < resolution; j++) {
            float x = -maxRadius + i * step;
            float z = -maxRadius + j * step;
            float rBase = std::sqrt(x * x + z * z);

            float n1 = fractalNoise(x * 0.003f, z * 0.003f, 4, _seed) * 2.f - 1.f;
            float n2 = fractalNoise(x * 0.01f, z * 0.01f, 3, _seed + 10) * 2.f - 1.f;
            float rEff = rBase * (1.f - n1 * 0.15f - n2 * 0.05f);

            float y = 0.f;
            glm::vec3 color(0.8f);

            if (rEff < p.bottomRadius) {
                y = -D; color = glm::vec3(0.65f);
            }
            else {
                float deltaR = rEff - p.bottomRadius;
                int   bIdx = (int)std::floor(deltaR / W_total);
                if (bIdx >= p.benchCount) {
                    y = 0.f; color = glm::vec3(0.45f, 0.52f, 0.42f);
                }
                else {
                    float rLocal = deltaR - bIdx * W_total;
                    if (rLocal < p.bermWidth) {
                        y = -D + bIdx * p.benchHeight;
                        color = glm::vec3(0.75f);
                    }
                    else {
                        float faceR = rLocal - p.bermWidth;
                        y = -D + bIdx * p.benchHeight + faceR * std::tan(faceAngleRad);
                        color = glm::vec3(0.55f);
                    }
                }
            }

            for (const auto& road : p.roadways) {
                float t;
                glm::vec2 p2d(x, z);
                glm::vec2 s2d(road.startPos.x, road.startPos.z);
                glm::vec2 e2d(road.endPos.x, road.endPos.z);
                float dist = pointToSegmentDistance(p2d, s2d, e2d, t);
                if (dist < road.width) {
                    float roadY = glm::mix(road.startPos.y, road.endPos.y, t);
                    float flatW = road.width * 0.5f;
                    if (dist < flatW) {
                        y = roadY; color = glm::vec3(0.5f, 0.45f, 0.35f);
                    }
                    else {
                        float eb = (dist - flatW) / (road.width - flatW);
                        eb = eb * eb * (3.f - 2.f * eb);
                        y = glm::mix(roadY, y, eb);
                        color = glm::mix(glm::vec3(0.5f, 0.45f, 0.35f), color, eb);
                    }
                }
            }

            if (y == 0.f)
                y += fractalNoise(x * 0.005f, z * 0.005f, 4, _seed + 20) * 15.f;
            else if (y == -D)
                y += fractalNoise(x * 0.02f, z * 0.02f, 3, _seed + 30) * 2.f;

            int idx = i * resolution + j;
            rawMesh.vertices[idx].position = glm::vec3(x, y, z);
            rawMesh.vertices[idx].color = color;
        }
    }

    for (int i = 0; i < resolution - 1; i++) {
        for (int j = 0; j < resolution - 1; j++) {
            unsigned int a = i * resolution + j;
            unsigned int b = i * resolution + (j + 1);
            unsigned int c = (i + 1) * resolution + j;
            unsigned int d = (i + 1) * resolution + (j + 1);
            rawMesh.indices.push_back(a); rawMesh.indices.push_back(b);
            rawMesh.indices.push_back(c); rawMesh.indices.push_back(c);
            rawMesh.indices.push_back(b); rawMesh.indices.push_back(d);
        }
    }

    return toFlatShaded(rawMesh);
}