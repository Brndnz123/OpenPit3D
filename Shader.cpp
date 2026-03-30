#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

Shader::Shader(const std::string& vertPath, const std::string& fragPath) {
    std::string vSrc = readFile(vertPath);
    std::string fSrc = readFile(fragPath);

    GLuint vert = compileShader(GL_VERTEX_SHADER, vSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fSrc);

    id = glCreateProgram();
    glAttachShader(id, vert);
    glAttachShader(id, frag);
    glLinkProgram(id);
    checkErrors(id, true);

    glDeleteShader(vert);
    glDeleteShader(frag);
}

Shader::~Shader() {
    if (id) glDeleteProgram(id);
}

void Shader::use() const { glUseProgram(id); }

void Shader::setMat4(const std::string& n, const glm::mat4& v) const {
    glUniformMatrix4fv(glGetUniformLocation(id, n.c_str()), 1, GL_FALSE, glm::value_ptr(v));
}
void Shader::setVec3(const std::string& n, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(id, n.c_str()), 1, glm::value_ptr(v));
}
void Shader::setFloat(const std::string& n, float v) const {
    glUniform1f(glGetUniformLocation(id, n.c_str()), v);
}
void Shader::setInt(const std::string& n, int v) const {
    glUniform1i(glGetUniformLocation(id, n.c_str()), v);
}
void Shader::setBool(const std::string& n, bool v) const {
    glUniform1i(glGetUniformLocation(id, n.c_str()), v ? 1 : 0);
}

std::string Shader::readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[Shader] Failed to open: " << path << "\n";
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint Shader::compileShader(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* cstr = src.c_str();
    glShaderSource(s, 1, &cstr, nullptr);
    glCompileShader(s);
    checkErrors(s, false);
    return s;
}

void Shader::checkErrors(GLuint obj, bool isProgram) {
    GLint success;
    char log[1024];
    if (isProgram) {
        glGetProgramiv(obj, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(obj, 1024, nullptr, log);
            std::cerr << "[Shader] Link error:\n" << log << "\n";
        }
    }
    else {
        glGetShaderiv(obj, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(obj, 1024, nullptr, log);
            std::cerr << "[Shader] Compile error:\n" << log << "\n";
        }
    }
}