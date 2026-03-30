#pragma once
#include <GL/glew.h>
#include <memory>
#include "Types.h"
#include "Camera.h"
#include "MeshModel.h"
#include "PitGenerator.h"
#include "BlockModel.h"
#include "DrillholeDatabase.h"

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init();
    void setWindowSize(int w, int h);

    // ── Data upload ──────────────────────────────────────────────────────────

    // Upload terrain mesh (from PitGenerator)
    void loadMesh(const PitMeshData& meshData);

    // Upload block instances, colored by mode.
    // Blocks whose state == DISCARDED or MINED are skipped automatically.
    void loadBlockModel(const BlockModel& model,
        BlockColorMode mode = BlockColorMode::GRADE);

    // Upload drillhole line segments (one path per hole)
    void loadDrillholes(const DrillholeDatabase& db);

    // ── Render ───────────────────────────────────────────────────────────────
    void render(const Camera& cam, float aspect, const RenderSettings& s);

    // ── Utilities ────────────────────────────────────────────────────────────
    // Grade → color ramp: 0=blue, CUTOFF=yellow, 3+=red
    static glm::vec3 gradeToColor(float grade);

private:
    void buildGrid(float size, int divs);
    void buildAxes(float len);
    void buildInstancedCube();

    // ── Scene objects ─────────────────────────────────────────────────────────
    std::unique_ptr<MeshModel> _meshModel;

    // Terrain shader, instanced-block shader, flat-color (grid/lines) shader
    void* _bShader = nullptr;
    void* _iShader = nullptr;
    void* _gShader = nullptr;

    // Grid & axes
    GLuint _gridVAO = 0, _gridVBO = 0;
    int    _gridVertCount = 0;
    GLuint _axisVAO = 0, _axisVBO = 0;
    int    _axisVertCount = 0;

    // Instanced block geometry
    GLuint _cubeVAO = 0, _cubeVBO = 0, _instanceVBO = 0;
    int    _instanceCount = 0;
    float  _blockSize = 10.f;

    // Drillhole line segments
    GLuint _drillVAO = 0, _drillVBO = 0;
    int    _drillVertCount = 0;

    int _width = 1280, _height = 720;
    glm::vec3 _lightDir = glm::normalize(glm::vec3(0.5f, 1.f, 0.5f));
};