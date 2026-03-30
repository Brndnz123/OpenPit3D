// Shader.cpp
#include "Shader.h"
#include <stdexcept>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

// ─────────────────────────────────────────────────────────────────────────────
//  GLSL source — embedded as raw string literals
// ─────────────────────────────────────────────────────────────────────────────

// ── Instanced solid block ─────────────────────────────────────────────────────
// Per-vertex  (loc 0) vec3 aPos    — unit cube vertex position  [-0.5, +0.5]
// Per-vertex  (loc 1) vec3 aNormal — face normal
// Per-instance(loc 2) vec3 aInstancePos   — block world-space centre
// Per-instance(loc 3) vec4 aInstanceColor — RGBA color (pre-computed by UI)

static const char* BLOCK_VERT_SOLID = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aInstancePos;
layout(location = 3) in vec4 aInstanceColor;

uniform mat4  uVP;
uniform float uBlockSize;   // metres per block (uniform scale)

out vec4 vColor;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec3 worldPos = aInstancePos + aPos * uBlockSize;
    vWorldPos     = worldPos;
    vNormal       = aNormal;          // No non-uniform scale, so no normal matrix needed
    vColor        = aInstanceColor;
    gl_Position   = uVP * vec4(worldPos, 1.0);
}
)glsl";

static const char* BLOCK_FRAG_SOLID = R"glsl(
#version 330 core

in vec4 vColor;
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3  uLightDir;    // Normalised direction toward light
uniform float uAmbient;     // 0..1
uniform float uDiffuse;     // 0..1

out vec4 FragColor;

void main() {
    vec3  N     = normalize(vNormal);
    float diff  = max(dot(N, normalize(uLightDir)), 0.0);
    float light = uAmbient + uDiffuse * diff;
    FragColor   = vec4(vColor.rgb * light, vColor.a);
}
)glsl";

// ── Instanced wireframe / outline block ──────────────────────────────────────
// Identical vertex layout so we reuse the same VAO.
// The fragment shader outputs flat black — the Renderer applies glPolygonOffset
// before this pass so it draws slightly in front of the solid faces.

static const char* BLOCK_VERT_WIRE = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aInstancePos;
layout(location = 3) in vec4 aInstanceColor;

uniform mat4  uVP;
uniform float uBlockSize;

void main() {
    vec3 worldPos = aInstancePos + aPos * uBlockSize;
    gl_Position   = uVP * vec4(worldPos, 1.0);
}
)glsl";

static const char* BLOCK_FRAG_WIRE = R"glsl(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);   // Solid black outline
}
)glsl";

// ── Drillhole lines ───────────────────────────────────────────────────────────
// Per-vertex (loc 0) vec3 aPos, (loc 1) vec4 aColor

static const char* DRILL_VERT = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uVP;

out vec4 vColor;
void main() {
    vColor      = aColor;
    gl_Position = uVP * vec4(aPos, 1.0);
}
)glsl";

static const char* DRILL_FRAG = R"glsl(
#version 330 core
in  vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
//  compile()
// ─────────────────────────────────────────────────────────────────────────────

bool Shader::compile(const std::string& vertSrc,
                     const std::string& fragSrc,
                     const std::string& geomSrc)
{
    if (programId_) { glDeleteProgram(programId_); programId_ = 0; }

    GLuint vert = compileStage(GL_VERTEX_SHADER,   vertSrc);
    GLuint frag = compileStage(GL_FRAGMENT_SHADER, fragSrc);
    GLuint geom = 0;
    if (!geomSrc.empty()) geom = compileStage(GL_GEOMETRY_SHADER, geomSrc);

    programId_ = glCreateProgram();
    glAttachShader(programId_, vert);
    glAttachShader(programId_, frag);
    if (geom) glAttachShader(programId_, geom);
    glLinkProgram(programId_);

    GLint ok = 0;
    glGetProgramiv(programId_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(programId_, sizeof(log), nullptr, log);
        std::cerr << "[Shader] Link error:\n" << log << '\n';
        glDeleteProgram(programId_); programId_ = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    if (geom) glDeleteShader(geom);

    return ok != 0;
}

GLuint Shader::compileStage(GLenum type, const std::string& src) const {
    GLuint s  = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Shader] Compile error (type=" << type << "):\n" << log << '\n';
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Uniform setters
// ─────────────────────────────────────────────────────────────────────────────

GLint Shader::uniformLoc(const char* name) const {
    return glGetUniformLocation(programId_, name);
}

void Shader::setMat4 (const char* n, const glm::mat4& m) const { glUniformMatrix4fv(uniformLoc(n), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::setMat3 (const char* n, const glm::mat3& m) const { glUniformMatrix3fv(uniformLoc(n), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::setVec4 (const char* n, const glm::vec4& v) const { glUniform4fv(uniformLoc(n), 1, glm::value_ptr(v)); }
void Shader::setVec3 (const char* n, const glm::vec3& v) const { glUniform3fv(uniformLoc(n), 1, glm::value_ptr(v)); }
void Shader::setVec2 (const char* n, const glm::vec2& v) const { glUniform2fv(uniformLoc(n), 1, glm::value_ptr(v)); }
void Shader::setFloat(const char* n, float  v)           const { glUniform1f (uniformLoc(n), v); }
void Shader::setInt  (const char* n, int    v)           const { glUniform1i (uniformLoc(n), v); }
void Shader::setBool (const char* n, bool   v)           const { glUniform1i (uniformLoc(n), v ? 1 : 0); }

// ─────────────────────────────────────────────────────────────────────────────
//  Factory methods
// ─────────────────────────────────────────────────────────────────────────────

Shader Shader::makeBlockSolid() {
    Shader s;
    if (!s.compile(BLOCK_VERT_SOLID, BLOCK_FRAG_SOLID))
        throw std::runtime_error("Failed to compile block solid shader");
    return s;
}

Shader Shader::makeBlockWireframe() {
    Shader s;
    if (!s.compile(BLOCK_VERT_WIRE, BLOCK_FRAG_WIRE))
        throw std::runtime_error("Failed to compile block wireframe shader");
    return s;
}

Shader Shader::makeDrillhole() {
    Shader s;
    if (!s.compile(DRILL_VERT, DRILL_FRAG))
        throw std::runtime_error("Failed to compile drillhole shader");
    return s;
}
