// Renderer.h
#pragma once
#include <vector>
#include <array>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Types.h"
#include "TextDrillholeData.h"
#include "MeshModel.h"

struct Plane { glm::vec3 normal; float d; };

struct FrustumCuller {
    std::array<Plane, 6> planes;
    void update(const glm::mat4& vp);
    bool testAABB(const glm::vec3& centre, const glm::vec3& halfExtent) const;
    bool testSphere(const glm::vec3& centre, float radius) const;
};

struct InstanceData { glm::vec3 pos; glm::vec4 color; };

class Renderer {
public:
    Renderer()  = default;
    ~Renderer() { shutdown(); }

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void init();
    void shutdown();

    void beginFrame(const glm::mat4& vp, const RenderSettings& rs);
    void renderBlocks   (const std::vector<Block>& blocks, BlockColorMode colorMode);
    void renderDrillholes(const std::vector<DesurveyedSample>& samples);
    // Lit triangle mesh (pit shell, terrain surface).
    // wireframe=true overlays black GL_LINE edges via glPolygonOffset.
    void renderMesh(const MeshModel& mesh, bool wireframe = false);
    void endFrame();

    int lastDrawnBlocks()  const { return stats_.drawnBlocks;  }
    int lastCulledBlocks() const { return stats_.culledBlocks; }
    int lastDrawCalls()    const { return stats_.drawCalls;    }

    void setBlockSize(float s)            { blockSize_ = s; }
    void setLightDir (const glm::vec3& d) { lightDir_ = glm::normalize(d); }

private:
    Shader shBlockSolid_;
    Shader shBlockWire_;
    Shader shDrillhole_;
    Shader shMesh_;           // Lit mesh shader for PitGenerator output

    GLuint cubeVAO_ = 0, cubeVBO_ = 0, cubeEBO_ = 0, instVBO_ = 0;
    static constexpr int MAX_INSTANCES = 2'000'000;

    GLuint drillVAO_       = 0;
    GLuint drillVBO_       = 0;
    int    drillVertCount_ = 0;

    glm::mat4      vp_        = glm::mat4(1.f);
    RenderSettings rs_        = {};
    FrustumCuller  frustum_   = {};
    float          blockSize_ = 20.f;
    glm::vec3      lightDir_  = glm::normalize(glm::vec3(1.f, 2.f, 1.5f));

    struct Stats { int drawnBlocks = 0, culledBlocks = 0, drawCalls = 0; };
    Stats stats_ = {};

    void buildCubeGeometry();
    void buildDrillholeBuffers(const std::vector<DesurveyedSample>& samples);
    static glm::vec4 blockColor(const Block& b, BlockColorMode mode);
    void drawInstanced(const std::vector<InstanceData>& instances, bool wireframe);
};
