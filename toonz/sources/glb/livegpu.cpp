#include "livegpu.h"

#include <QOpenGLContext>
#include <QPointer>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QMatrix3x3>
#include <QVector3D>
#include <QVector4D>
#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>

namespace otglb {
namespace {
const char *vertexShader = R"GLSL(
#version 120
attribute vec4 position;
attribute vec3 normal, view;
attribute vec4 color;
attribute vec3 reflection;
attribute vec2 uv;
uniform mat4 imageToClip;
varying vec3 N, V, R;
varying vec4 C;
varying vec2 UV;
void main() {
  vec4 p = imageToClip * vec4(position.xy, 0.0, 1.0);
  gl_Position = vec4(p.xy * position.w, position.z * position.w, position.w);
  N = normal; V = view; C = color; R = reflection; UV = uv;
}
)GLSL";
QString fragmentShader() {
  QString shader = R"GLSL(
#version 120
varying vec3 N, V, R;
varying vec4 C;
varying vec2 UV;
uniform vec4 colorScale;
uniform sampler2D image;
vec3 linear(vec3 c) {
  return mix(c / 12.92, pow(max((c + 0.055) / 1.055, vec3(0.0)), vec3(2.4)), step(vec3(0.04045), c));
}
vec3 display(vec3 c) {
  c = max(c, vec3(0.0));
  return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
vec2 panorama(vec3 d) {
  return vec2(0.5 + atan(d.x, d.z) / 6.28318530718,
              0.5 + asin(clamp(d.y, -1.0, 1.0)) / 3.14159265359);
}
)GLSL";
  for (int i = 0; i < 4; ++i)
    shader +=
        QString(
            "uniform sampler2D environment%1;\nuniform mat3 "
            "rotation%1;\nuniform vec3 gain%1;\nuniform vec3 settings%1;\n")
            .arg(i);
  shader += R"GLSL(
void main() {
  vec3 n = normalize(N), v = normalize(V);
  if (dot(n, v) < 0.0) n = -n;
  vec3 reflected = reflect(-v, n);
  vec3 c = C.rgb;
  float alpha = C.a;
  if (UV.x >= 0.0) {
    vec4 t = texture2D(image, UV);
    alpha = t.a;
    c = alpha > 0.0 ? linear(t.rgb / alpha) : vec3(0.0);
  }
)GLSL";
  for (int i = 0; i < 4; ++i)
    shader += QString(R"GLSL(
  {
    float r = settings%1.x >= 0.0 ? settings%1.x : R.x;
    float b = settings%1.y >= 0.0 ? settings%1.y : R.y;
    r *= R.z * settings%1.z;
    if (r > 0.0) c = c * (1.0 - r * b) + r * gain%1 * texture2D(environment%1, panorama(normalize(rotation%1 * reflected))).rgb;
  }
)GLSL")
                  .arg(i);
  shader += R"GLSL(
  c = display(c);
  c = colorScale.rgb + (1.0 - colorScale.rgb) * c;
  alpha *= colorScale.a;
  gl_FragColor = vec4(linear(c) * alpha, alpha);
}
)GLSL";
  return shader;
}
const char *screenVertex   = R"GLSL(
#version 120
attribute vec2 position;
varying vec2 uv;
void main() { gl_Position = vec4(position, 0.0, 1.0); uv = position * 0.5 + 0.5; }
)GLSL";
const char *screenFragment = R"GLSL(
#version 120
uniform sampler2D image;
varying vec2 uv;
void main() {
  vec4 p = texture2D(image, uv);
  vec3 c = p.a > 0.0 ? max(p.rgb / p.a, vec3(0.0)) : vec3(0.0);
  c = mix(12.92 * c, 1.055 * pow(c, vec3(1.0/2.4)) - 0.055, step(vec3(0.0031308), c));
  gl_FragColor = vec4(c * p.a, p.a);
}
)GLSL";
// Qt's surrounding viewer uses compatibility GL. Preserve all vertex attribute
// pointers as well as bindings; an enabled array alone is insufficient.
class State {
  QOpenGLFunctions *f;
  GLint program, buffer, framebuffer, active, viewport[4], blendSrc, blendDst;
  GLint blendSrcAlpha, blendDstAlpha, blendEquation, blendEquationAlpha,
      depthFunc;
  GLboolean depth, blend, cull, scissor, depthMask, colorMask[4];
  GLfloat clearColor[4], depthRange[2], clearDepth;
  struct Attribute {
    GLint enabled, size, type, normalized, stride, buffer;
    void *pointer;
  } attrs[6];
  GLint textures[5];
  bool hasSrgb   = false;
  GLboolean srgb = GL_FALSE;

public:
  explicit State(QOpenGLFunctions *functions) : f(functions) {
    auto *context = QOpenGLContext::currentContext();
    hasSrgb =
        context->format().majorVersion() >= 3 ||
        context->hasExtension(QByteArrayLiteral("GL_ARB_framebuffer_sRGB")) ||
        context->hasExtension(QByteArrayLiteral("GL_EXT_framebuffer_sRGB"));
    if (hasSrgb) {
      srgb = f->glIsEnabled(0x8DB9);
      f->glDisable(0x8DB9);
    }
    f->glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    f->glGetFloatv(GL_DEPTH_RANGE, depthRange);
    f->glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    f->glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    f->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    f->glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    f->glGetIntegerv(GL_VIEWPORT, viewport);
    f->glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrc);
    f->glGetIntegerv(GL_BLEND_DST_RGB, &blendDst);
    f->glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    f->glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
    f->glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquation);
    f->glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
    f->glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    depth   = f->glIsEnabled(GL_DEPTH_TEST);
    blend   = f->glIsEnabled(GL_BLEND);
    cull    = f->glIsEnabled(GL_CULL_FACE);
    scissor = f->glIsEnabled(GL_SCISSOR_TEST);
    f->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    f->glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    for (int i = 0; i < 5; ++i) {
      f->glActiveTexture(GL_TEXTURE0 + i);
      f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
    }
    for (int i = 0; i < 6; ++i) {
      auto &a = attrs[i];
      f->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a.enabled);
      f->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a.size);
      f->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a.type);
      f->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                             &a.normalized);
      f->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a.stride);
      f->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                             &a.buffer);
      f->glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                                   &a.pointer);
    }
  }
  ~State() {
    for (int i = 0; i < 6; ++i) {
      const auto &a = attrs[i];
      f->glBindBuffer(GL_ARRAY_BUFFER, a.buffer);
      f->glVertexAttribPointer(i, a.size, a.type, a.normalized, a.stride,
                               a.pointer);
      if (a.enabled)
        f->glEnableVertexAttribArray(i);
      else
        f->glDisableVertexAttribArray(i);
    }
    for (int i = 0; i < 5; ++i) {
      f->glActiveTexture(GL_TEXTURE0 + i);
      f->glBindTexture(GL_TEXTURE_2D, textures[i]);
    }
    f->glActiveTexture(active);
    f->glBindBuffer(GL_ARRAY_BUFFER, buffer);
    f->glUseProgram(program);
    f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    f->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    auto toggle = [&](GLenum cap, GLboolean value) {
      if (value)
        f->glEnable(cap);
      else
        f->glDisable(cap);
    };
    toggle(GL_DEPTH_TEST, depth);
    toggle(GL_BLEND, blend);
    toggle(GL_CULL_FACE, cull);
    toggle(GL_SCISSOR_TEST, scissor);
    if (hasSrgb) toggle(0x8DB9, srgb);
    f->glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    f->glDepthRangef(depthRange[0], depthRange[1]);
    f->glClearDepthf(clearDepth);
    f->glDepthMask(depthMask);
    f->glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    f->glDepthFunc(depthFunc);
    f->glBlendFuncSeparate(blendSrc, blendDst, blendSrcAlpha, blendDstAlpha);
    f->glBlendEquationSeparate(blendEquation, blendEquationAlpha);
  }
  void restoreScissor() {
    if (scissor) f->glEnable(GL_SCISSOR_TEST);
  }
  GLuint target() const { return GLuint(framebuffer); }
};
}  // namespace
class LiveGpu::Imp {
public:
  QPointer<QOpenGLContext> context;
  std::unique_ptr<QOpenGLShaderProgram> shader, screen;
  std::unique_ptr<QOpenGLFramebufferObject> target;
  std::shared_ptr<const LiveFrame> frame;
  std::vector<GLuint> buffers, textures;
  std::vector<std::array<GLuint, 5>> maps;
  GLuint quad             = 0;
  std::size_t uploadCount = 0, environmentUploadCount = 0;
  std::map<std::shared_ptr<const EnvironmentMap>, GLuint,
           std::owner_less<std::shared_ptr<const EnvironmentMap>>>
      environmentCache;
  QOpenGLFunctions *f = nullptr;
  void clearFrame(bool all = false) {
    if (all) {
      for (const auto &item : environmentCache)
        f->glDeleteTextures(1, &item.second);
      environmentCache.clear();
    }
    if (!buffers.empty())
      f->glDeleteBuffers(GLsizei(buffers.size()), buffers.data());
    if (!textures.empty())
      f->glDeleteTextures(GLsizei(textures.size()), textures.data());
    buffers.clear();
    textures.clear();
    maps.clear();
    frame.reset();
  }
  GLuint texture(int w, int h, const void *pixels, bool transient = true) {
    GLint limit = 0;
    f->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (w <= 0 || h <= 0 || w > limit || h > limit)
      throw std::runtime_error("Live Render texture exceeds GPU limits");
    GLuint id;
    f->glGenTextures(1, &id);
    if (transient) textures.push_back(id);
    f->glBindTexture(GL_TEXTURE_2D, id);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexImage2D(GL_TEXTURE_2D, 0, 0x8814, w, h, 0, GL_RGBA, GL_FLOAT,
                    pixels);  // RGBA32F retains environment HDR.
    if (f->glGetError() != GL_NO_ERROR)
      throw std::runtime_error(
          "Live Render could not upload a floating-point texture");
    return id;
  }
  void upload(const std::shared_ptr<const LiveFrame> &value) {
    clearFrame();
    std::map<const EnvironmentMap *, GLuint> cache;
    std::size_t bytes = 0;
    for (const auto &b : value->batches) {
      bytes += b.vertices.size() * sizeof(LiveVertex);
      for (const auto &l : b.reflections)
        if (!cache.count(l.map.get())) {
          bytes += std::size_t(l.map->width()) * l.map->height() * 16;
          cache[l.map.get()] = 0;
        }
      if (b.texture) bytes += b.texture->size() * sizeof(ColorPixel);
    }
    if (bytes > 384ull * 1024 * 1024)
      throw std::runtime_error(
          "Live Render exceeds its 384 MiB resource budget");
    for (auto i = environmentCache.begin(); i != environmentCache.end();) {
      if (!cache.count(i->first.get())) {
        f->glDeleteTextures(1, &i->second);
        i = environmentCache.erase(i);
      } else
        ++i;
    }
    for (const auto &b : value->batches) {
      GLuint id;
      f->glGenBuffers(1, &id);
      buffers.push_back(id);
      f->glBindBuffer(GL_ARRAY_BUFFER, id);
      f->glBufferData(GL_ARRAY_BUFFER,
                      GLsizeiptr(b.vertices.size() * sizeof(LiveVertex)),
                      b.vertices.data(), GL_STATIC_DRAW);
      if (f->glGetError() != GL_NO_ERROR)
        throw std::runtime_error("Live Render could not upload geometry");
      std::array<GLuint, 5> ids{};
      for (std::size_t i = 0; i < b.reflections.size(); ++i) {
        const auto &m = *b.reflections[i].map;
        auto &cached  = environmentCache[b.reflections[i].map];
        if (!cached) {
          std::vector<ColorPixel> pixels;
          pixels.reserve(m.pixels().size());
          for (const auto &p : m.pixels())
            pixels.push_back({p[0], p[1], p[2], 1});
          cached = texture(m.width(), m.height(), pixels.data(), false);
          ++environmentUploadCount;
        }
        ids[i] = cached;
      }
      if (b.texture)
        ids[4] = texture(b.textureWidth, b.textureHeight, b.texture->data());
      maps.push_back(ids);
    }
    frame = value;
    ++uploadCount;
  }
};
LiveGpu::LiveGpu() : m_imp(new Imp) {}
LiveGpu::~LiveGpu() { release(); }
void LiveGpu::release() {
  if (!m_imp->context && !m_imp->shader) return;
  if (QOpenGLContext::currentContext() == m_imp->context) {
    m_imp->clearFrame(true);
    if (m_imp->quad) m_imp->f->glDeleteBuffers(1, &m_imp->quad);
    m_imp->target.reset();
    m_imp->shader.reset();
    m_imp->screen.reset();
  }
  // The viewer releases before context destruction. A lost context has already
  // reclaimed its GL objects; never delete names through a replacement context.
  else {
    m_imp->target.reset();
    m_imp->shader.reset();
    m_imp->screen.reset();
  }
  m_imp.reset(new Imp);
}
std::size_t LiveGpu::uploads() const { return m_imp->uploadCount; }
std::size_t LiveGpu::environmentUploads() const {
  return m_imp->environmentUploadCount;
}
bool LiveGpu::draw(const std::shared_ptr<const LiveFrame> &frame,
                   const QMatrix4x4 &imageToClip, int width, int height,
                   QString &error) {
  if (!frame || width <= 0 || height <= 0) return false;
  auto *context = QOpenGLContext::currentContext();
  if (!context) {
    error = "Live Render has no graphics context";
    return false;
  }
  if (m_imp->context != context && m_imp->shader) release();
  auto &p   = *m_imp;
  p.context = context;
  p.f       = context->functions();
  State state(p.f);
  try {
    if (!p.shader) {
      if (context->isOpenGLES())
        throw std::runtime_error("Live Render requires desktop OpenGL");
      if (!context->hasExtension(QByteArrayLiteral("GL_ARB_texture_float")) &&
          context->format().majorVersion() < 3)
        throw std::runtime_error(
            "Live Render requires floating-point textures");
      p.shader.reset(new QOpenGLShaderProgram);
      p.screen.reset(new QOpenGLShaderProgram);
      auto compile = [](QOpenGLShaderProgram &s, const char *v,
                        const QByteArray &f) {
        if (!s.addShaderFromSourceCode(QOpenGLShader::Vertex, v) ||
            !s.addShaderFromSourceCode(QOpenGLShader::Fragment, f))
          throw std::runtime_error(s.log().toStdString());
        const char *names[] = {"position", "normal",     "view",
                               "color",    "reflection", "uv"};
        for (int i = 0; i < 6; ++i) s.bindAttributeLocation(names[i], i);
        if (!s.link()) throw std::runtime_error(s.log().toStdString());
      };
      compile(*p.shader, vertexShader, fragmentShader().toUtf8());
      compile(*p.screen, screenVertex, screenFragment);
      const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
      p.f->glGenBuffers(1, &p.quad);
      p.f->glBindBuffer(GL_ARRAY_BUFFER, p.quad);
      p.f->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    }
    p.f->glActiveTexture(GL_TEXTURE0);
    if (p.frame != frame) p.upload(frame);
    if (!p.target || p.target->size() != QSize(width, height)) {
      if (std::size_t(width) * height > 16 * 1024 * 1024)
        throw std::runtime_error("Live Render viewport exceeds 16 megapixels");
      QOpenGLFramebufferObjectFormat format;
      format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
      format.setInternalTextureFormat(0x8814);  // RGBA32F
      p.target.reset(new QOpenGLFramebufferObject(width, height, format));
      if (!p.target->isValid())
        throw std::runtime_error(
            "Live Render cannot allocate a floating-point framebuffer");
    }
    p.target->bind();
    p.f->glViewport(0, 0, width, height);
    p.f->glDisable(GL_SCISSOR_TEST);
    p.f->glDisable(GL_CULL_FACE);
    p.f->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    p.f->glClearColor(0, 0, 0, 0);
    p.f->glClear(GL_COLOR_BUFFER_BIT);
    p.f->glEnable(GL_BLEND);
    p.f->glBlendEquation(GL_FUNC_ADD);
    p.f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    p.f->glDepthRangef(0, 1);
    p.f->glClearDepthf(1);
    p.f->glEnable(GL_DEPTH_TEST);
    p.f->glDepthFunc(GL_LEQUAL);
    p.shader->bind();
    for (std::size_t i = 0; i < frame->batches.size(); ++i) {
      const auto &b = frame->batches[i];
      p.f->glDepthMask(GL_TRUE);
      p.f->glClear(GL_DEPTH_BUFFER_BIT);
      QMatrix4x4 a;
      a(0, 0) = b.affine[0];
      a(0, 1) = b.affine[1];
      a(0, 3) = b.affine[2];
      a(1, 0) = b.affine[3];
      a(1, 1) = b.affine[4];
      a(1, 3) = b.affine[5];
      p.shader->setUniformValue("imageToClip", imageToClip * a);
      p.shader->setUniformValue("colorScale",
                                QVector4D(b.colorScale[0], b.colorScale[1],
                                          b.colorScale[2], b.colorScale[3]));
      for (int k = 0; k < 4; ++k) {
        p.f->glActiveTexture(GL_TEXTURE0 + k);
        p.f->glBindTexture(GL_TEXTURE_2D, p.maps[i][k]);
        auto uniform = [&](const char *name) {
          return QString("%1%2").arg(name).arg(k).toLatin1();
        };
        p.shader->setUniformValue(uniform("environment").constData(), k);
        const bool enabled = std::size_t(k) < b.reflections.size();
        QMatrix3x3 rotation;
        QVector3D gain, settings(0, 0, 0);
        if (enabled) {
          const auto &l = b.reflections[k];
          for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
              rotation(r, c) = float(l.worldToMap[r * 3 + c]);
          gain     = QVector3D(l.gain[0], l.gain[1], l.gain[2]);
          settings = QVector3D(l.overrideMaterial ? float(l.reflectivity) : -1,
                               l.overrideMaterial ? float(l.blend) : -1, 1);
        }
        p.shader->setUniformValue(uniform("rotation").constData(), rotation);
        p.shader->setUniformValue(uniform("gain").constData(), gain);
        p.shader->setUniformValue(uniform("settings").constData(), settings);
      }
      p.f->glActiveTexture(GL_TEXTURE4);
      p.f->glBindTexture(GL_TEXTURE_2D, p.maps[i][4]);
      p.shader->setUniformValue("image", 4);
      p.f->glBindBuffer(GL_ARRAY_BUFFER, p.buffers[i]);
      const int sizes[]           = {4, 3, 3, 4, 3, 2};
      const std::size_t offsets[] = {
          offsetof(LiveVertex, position),   offsetof(LiveVertex, normal),
          offsetof(LiveVertex, view),       offsetof(LiveVertex, color),
          offsetof(LiveVertex, reflection), offsetof(LiveVertex, uv)};
      for (int k = 0; k < 6; ++k) {
        p.f->glEnableVertexAttribArray(k);
        p.f->glVertexAttribPointer(k, sizes[k], GL_FLOAT, GL_FALSE,
                                   sizeof(LiveVertex),
                                   reinterpret_cast<void *>(offsets[k]));
      }
      // Wireframe is retained by the surrounding viewer's compatibility state.
      p.f->glDrawArrays(GL_TRIANGLES, 0, GLsizei(b.opaqueVertices));
      p.f->glDepthMask(GL_FALSE);
      p.f->glDrawArrays(GL_TRIANGLES, GLint(b.opaqueVertices),
                        GLsizei(b.vertices.size() - b.opaqueVertices));
    }
    p.f->glBindFramebuffer(GL_FRAMEBUFFER, state.target());
    p.f->glViewport(0, 0, width, height);
    p.f->glDisable(GL_DEPTH_TEST);
    state.restoreScissor();
    p.screen->bind();
    p.f->glActiveTexture(GL_TEXTURE0);
    p.f->glBindTexture(GL_TEXTURE_2D, p.target->texture());
    p.screen->setUniformValue("image", 0);
    p.f->glBindBuffer(GL_ARRAY_BUFFER, p.quad);
    p.f->glEnableVertexAttribArray(0);
    p.f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    for (int k = 1; k < 6; ++k) p.f->glDisableVertexAttribArray(k);
    p.f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    return true;
  } catch (const std::exception &e) {
    error = QString::fromUtf8(e.what());
    p.clearFrame(true);
    if (p.quad) p.f->glDeleteBuffers(1, &p.quad);
    p.quad = 0;
    p.shader.reset();
    p.screen.reset();
    p.target.reset();
    return false;
  }
}
}  // namespace otglb
