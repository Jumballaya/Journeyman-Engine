#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "../common.hpp"

namespace gl {

class Shader {
 public:
  Shader() = default;

  ~Shader() {
    if (isValid()) {
      glDeleteProgram(_program);
    }
  }

  Shader(Shader&) noexcept = delete;
  Shader& operator=(Shader&) noexcept = delete;

  Shader(Shader&& other) noexcept
      : _fragmentSource(std::move(other._fragmentSource)),
        _vertexSource(std::move(other._vertexSource)),
        _program(other._program) {
    other._program = 0;
  }

  Shader& operator=(Shader&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    _fragmentSource = std::move(other._fragmentSource);
    _vertexSource = std::move(other._vertexSource);
    _program = other._program;
    other._program = 0;

    return *this;
  }

  void initialize() {
    _program = glCreateProgram();
  }

  void loadShader(const std::filesystem::path& vertPath, const std::filesystem::path& fragPath) {
    loadShader(fileToString(vertPath), fileToString(fragPath));
  }

  void loadShader(const std::string& vertexSource, const std::string& fragmentSource) {
    _vertexSource = vertexSource;
    _fragmentSource = fragmentSource;

    GLuint vertShader = createShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragShader = createShader(GL_FRAGMENT_SHADER, fragmentSource);

    glAttachShader(_program, vertShader);
    glAttachShader(_program, fragShader);
    glLinkProgram(_program);

    GLint success = 0;
    glGetProgramiv(_program, GL_LINK_STATUS, &success);

    if (success == false) {
      GLint logLength;
      glGetProgramiv(_program, GL_INFO_LOG_LENGTH, &logLength);
      std::string log(logLength, '\0');
      glGetProgramInfoLog(_program, logLength, nullptr, log.data());
      glDeleteProgram(_program);
      throw std::runtime_error("Program linking failed:\n" + log);
    }

    glDetachShader(_program, vertShader);
    glDetachShader(_program, fragShader);
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
  }

  void bind() {
    glUseProgram(_program);
  }

  void unbind() {
    glUseProgram(0);
  }

  bool isValid() {
    return _program != 0;
  }

  // Uniform API
  // run bind before setting uniform values
  //

  void uniform(const std::string& name, float val) {
    GLint loc = getUniformLocation(name);
    glUniform1f(loc, val);
  }

  void uniform(const std::string& name, int val) {
    GLint loc = getUniformLocation(name);
    glUniform1i(loc, val);
  }

 private:
  std::string _fragmentSource;
  std::string _vertexSource;
  GLuint _program;
  std::unordered_map<std::string, GLint> _uniformLocations;

  GLuint createShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const GLchar* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (success == false) {
      GLint logLength;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
      std::string log(logLength, '\0');
      glGetShaderInfoLog(shader, logLength, nullptr, log.data());
      glDeleteShader(shader);
      throw std::runtime_error("Shader compilation failed:\n" + log);
    }

    return shader;
  }

  GLint getUniformLocation(const std::string& location) {
    auto found = _uniformLocations.find(location);
    if (found == _uniformLocations.end()) {
      bind();
      GLint loc = glGetUniformLocation(_program, location.c_str());
      _uniformLocations.emplace(location, loc);
      return loc;
    }
    return found->second;
  }

  std::string fileToString(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);

    if (!file) {
      throw std::runtime_error("Failed to open file: " + path.string());
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
  }
};
}  // namespace gl