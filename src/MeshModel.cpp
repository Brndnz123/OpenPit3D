// MeshModel.cpp
#include "MeshModel.h"
#include <stdexcept>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>

// ─────────────────────────────────────────────────────────────────────────────
//  Move semantics — transfer GPU handles without double-free
// ─────────────────────────────────────────────────────────────────────────────

MeshModel::MeshModel(MeshModel&& o) noexcept
    : vertices(std::move(o.vertices))
    , indices (std::move(o.indices))
    , vao_(o.vao_), vbo_(o.vbo_), ebo_(o.ebo_), indexCount_(o.indexCount_)
{
    o.vao_ = o.vbo_ = o.ebo_ = 0;
    o.indexCount_ = 0;
}

MeshModel& MeshModel::operator=(MeshModel&& o) noexcept {
    if (this != &o) {
        freeGPU();
        vertices     = std::move(o.vertices);
        indices      = std::move(o.indices);
        vao_         = o.vao_;  vbo_ = o.vbo_;  ebo_ = o.ebo_;
        indexCount_  = o.indexCount_;
        o.vao_ = o.vbo_ = o.ebo_ = 0;
        o.indexCount_ = 0;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
//  upload — CPU → GPU
//
//  Strategy: orphan + re-upload avoids GPU stalls.
//  On first call, VAO/VBO/EBO are allocated; on subsequent calls the same
//  objects are reused so the VAO format description stays valid.
// ─────────────────────────────────────────────────────────────────────────────

void MeshModel::upload() {
    if (vertices.empty() || indices.empty()) return;

    const bool first = (vao_ == 0);

    if (first) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers     (1, &vbo_);
        glGenBuffers     (1, &ebo_);
    }

    glBindVertexArray(vao_);

    // ── Vertex buffer ──────────────────────────────────────────────────────────
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vertices.size() * sizeof(MeshVertex)),
                 vertices.data(), GL_DYNAMIC_DRAW);

    if (first) {
        // Attribute pointers only need to be set once (stored in the VAO)
        // loc 0 — position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                              (void*)offsetof(MeshVertex, pos));
        // loc 1 — normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                              (void*)offsetof(MeshVertex, normal));
        // loc 2 — color
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                              (void*)offsetof(MeshVertex, color));
    }

    // ── Index buffer ───────────────────────────────────────────────────────────
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(indices.size() * sizeof(GLuint)),
                 indices.data(), GL_DYNAMIC_DRAW);

    indexCount_ = (GLsizei)indices.size();

    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  draw
// ─────────────────────────────────────────────────────────────────────────────

void MeshModel::draw() const {
    if (!vao_ || indexCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeGPU
// ─────────────────────────────────────────────────────────────────────────────

void MeshModel::freeGPU() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_);       vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_);       ebo_ = 0; }
    indexCount_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  computeNormals
//
//  Area-weighted smooth normals:
//    1. Accumulate cross-product of each triangle's edges onto its three
//       vertices (the cross-product magnitude is proportional to area).
//    2. Normalise each accumulated normal.
//
//  This produces visually smooth shading on the curved pit walls while
//  preserving sharp bench-floor edges (since disconnected quads share no
//  vertices across bench breaks when built by PitGenerator).
// ─────────────────────────────────────────────────────────────────────────────

void MeshModel::computeNormals() {
    // Zero all normals first
    for (auto& v : vertices) v.normal = glm::vec3(0.f);

    const size_t triCount = indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t) {
        GLuint i0 = indices[t*3+0];
        GLuint i1 = indices[t*3+1];
        GLuint i2 = indices[t*3+2];

        const glm::vec3& p0 = vertices[i0].pos;
        const glm::vec3& p1 = vertices[i1].pos;
        const glm::vec3& p2 = vertices[i2].pos;

        // Cross product (un-normalised — magnitude = 2 × triangle area)
        glm::vec3 faceN = glm::cross(p1 - p0, p2 - p0);

        vertices[i0].normal += faceN;
        vertices[i1].normal += faceN;
        vertices[i2].normal += faceN;
    }

    for (auto& v : vertices) {
        float len = glm::length(v.normal);
        if (len > 1e-6f) v.normal /= len;
    }
}
