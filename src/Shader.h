// Shader.h
#pragma once
#include <string>
#include <GL/glew.h>
#include <glm/glm.hpp>

// ─────────────────────────────────────────────────────────────────────────────
//  Shader — compiles a GLSL program from source strings.
//
//  Usage:
//    Shader s = Shader::makeBlockSolid();
//    s.use();
//    s.setMat4("uVP", vpMatrix);
//    // draw ...
//    Shader::unuse();
// ─────────────────────────────────────────────────────────────────────────────

class Shader {
public:
    Shader()  = default;
    ~Shader() { if (programId_) glDeleteProgram(programId_); }

    // Non-copyable, movable
    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept : programId_(o.programId_) { o.programId_ = 0; }
    Shader& operator=(Shader&& o) noexcept {
        if (this != &o) { if (programId_) glDeleteProgram(programId_);
                          programId_ = o.programId_; o.programId_ = 0; }
        return *this;
    }

    // Compile from source strings (geometry shader optional)
    bool compile(const std::string& vertSrc,
                 const std::string& fragSrc,
                 const std::string& geomSrc = "");

    void       use()   const { glUseProgram(programId_); }
    static void unuse()      { glUseProgram(0); }
    GLuint     id()    const { return programId_; }
    bool       valid() const { return programId_ != 0; }

    // ── Uniform setters ───────────────────────────────────────────────────────
    void setMat4 (const char* name, const glm::mat4& m)  const;
    void setMat3 (const char* name, const glm::mat3& m)  const;
    void setVec4 (const char* name, const glm::vec4& v)  const;
    void setVec3 (const char* name, const glm::vec3& v)  const;
    void setVec2 (const char* name, const glm::vec2& v)  const;
    void setFloat(const char* name, float  v)            const;
    void setInt  (const char* name, int    v)            const;
    void setBool (const char* name, bool   v)            const;

    // ── Pre-built shaders ─────────────────────────────────────────────────────
    //
    //  makeBlockSolid()
    //    Instanced cube renderer. Per-instance attribs: vec3 pos, vec4 color.
    //    Uniforms: mat4 uVP, vec3 uLightDir, float uAmbient, float uDiffuse,
    //              float uBlockSize.
    //
    //  makeBlockWireframe()
    //    Same instanced layout. Outputs flat black — used over the solid pass
    //    with glPolygonOffset to produce block outlines without Z-fighting.
    //
    //  makeDrillhole()
    //    Simple colored line shader. Per-vertex: vec3 pos, vec4 color.
    //    Uniforms: mat4 uVP.
    //
    static Shader makeBlockSolid();
    static Shader makeBlockWireframe();
    static Shader makeDrillhole();

private:
    GLuint programId_ = 0;

    GLuint compileStage(GLenum type, const std::string& src) const;
    GLint  uniformLoc  (const char* name) const;
};
