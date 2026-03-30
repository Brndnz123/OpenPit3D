// Renderer.cpp
#include "Renderer.h"
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

// ─────────────────────────────────────────────────────────────────────────────
//  FrustumCuller — Gribb-Hartmann plane extraction
//
//  Given the combined VP matrix M, the six clip planes are rows of M combined:
//    Left   =  col3 + col0
//    Right  =  col3 - col0
//    Bottom =  col3 + col1
//    Top    =  col3 - col1
//    Near   =  col3 + col2
//    Far    =  col3 - col2
//
//  We normalise each plane so the distance test is in metres.
// ─────────────────────────────────────────────────────────────────────────────

void FrustumCuller::update(const glm::mat4& vp) {
    // GLM stores matrices column-major: vp[col][row]
    auto extractPlane = [&](float a, float b, float c, float d) -> Plane {
        Plane p;
        p.normal = glm::vec3(a, b, c);
        p.d      = d;
        float len = glm::length(p.normal);
        if (len > 1e-6f) { p.normal /= len; p.d /= len; }
        return p;
    };

    const glm::mat4& m = vp;
    // Row access via transpose trick:  row_i = column i of transpose = m[*][i]
    // Using m[col][row] notation:
    planes[0] = extractPlane( m[0][3]+m[0][0],  m[1][3]+m[1][0],  m[2][3]+m[2][0],  m[3][3]+m[3][0]); // Left
    planes[1] = extractPlane( m[0][3]-m[0][0],  m[1][3]-m[1][0],  m[2][3]-m[2][0],  m[3][3]-m[3][0]); // Right
    planes[2] = extractPlane( m[0][3]+m[0][1],  m[1][3]+m[1][1],  m[2][3]+m[2][1],  m[3][3]+m[3][1]); // Bottom
    planes[3] = extractPlane( m[0][3]-m[0][1],  m[1][3]-m[1][1],  m[2][3]-m[2][1],  m[3][3]-m[3][1]); // Top
    planes[4] = extractPlane( m[0][3]+m[0][2],  m[1][3]+m[1][2],  m[2][3]+m[2][2],  m[3][3]+m[3][2]); // Near
    planes[5] = extractPlane( m[0][3]-m[0][2],  m[1][3]-m[1][2],  m[2][3]-m[2][2],  m[3][3]-m[3][2]); // Far
}

bool FrustumCuller::testAABB(const glm::vec3& c, const glm::vec3& h) const {
    // For each plane, find the positive vertex (most in the direction of the normal)
    // and test if it's behind the plane.  If so, the box is completely outside.
    for (const auto& p : planes) {
        // Radius of the projection of the AABB half-extents onto the plane normal
        float r = std::abs(p.normal.x * h.x)
                + std::abs(p.normal.y * h.y)
                + std::abs(p.normal.z * h.z);
        // Signed distance from AABB centre to plane
        float dist = glm::dot(p.normal, c) + p.d;
        if (dist + r < 0.f) return false;   // Entirely outside this plane
    }
    return true;
}

bool FrustumCuller::testSphere(const glm::vec3& c, float r) const {
    for (const auto& p : planes)
        if (glm::dot(p.normal, c) + p.d < -r) return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Unit cube geometry
//
//  24 unique vertices (4 per face, so normals are face-flat — important for
//  correct lighting on a block model where each face is a distinct plane).
//  36 indices (6 faces × 2 triangles × 3 vertices).
// ─────────────────────────────────────────────────────────────────────────────

struct CubeVertex { glm::vec3 pos; glm::vec3 normal; };

static const CubeVertex CUBE_VERTS[24] = {
    // +X face (normal = +X)
    {{ 0.5f,-0.5f,-0.5f},{1,0,0}}, {{ 0.5f, 0.5f,-0.5f},{1,0,0}},
    {{ 0.5f, 0.5f, 0.5f},{1,0,0}}, {{ 0.5f,-0.5f, 0.5f},{1,0,0}},
    // -X face (normal = -X)
    {{-0.5f,-0.5f, 0.5f},{-1,0,0}}, {{-0.5f, 0.5f, 0.5f},{-1,0,0}},
    {{-0.5f, 0.5f,-0.5f},{-1,0,0}}, {{-0.5f,-0.5f,-0.5f},{-1,0,0}},
    // +Y face (normal = +Y)
    {{-0.5f, 0.5f,-0.5f},{0,1,0}}, {{-0.5f, 0.5f, 0.5f},{0,1,0}},
    {{ 0.5f, 0.5f, 0.5f},{0,1,0}}, {{ 0.5f, 0.5f,-0.5f},{0,1,0}},
    // -Y face (normal = -Y)
    {{-0.5f,-0.5f, 0.5f},{0,-1,0}}, {{-0.5f,-0.5f,-0.5f},{0,-1,0}},
    {{ 0.5f,-0.5f,-0.5f},{0,-1,0}}, {{ 0.5f,-0.5f, 0.5f},{0,-1,0}},
    // +Z face (normal = +Z)
    {{-0.5f,-0.5f, 0.5f},{0,0,1}}, {{ 0.5f,-0.5f, 0.5f},{0,0,1}},
    {{ 0.5f, 0.5f, 0.5f},{0,0,1}}, {{-0.5f, 0.5f, 0.5f},{0,0,1}},
    // -Z face (normal = -Z)
    {{ 0.5f,-0.5f,-0.5f},{0,0,-1}}, {{-0.5f,-0.5f,-0.5f},{0,0,-1}},
    {{-0.5f, 0.5f,-0.5f},{0,0,-1}}, {{ 0.5f, 0.5f,-0.5f},{0,0,-1}},
};

static const GLuint CUBE_IDX[36] = {
     0, 1, 2,  0, 2, 3,   // +X
     4, 5, 6,  4, 6, 7,   // -X
     8, 9,10,  8,10,11,   // +Y
    12,13,14, 12,14,15,   // -Y
    16,17,18, 16,18,19,   // +Z
    20,21,22, 20,22,23    // -Z
};

// ─────────────────────────────────────────────────────────────────────────────
//  Color mapping
// ─────────────────────────────────────────────────────────────────────────────

static glm::vec3 heatmap(float t) {
    // Cool blue (low) → white (mid) → hot red (high)
    t = std::clamp(t, 0.f, 1.f);
    if (t < 0.5f) return glm::mix(glm::vec3(0.1f,0.3f,0.9f), glm::vec3(1.f,1.f,1.f), t * 2.f);
    else           return glm::mix(glm::vec3(1.f,1.f,1.f),   glm::vec3(0.9f,0.1f,0.1f), (t - 0.5f) * 2.f);
}

glm::vec4 Renderer::blockColor(const Block& b, BlockColorMode mode) {
    switch (mode) {
        case BlockColorMode::FLAT:
            // Colour by state
            if (b.state == LGState::IN_PIT)    return {0.85f, 0.55f, 0.20f, 1.f}; // ore orange
            if (b.state == LGState::MINED)     return {0.50f, 0.50f, 0.50f, 1.f}; // grey mined
            if (b.state == LGState::DISCARDED) return {0.20f, 0.20f, 0.20f, 0.5f};// transparent waste
            return {0.70f, 0.70f, 0.70f, 1.f};

        case BlockColorMode::GRADE: {
            // Normalise grade to [0,1] assuming 0–5% range — tune to deposit
            float t = std::clamp(b.grade / 5.f, 0.f, 1.f);
            return glm::vec4(heatmap(t), 1.f);
        }
        case BlockColorMode::ECONOMIC: {
            // Green = positive economic value; red = waste
            if (b.value >= 0.f) {
                float t = std::clamp(b.value / 1000.f, 0.f, 1.f);
                return {0.1f + 0.6f*t, 0.7f, 0.1f, 1.f};
            } else {
                float t = std::clamp(-b.value / 500.f, 0.f, 1.f);
                return {0.7f, 0.1f + 0.3f*(1.f-t), 0.1f, 1.f};
            }
        }
    }
    return {1,1,1,1};
}

// ─────────────────────────────────────────────────────────────────────────────
//  init / shutdown
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::init() {
    shBlockSolid_ = Shader::makeBlockSolid();
    shBlockWire_  = Shader::makeBlockWireframe();
    shDrillhole_  = Shader::makeDrillhole();

    buildCubeGeometry();
}

void Renderer::buildCubeGeometry() {
    glGenVertexArrays(1, &cubeVAO_);
    glGenBuffers(1, &cubeVBO_);
    glGenBuffers(1, &cubeEBO_);
    glGenBuffers(1, &instVBO_);

    glBindVertexArray(cubeVAO_);

    // ── Static cube geometry ──────────────────────────────────────────────────
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTS), CUBE_VERTS, GL_STATIC_DRAW);

    // loc 0 — position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex),
                          (void*)offsetof(CubeVertex, pos));
    // loc 1 — normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex),
                          (void*)offsetof(CubeVertex, normal));

    // ── Index buffer ──────────────────────────────────────────────────────────
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(CUBE_IDX), CUBE_IDX, GL_STATIC_DRAW);

    // ── Per-instance VBO ──────────────────────────────────────────────────────
    //  Pre-allocate MAX_INSTANCES worth of space; we'll sub-upload each frame.
    glBindBuffer(GL_ARRAY_BUFFER, instVBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 MAX_INSTANCES * sizeof(InstanceData),
                 nullptr, GL_DYNAMIC_DRAW);

    // loc 2 — instance world position (vec3)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          (void*)offsetof(InstanceData, pos));
    glVertexAttribDivisor(2, 1);    // Advance once per instance

    // loc 3 — instance color (vec4)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          (void*)offsetof(InstanceData, color));
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
}

void Renderer::shutdown() {
    if (cubeVAO_)  { glDeleteVertexArrays(1, &cubeVAO_);  cubeVAO_  = 0; }
    if (cubeVBO_)  { glDeleteBuffers(1, &cubeVBO_);        cubeVBO_  = 0; }
    if (cubeEBO_)  { glDeleteBuffers(1, &cubeEBO_);        cubeEBO_  = 0; }
    if (instVBO_)  { glDeleteBuffers(1, &instVBO_);        instVBO_  = 0; }
    if (drillVAO_) { glDeleteVertexArrays(1, &drillVAO_); drillVAO_ = 0; }
    if (drillVBO_) { glDeleteBuffers(1, &drillVBO_);       drillVBO_ = 0; }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Frame begin / end
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::beginFrame(const glm::mat4& vp, const RenderSettings& rs) {
    vp_ = vp;
    rs_ = rs;
    frustum_.update(vp);
    stats_ = {};
}

void Renderer::endFrame() {
    // Reset any global GL state changes made during rendering
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_POLYGON_OFFSET_LINE);
    Shader::unuse();
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderBlocks — frustum cull then instanced draw
//
//  Two-pass outline technique:
//    Pass 1 — Solid fill.  glPolygonMode FILL, no offset.
//    Pass 2 — Wireframe.   glPolygonMode LINE + glPolygonOffset(1, 1) pulls the
//             lines slightly toward the camera so they sit in front of the faces
//             without Z-fighting artifacts.
//
//  Performance notes:
//    • We batch up to MAX_INSTANCES instances, flushing in chunks when full.
//    • Culled blocks never enter the instance array at all.
//    • DISCARDED blocks are always skipped (waste outside pit shell).
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::renderBlocks(const std::vector<Block>& blocks,
                             BlockColorMode colorMode)
{
    if (!rs_.showBlocks) return;

    const float half = blockSize_ * 0.5f;
    const glm::vec3 halfExt(half, half, half);

    std::vector<InstanceData> batch;
    batch.reserve(std::min((int)blocks.size(), MAX_INSTANCES));

    for (const auto& b : blocks) {
        if (b.state == LGState::DISCARDED) { ++stats_.culledBlocks; continue; }

        // Frustum cull — AABB centred on block world position
        if (!frustum_.testAABB(b.worldPos, halfExt)) { ++stats_.culledBlocks; continue; }

        batch.push_back({ b.worldPos, blockColor(b, colorMode) });
        ++stats_.drawnBlocks;

        // Flush when we hit the buffer limit
        if ((int)batch.size() >= MAX_INSTANCES) {
            drawInstanced(batch, false);
            if (rs_.showWireframe) drawInstanced(batch, true);
            batch.clear();
        }
    }

    if (!batch.empty()) {
        drawInstanced(batch, false);
        if (rs_.showWireframe) drawInstanced(batch, true);
    }
}

void Renderer::drawInstanced(const std::vector<InstanceData>& instances, bool wireframe)
{
    if (instances.empty()) return;

    // Upload instance data (orphan the buffer, then sub-data — avoids GPU stall)
    glBindBuffer(GL_ARRAY_BUFFER, instVBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 MAX_INSTANCES * sizeof(InstanceData),
                 nullptr, GL_DYNAMIC_DRAW);                          // Orphan
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    instances.size() * sizeof(InstanceData),
                    instances.data());                                // Upload

    if (!wireframe) {
        // ── Pass 1: Solid fill ────────────────────────────────────────────────
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_POLYGON_OFFSET_LINE);

        shBlockSolid_.use();
        shBlockSolid_.setMat4 ("uVP",        vp_);
        shBlockSolid_.setFloat("uBlockSize",  blockSize_);
        shBlockSolid_.setVec3 ("uLightDir",   lightDir_);
        shBlockSolid_.setFloat("uAmbient",    rs_.ambientStr);
        shBlockSolid_.setFloat("uDiffuse",    rs_.diffuseStr);
    } else {
        // ── Pass 2: Wireframe outline ─────────────────────────────────────────
        //  glPolygonOffset(factor, units):
        //    factor = 1.0 scales by the polygon's depth slope (handles polygons
        //             that are not screen-parallel).
        //    units  = 1.0 adds a fixed offset in depth buffer precision units.
        //  Together they shift the wireframe lines forward just enough to beat
        //  the solid face in the depth test without visible bias.
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.f, -1.f);   // Negative = toward camera in NDC

        shBlockWire_.use();
        shBlockWire_.setMat4 ("uVP",       vp_);
        shBlockWire_.setFloat("uBlockSize", blockSize_);
    }

    glBindVertexArray(cubeVAO_);
    glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr,
                             (GLsizei)instances.size());
    glBindVertexArray(0);
    ++stats_.drawCalls;
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderDrillholes
// ─────────────────────────────────────────────────────────────────────────────

struct DrillVertex { glm::vec3 pos; glm::vec4 color; };

static glm::vec4 gradeColor(float grade) {
    float t = std::clamp(grade / 5.f, 0.f, 1.f);
    return glm::vec4(t, 1.f - t * 0.8f, 0.1f, 1.f);
}

void Renderer::buildDrillholeBuffers(const std::vector<DesurveyedSample>& samples) {
    if (samples.empty()) return;

    std::vector<DrillVertex> verts;
    verts.reserve(samples.size() * 2);
    for (const auto& s : samples) {
        glm::vec4 c = gradeColor((float)s.grade);
        verts.push_back({ glm::vec3(s.fromPt), c });
        verts.push_back({ glm::vec3(s.toPt),   c });
    }
    drillVertCount_ = (int)verts.size();

    if (!drillVAO_) {
        glGenVertexArrays(1, &drillVAO_);
        glGenBuffers(1, &drillVBO_);
    }
    glBindVertexArray(drillVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, drillVBO_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(DrillVertex),
                 verts.data(), GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DrillVertex),
                          (void*)offsetof(DrillVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(DrillVertex),
                          (void*)offsetof(DrillVertex, color));
    glBindVertexArray(0);
}

void Renderer::renderDrillholes(const std::vector<DesurveyedSample>& samples) {
    if (!rs_.showDrillholes || samples.empty()) return;

    buildDrillholeBuffers(samples);   // Re-uploads if data changed; cheap if not

    shDrillhole_.use();
    shDrillhole_.setMat4("uVP", vp_);

    glLineWidth(2.f);
    glBindVertexArray(drillVAO_);
    glDrawArrays(GL_LINES, 0, drillVertCount_);
    glBindVertexArray(0);
    glLineWidth(1.f);
    ++stats_.drawCalls;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mesh shader  (lit, per-vertex color, same light model as block shader)
// ─────────────────────────────────────────────────────────────────────────────

static const char* MESH_VERT = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;

uniform mat4 uVP;

out vec4 vColor;
out vec3 vNormal;

void main() {
    vColor    = aColor;
    vNormal   = aNormal;
    gl_Position = uVP * vec4(aPos, 1.0);
}
)glsl";

static const char* MESH_FRAG = R"glsl(
#version 330 core
in vec4 vColor;
in vec3 vNormal;

uniform vec3  uLightDir;
uniform float uAmbient;
uniform float uDiffuse;

out vec4 FragColor;

void main() {
    float diff  = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);
    float light = uAmbient + uDiffuse * diff;
    FragColor   = vec4(vColor.rgb * light, vColor.a);
}
)glsl";

static const char* MESH_VERT_WIRE = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
uniform mat4 uVP;
void main() { gl_Position = uVP * vec4(aPos, 1.0); }
)glsl";

static const char* MESH_FRAG_WIRE = R"glsl(
#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(0.0, 0.0, 0.0, 0.8); }
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer::init() extension — compile mesh shaders
//  We patch init() by appending; the block/drill shaders compile in the
//  existing init() above.  The mesh shader pair is compiled here on first
//  renderMesh() call so GL is guaranteed to be ready.
// ─────────────────────────────────────────────────────────────────────────────

static bool s_meshShaderBuilt = false;

// ─────────────────────────────────────────────────────────────────────────────
//  renderMesh
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::renderMesh(const MeshModel& mesh, bool wireframe) {
    if (mesh.isEmpty() || !mesh.isUploaded()) return;

    // Lazy compile mesh shaders (Shader objects live in shMesh_ but init()
    // already ran — we build on first use to avoid coupling)
    if (!shMesh_.valid()) {
        shMesh_.compile(MESH_VERT, MESH_FRAG);
    }

    // ── Pass 1: Solid fill ────────────────────────────────────────────────────
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_POLYGON_OFFSET_FILL);

    shMesh_.use();
    shMesh_.setMat4 ("uVP",       vp_);
    shMesh_.setVec3 ("uLightDir", lightDir_);
    shMesh_.setFloat("uAmbient",  rs_.ambientStr);
    shMesh_.setFloat("uDiffuse",  rs_.diffuseStr);

    mesh.draw();
    ++stats_.drawCalls;

    // ── Pass 2: Wireframe overlay ─────────────────────────────────────────────
    if (wireframe) {
        // Reuse block wireframe approach: polygon offset pulls lines forward
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.f, -1.f);

        // Compile wire variant lazily
        static Shader meshWire;
        if (!meshWire.valid()) meshWire.compile(MESH_VERT_WIRE, MESH_FRAG_WIRE);
        meshWire.use();
        meshWire.setMat4("uVP", vp_);

        mesh.draw();
        ++stats_.drawCalls;

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_POLYGON_OFFSET_LINE);
    }

    Shader::unuse();
}
