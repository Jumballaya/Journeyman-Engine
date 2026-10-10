#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "../common.hpp"

namespace gl {

// A linked vertex + fragment program. Uniform setters need bind() first.
class Shader {
 public:
  Shader() = default;
  ~Shader() {
    if (_program) glDeleteProgram(_program);
  }
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;
  Shader(Shader&& other) noexcept
      : _program(std::exchange(other._program, 0)), _locations(std::move(other._locations)) {}
  Shader& operator=(Shader&&) = delete;

  // Replaces the program (a reload). Throws std::runtime_error carrying the
  // compiler's or linker's log, keeping the program it had.
  void load(const std::string& vertexSource, const std::string& fragmentSource) {
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = 0;
    try {
      fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
    } catch (...) {
      glDeleteShader(vertex);
      throw;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);  // freed along with the program
    glDeleteShader(fragment);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
      const std::string log = infoLog(program, glGetProgramiv, glGetProgramInfoLog);
      glDeleteProgram(program);
      throw std::runtime_error("Program linking failed:\n" + log);
    }
    if (_program) glDeleteProgram(_program);
    _program = program;
    _locations.clear();  // they were the old program's
  }

  void bind() { glUseProgram(_program); }
  void unbind() { glUseProgram(0); }

  void uniform(const std::string& name, float v) { glUniform1f(location(name), v); }
  void uniform(const std::string& name, int v) { glUniform1i(location(name), v); }
  void uniform(const std::string& name, const glm::vec2& v) { glUniform2fv(location(name), 1, glm::value_ptr(v)); }
  void uniform(const std::string& name, const glm::vec3& v) { glUniform3fv(location(name), 1, glm::value_ptr(v)); }
  void uniform(const std::string& name, const glm::vec4& v) { glUniform4fv(location(name), 1, glm::value_ptr(v)); }
  // A vec4 array uniform ("u_lights", declared vec4 u_lights[N]): its first values.size() entries.
  void uniform(const std::string& name, const std::vector<glm::vec4>& values) {
    if (!values.empty()) glUniform4fv(location(name), static_cast<GLsizei>(values.size()), glm::value_ptr(values[0]));
  }
  void uniform(const std::string& name, const glm::mat4& v) {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(v));
  }

 private:
  GLuint _program = 0;
  std::unordered_map<std::string, GLint> _locations;

  template <typename GetIv, typename GetLog>
  static std::string infoLog(GLuint object, GetIv getIv, GetLog getLog) {
    GLint length = 0;
    getIv(object, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(std::max(length, 0)), '\0');
    getLog(object, length, nullptr, log.data());
    return log;
  }

  static GLuint compile(GLenum type, const std::string& source) {
    const GLuint shader = glCreateShader(type);
    const GLchar* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
      const std::string log = infoLog(shader, glGetShaderiv, glGetShaderInfoLog);
      glDeleteShader(shader);
      throw std::runtime_error("Shader compilation failed:\n" + log);
    }
    return shader;
  }

  GLint location(const std::string& name) {
    auto it = _locations.find(name);
    if (it == _locations.end()) it = _locations.emplace(name, glGetUniformLocation(_program, name.c_str())).first;
    return it->second;
  }
};

}  // namespace gl
