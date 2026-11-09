#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Native, read-only A:M data. No Hash SDK runtime, Qt, MFC, or graphics
// context. Source bytes and CP occurrences remain authoritative; Mesh is a
// disposable experimental preview, NOT a claim of Hash patch evaluation
// compatibility.
namespace otam {
using Id     = std::int64_t;
using Vec3   = std::array<double, 3>;
using Bias   = std::array<double, 3>;  // Alpha, gamma, magnitude.
using Color  = std::array<float, 3>;
using Matrix = std::array<double, 16>;  // Row major; column vectors.

struct Limits {
  std::size_t bytes     = 64 * 1024 * 1024;
  std::size_t records   = 1000000;
  std::size_t triangles = 500000;
  unsigned depth        = 64;
};
struct SourceNode {
  std::string name;
  std::size_t begin = 0, contentBegin = 0, contentEnd = 0, end = 0;
  std::vector<std::size_t> children;
};
struct ControlPoint {
  Id id = 0, attachedTo = -1;
  std::uint32_t flags = 0;
  Vec3 position{{0, 0, 0}};
  // Incoming alpha/gamma/magnitude, outgoing alpha/gamma/magnitude.
  std::array<double, 6> bias{{0, 0, 1, 0, 0, 1}};
  std::string sourceRecord;
  std::size_t head = 0;
};
struct Spline {
  std::vector<std::size_t> points;  // CP occurrences, never welded by position.
  bool closed = false;
};
struct Patch {
  std::uint32_t flags = 0;
  std::vector<Id> points;
  std::string sourceRecord;
};
struct Group {
  std::string name;
  std::vector<Id> points;
  Color color{{0.65f, 0.65f, 0.65f}};
  bool hasColor = false;
};
struct Model {
  std::string name;
  std::vector<ControlPoint> points;
  std::vector<Spline> splines;
  std::vector<Patch> patches;
  std::vector<Group> groups;
};
// The two handles belong to spline occurrences. Their origins belong to
// attachment heads. The same cubic is used by the overlay and patch preview.
struct BiasHandle {
  Vec3 origin, forward, side, normal;
  double length = 0;
  Bias bias{};

  Vec3 point() const;
  Bias biasAt(const Vec3 &point, bool magnitudeOnly) const;
};

struct SplineSegment {
  std::size_t from, to;
  BiasHandle outgoing, incoming;

  std::array<Vec3, 4> bezier() const;
};

Vec3 sampleSpline(const std::array<Vec3, 4> &points, double t);
SplineSegment splineSegment(const Model &model, const Spline &spline,
                            std::size_t start);
struct Document {
  std::string source;
  std::vector<SourceNode> nodes;
  std::vector<Model> models;
  std::vector<std::string> warnings;
  std::string version;
};
struct Triangle {
  std::array<Vec3, 3> vertices;
  Color color;
  std::size_t patch = 0;
  int colorGroup    = -1;  // -1 is the default surface.
  // Smooth derivatives of the preview patch, independent of tessellation.
  // Empty/false retains flat shading for triangles and manually built meshes.
  std::array<Vec3, 3> normals{};
  bool hasNormals = false;
};
struct Mesh {
  // All containing groups, independent of the palette color winner.
  std::vector<std::vector<std::string>> patchGroups;
  bool amMaterials = true;
  std::vector<Triangle> triangles;
  std::vector<std::string> warnings;
};

// Bounded native tagged-source reader shared by model and material documents.
Document parseSource(std::string source, const std::string &root,
                     const Limits &limits = Limits());
Document parse(std::string source, const Limits &limits = Limits());
Mesh previewMesh(const Document &document, std::size_t model = 0,
                 unsigned subdivisions = 6, const Limits &limits = Limits());
Mesh previewMesh(const Model &model, const std::vector<std::string> &warnings,
                 unsigned subdivisions = 6, const Limits &limits = Limits());
Matrix identity();
Matrix multiply(const Matrix &a, const Matrix &b);
// Finite affine matrices only. Throws for a singular camera pose.
Matrix inverse(const Matrix &matrix);
Matrix translation(double x, double y, double z);
Matrix scaling(double x, double y, double z);
Matrix rotation(double xDegrees, double yDegrees, double zDegrees);
Vec3 transform(const Matrix &matrix, const Vec3 &point);
Mesh transformed(const Mesh &mesh, const Matrix &matrix);
// Clip to z < cameraZ + focus - nearPlane before perspective projection.
// Input/output XY use stage coordinates, not pixels. Z stores reciprocal camera
// distance for screen-space depth testing.
Mesh projected(const Mesh &mesh, double cameraX, double cameraY, double cameraZ,
               double focus = 1000.0, double nearPlane = 1.0);
// Optical camera pose, looking along local -Z. imageToStage embeds the
// legacy output-plane affine so the downstream OT camera affine is applied
// exactly once. It must preserve depth (the Z coordinate). Smooth shading
// normals remain in world space after projection.
Mesh projected(const Mesh &mesh, const Matrix &cameraToWorld,
               const Matrix &imageToStage, double focus = 1000.0,
               double nearPlane = 1.0);
// Tile-local triangle rasterization. Larger z is closer. Transparent
// background. Capacity and finite-coordinate validation are applied before
// writing pixels.
std::vector<std::array<float, 4>> rasterize(const Mesh &pixelMesh, int width,
                                            int height);
}  // namespace otam
