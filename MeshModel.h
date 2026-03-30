#pragma once
#include <GL/glew.h>
#include "PitGenerator.h"

class MeshModel {
public:
    MeshModel();
    ~MeshModel();

    void upload(const PitMeshData& meshData);
    void draw() const;
    void drawWireframe() const;

    int vertexCount() const { return _indexCount; }

private:
    GLuint _vao = 0;
    GLuint _vbo = 0;
    GLuint _ebo = 0;
    int _indexCount = 0;
};