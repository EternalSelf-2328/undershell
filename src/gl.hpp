// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <GLES3/gl3.h>

#include <string>

namespace undershell {

// A linked GLES program. Throws std::runtime_error on compile/link failure.
class Program {
public:
  Program() = default;
  ~Program() { destroy(); }
  Program(const Program&) = delete;
  Program& operator=(const Program&) = delete;

  void create(const std::string& vertex, const std::string& fragment, const char* label);
  void destroy();
  [[nodiscard]] GLuint id() const { return m_id; }
  [[nodiscard]] bool valid() const { return m_id != 0; }
  GLint uniform(const char* name) const { return glGetUniformLocation(m_id, name); }

private:
  GLuint m_id = 0;
};

// The unit quad [0,1]^2 as two triangles, bound to attribute location 0.
void drawUnitQuad();

// Fullscreen vertex shader shared by all passes: v_uv in [0,1], y down.
extern const char* kQuadVertexShader;

}  // namespace undershell
