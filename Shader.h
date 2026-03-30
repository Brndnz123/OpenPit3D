#pragma once
#include <string>
#include <GL/glew.h>
#include <glm/glm.hpp>

class Shader {
public:
    GLuint id;
    Shader(const std::string& vertPath, const std::string& fragPath);
    ~Shader();
    void use() const;
    void setMat4(const std::string& n, const glm::mat4& v) const;
    void setVec3(const std::string& n, const glm::vec3& v) const;
    void setFloat(const std::string& n, float v) const;
    void setInt(const std::string& n, int v) const;
    void setBool(const std::string& n, bool v) const;
private:
    std::string readFile(const std::string& path);
    GLuint compileShader(GLenum type, const std::string& src);
    void checkErrors(GLuint obj, bool isProgram);
};