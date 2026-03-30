// MeshModel.h
#pragma once
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>

// ─────────────────────────────────────────────────────────────────────────────
//  MeshVertex — per-vertex layout for the mesh shader
//
//  loc 0  vec3  pos
//  loc 1  vec3  normal
//  loc 2  vec4  color
// ─────────────────────────────────────────────────────────────────────────────
struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 color;
};

// ─────────────────────────────────────────────────────────────────────────────
//  MeshModel
//
//  Owns an indexed triangle mesh on the CPU and its GPU mirror.
//
//  Usage:
//    MeshModel m;
//    m.vertices = { ... };
//    m.indices  = { ... };
//    m.computeNormals();   // optional — area-weighted smooth normals
//    m.upload();           // CPU → GPU; safe to call again after update
//    // per frame:
//    m.draw();
// ─────────────────────────────────────────────────────────────────────────────
class MeshModel {
public:
    MeshModel()  = default;
    ~MeshModel() { freeGPU(); }

    MeshModel(const MeshModel&)            = delete;
    MeshModel& operator=(const MeshModel&) = delete;
    MeshModel(MeshModel&& o) noexcept;
    MeshModel& operator=(MeshModel&& o) noexcept;

    // CPU geometry — fill then call upload()
    std::vector<MeshVertex> vertices;
    std::vector<GLuint>     indices;

    // GPU lifecycle
    void upload();           // Sends (or re-sends) data to GPU
    void draw()    const;    // glDrawElements; no-op if not uploaded
    void freeGPU();          // Deletes VAO/VBO/EBO

    bool isUploaded() const { return vao_ != 0; }
    bool isEmpty()    const { return indices.empty(); }

    // Area-weighted smooth normals from indexed triangle soup
    void computeNormals();

private:
    GLuint  vao_        = 0;
    GLuint  vbo_        = 0;
    GLuint  ebo_        = 0;
    GLsizei indexCount_ = 0;
};
