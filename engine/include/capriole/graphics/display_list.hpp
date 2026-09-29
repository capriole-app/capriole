#pragma once

// The display list: what a page draws, in order, as commands a rasterizer (or the SVG exporter) can
// replay without knowing anything about PDF syntax. The interpreter (content/interpreter.hpp) builds
// it. Geometry is in the page's default user space (§8.3.2.3): each command carries the CTM in force,
// so a stroke keeps its line width in its own space and a renderer only adds the page-to-device matrix.
//
// Colours are kept as the document gives them (space + components) together with an sRGB
// approximation; exact colour management (ICC, spot colours) is a renderer concern for later.

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "capriole/graphics/geometry.hpp"
#include "capriole/model/object.hpp"

namespace capriole::graphics {

// ── Paths ───────────────────────────────────────────────────────────────────────────────────

enum class SegmentKind : std::uint8_t { kMove, kLine, kCubic, kClose };

// A path, compact: one verb per segment and only the points it needs (one for a move or a line,
// three for a cubic, none for a close) — dense vector pages hold hundreds of thousands of segments.
class Path {
 public:
  void move_to(Point p) { add(SegmentKind::kMove, {p}); }
  void line_to(Point p) { add(SegmentKind::kLine, {p}); }
  void cubic_to(Point c1, Point c2, Point end) { add(SegmentKind::kCubic, {c1, c2, end}); }
  void close() { verbs_.push_back(SegmentKind::kClose); }

  [[nodiscard]] bool empty() const noexcept { return verbs_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return verbs_.size(); }
  [[nodiscard]] const std::vector<SegmentKind>& verbs() const noexcept { return verbs_; }

  // The number of points a segment of this kind carries.
  static constexpr std::size_t points_of(SegmentKind kind) noexcept {
    switch (kind) {
      case SegmentKind::kCubic:
        return 3;
      case SegmentKind::kClose:
        return 0;
      case SegmentKind::kMove:
      case SegmentKind::kLine:
        return 1;
    }
    return 0;
  }

  // Calls f(kind, points) for each segment, points pointing at its 0, 1 or 3 points.
  template <typename F>
  void for_each(F f) const {
    std::size_t at = 0;
    for (const SegmentKind kind : verbs_) {
      f(kind, points_.data() + at);
      at += points_of(kind);
    }
  }

 private:
  void add(SegmentKind kind, std::initializer_list<Point> points) {
    verbs_.push_back(kind);
    points_.insert(points_.end(), points);
  }

  std::vector<SegmentKind> verbs_;
  std::vector<Point> points_;
};

enum class FillRule : std::uint8_t { kNonZero, kEvenOdd };

// ── Paint ───────────────────────────────────────────────────────────────────────────────────

enum class ColorSpaceFamily : std::uint8_t { kGray, kRGB, kCMYK, kLab, kICCBased, kIndexed, kSeparation, kDeviceN, kPattern, kUnknown };

struct Rgb {
  double r = 0;
  double g = 0;
  double b = 0;
  constexpr Rgb() = default;
  constexpr Rgb(double red, double green, double blue) noexcept : r(red), g(green), b(blue) {}
  friend bool operator==(const Rgb&, const Rgb&) = default;
};

struct Color {
  ColorSpaceFamily family = ColorSpaceFamily::kGray;
  std::vector<double> components{0};
  std::string pattern;  // kPattern: the pattern resource name
  Rgb rgb;              // sRGB approximation, for previews
};

enum class LineCap : std::uint8_t { kButt, kRound, kSquare };
enum class LineJoin : std::uint8_t { kMiter, kRound, kBevel };

struct StrokeStyle {
  double width = 1;
  LineCap cap = LineCap::kButt;
  LineJoin join = LineJoin::kMiter;
  double miter_limit = 10;
  std::vector<double> dash;
  double dash_phase = 0;
};

// ── Transparency (§11) ──────────────────────────────────────────────────────────────────────

// The standard blend modes (§11.3.5): the first twelve separable (per colour component), the last four
// non-separable (on the colour as a whole). Compatible is Normal.
enum class BlendMode : std::uint8_t {
  kNormal,
  kMultiply,
  kScreen,
  kOverlay,
  kDarken,
  kLighten,
  kColorDodge,
  kColorBurn,
  kHardLight,
  kSoftLight,
  kDifference,
  kExclusion,
  kHue,
  kSaturation,
  kColor,
  kLuminosity,
};

[[nodiscard]] constexpr bool is_separable(BlendMode mode) noexcept { return mode < BlendMode::kHue; }

// The mode a /BM name gives; nullopt for a name that is not a blend mode (§11.6.3: a reader uses the
// first mode it recognises in an array, Normal when none).
[[nodiscard]] std::optional<BlendMode> blend_mode_named(std::string_view name) noexcept;

// The PDF name of the mode (Multiply, ColorDodge, ...).
[[nodiscard]] std::string_view blend_mode_name(BlendMode mode) noexcept;

// ── Commands ────────────────────────────────────────────────────────────────────────────────

struct Save {};
struct Restore {};

struct ClipPath {
  Path path;
  Matrix ctm;
  FillRule rule = FillRule::kNonZero;
};

// Colours and stroke styles are stored once in the display list (DisplayList::colors, ::styles) and
// referred to by index: consecutive commands almost always share them.
using ColorIndex = std::uint32_t;
using StyleIndex = std::uint32_t;

struct FillPath {
  Path path;
  Matrix ctm;
  FillRule rule = FillRule::kNonZero;
  BlendMode blend = BlendMode::kNormal;
  ColorIndex color = 0;
  double alpha = 1;
};

struct StrokePath {
  Path path;
  Matrix ctm;
  StyleIndex style = 0;
  ColorIndex color = 0;
  double alpha = 1;
  BlendMode blend = BlendMode::kNormal;
};

// Decoded image samples: straight RGBA, 8 bits per channel, the first row of the image at the top of
// its unit square. Shared: an image drawn many times is decoded once per page.
struct ImagePixels {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::vector<std::uint8_t> rgba;
};

// An image in the unit square of its CTM (§8.9.4): where it goes, where its data is, and its pixels
// when the interpreter could decode them (null: a placeholder is drawn).
struct DrawImage {
  Matrix ctm;
  std::optional<model::ObjectRef> xobject;  // an image XObject, or
  model::Dict inline_dict;                  // the dictionary of an inline image
  std::shared_ptr<const std::vector<std::byte>> inline_data;
  std::int64_t width = 0;
  std::int64_t height = 0;
  bool mask = false;     // an image mask (/ImageMask true): painted with the fill colour
  ColorIndex color = 0;  // the fill colour, for image masks
  double alpha = 1;
  BlendMode blend = BlendMode::kNormal;
  std::shared_ptr<const ImagePixels> pixels;
  bool interpolate = false;  // /Interpolate: smooth when drawn larger (drawn smaller, images are always smoothed)
};

// A shading (§8.7.4.5), as the interpreter prepares it for any renderer: geometry in shading space,
// colours already in sRGB (graphics/shading.hpp computes its pixels).
struct Shading {
  enum class Kind : std::uint8_t {
    kFunction,  // type 1: a colour for each point of a domain
    kAxial,     // type 2: along the axis from (x0, y0) to (x1, y1)
    kRadial,    // type 3: between the circles (x0, y0, r0) and (x1, y1, r1)
    kMesh,      // types 4–7: triangles with a colour at each corner (patches are subdivided)
  };
  Kind kind = Kind::kAxial;
  std::array<double, 6> coords{};  // axial: x0 y0 x1 y1; radial: x0 y0 r0 x1 y1 r1
  // Axial and radial: t runs from t0 to t1 along the geometry; beyond it, the end colours when extended.
  double t0 = 0;
  double t1 = 1;
  bool extend_start = false;
  bool extend_end = false;
  // The colours of t from t0 to t1, evenly spaced (axial, radial, and meshes whose corners give t).
  std::vector<Rgb> ramp;
  // Function shadings: the domain, its matrix into shading space, and colours sampled on a grid over the
  // domain (grid_width × grid_height, row 0 at the domain's y0).
  Rect domain{0, 0, 1, 1};
  Matrix matrix;
  std::int32_t grid_width = 0;
  std::int32_t grid_height = 0;
  std::vector<Rgb> grid;
  // Meshes: each corner has a colour, or with a ramp a t (interpolated, then looked up: §8.7.4.5.5).
  struct Vertex {
    Point point;
    Rgb color;
    double t = 0;
  };
  std::vector<std::array<Vertex, 3>> triangles;
  // /Background (painted first where the shading is a pattern's), /BBox (in shading space).
  std::optional<Rgb> background;
  std::optional<Rect> bbox;
};

// A shading painted over the current clip (sh, §8.7.4.2), or over the area a shading pattern fills
// (§8.7.3.3, after a ClipPath of that area; with its background).
struct PaintShading {
  Matrix ctm;  // shading space → user space
  std::shared_ptr<const Shading> shading;
  double alpha = 1;
  BlendMode blend = BlendMode::kNormal;
  bool background = false;  // a pattern fill: /Background is painted too
};

struct DisplayList;

// A tiling pattern (§8.7.3.2) painted over the current clip (a ClipPath of the area it fills comes
// first): its cell — the pattern's content as a display list of its own, in pattern space, painted over
// the rectangle bbox — repeated every xstep, ystep.
struct PaintTiling {
  Matrix matrix;  // pattern space → user space
  std::shared_ptr<const DisplayList> cell;
  Rect bbox;
  double xstep = 1;
  double ystep = 1;
  double alpha = 1;
  BlendMode blend = BlendMode::kNormal;
};

// A run of glyphs of one Tj/TJ/'/" with their positions and outlines.
struct Glyph {
  std::uint32_t code = 0;  // the character code (1 to 4 bytes, as the font's CMap splits them)
  std::uint32_t cid = 0;   // composite fonts: the CID the code selects; the code itself otherwise
  Point origin;            // in text space, from the start of the run (the text rise included in y)
  double advance = 0;      // in text space units, after spacing and scaling (vertical fonts: downwards)
  // The glyph in text space for a font size of 1 (font/font.hpp); null when there is nothing to draw — a
  // missing glyph, or a Type 3 font, whose glyphs are the commands that follow the run.
  std::shared_ptr<const Path> outline;
};

struct ShowText {
  // Text space → user space at the start of the run: Tm × CTM, and Tm alone (strokes keep their line
  // width in user space). A glyph is drawn with the matrix [size·horizontal_scale 0 0 size origin.x
  // origin.y] × text_matrix (§9.4.4).
  Matrix text_matrix;
  Matrix tm;
  Matrix ctm;
  std::string font;  // the font resource name (/F1)
  std::optional<model::ObjectRef> font_ref;
  double font_size = 0;
  double horizontal_scale = 1;   // Tz / 100
  std::int32_t render_mode = 0;  // Tr (§9.3.6): fill, stroke, both, invisible; 4–7 also clip (a ClipPath at ET)
  std::string bytes;             // the string shown, as bytes
  std::vector<Glyph> glyphs;
  ColorIndex fill = 0;
  ColorIndex stroke = 0;
  StyleIndex stroke_style = 0;
  double alpha = 1;         // of the fill
  double stroke_alpha = 1;  // of the stroke
  BlendMode blend = BlendMode::kNormal;
  bool widths_known = false;      // false: the font gives no widths, advances are estimated
  bool latin_codes = false;       // the codes read as Latin text (a standard encoding); otherwise unknown until ToUnicode
  bool glyph_procedures = false;  // a Type 3 font: the glyphs are drawn by the commands after the run
};

// The outlines of a run's glyphs as one path in user space (to be drawn with the run's CTM, nonzero).
[[nodiscard]] Path glyph_run_path(const ShowText& run);

// A transparency group (§11.4.7, a form XObject with /Group /S /Transparency): what is drawn up to the
// matching EndGroup is composed on its own, then composited as one object with `alpha`, `blend` and the
// soft mask in force at BeginGroup. Inside the group the alpha, blend mode and soft mask start again
// from their initial values (§11.6.6).
struct BeginGroup {
  double alpha = 1;
  BlendMode blend = BlendMode::kNormal;
  bool isolated = false;
  bool knockout = false;
};
struct EndGroup {};

// A soft mask (§11.6.5, the /SMask of a graphics state): the commands up to the matching EndSoftMask
// draw the mask's group — on transparent black for kAlpha, on the opaque `backdrop` colour for
// kLuminosity — and then its alpha or luminosity becomes the mask. The mask applies to what is painted
// after it until the saved state it was set in is restored, another mask is set, or ClearSoftMask.
enum class SoftMaskKind : std::uint8_t { kAlpha, kLuminosity };
struct BeginSoftMask {
  SoftMaskKind kind = SoftMaskKind::kAlpha;
  Rgb backdrop;
  // The transfer function /TR sampled at the 256 mask values (value → table[value]); null: identity.
  std::shared_ptr<const std::vector<std::uint8_t>> transfer;
};
struct EndSoftMask {};
struct ClearSoftMask {};

// A value on the heap that copies like a value: the large, rarer commands are boxed so that they do not
// set the size of every command (a variant is as large as its largest alternative, and most commands
// of a page are small).
template <typename T>
class Boxed {
 public:
  Boxed(T value) : value_(std::make_unique<T>(std::move(value))) {}  // NOLINT(google-explicit-constructor): a transparent wrapper
  Boxed(const Boxed& other) : value_(std::make_unique<T>(*other.value_)) {}
  Boxed& operator=(const Boxed& other) {
    if (this != &other) {
      value_ = std::make_unique<T>(*other.value_);
    }
    return *this;
  }
  Boxed(Boxed&&) noexcept = default;
  Boxed& operator=(Boxed&&) noexcept = default;
  ~Boxed() = default;

  [[nodiscard]] const T& operator*() const noexcept { return *value_; }
  [[nodiscard]] const T* operator->() const noexcept { return value_.get(); }

 private:
  std::unique_ptr<T> value_;
};

using Command = std::variant<Save, Restore, ClipPath, FillPath, StrokePath, Boxed<DrawImage>, Boxed<PaintShading>, Boxed<PaintTiling>, Boxed<ShowText>,
                             BeginGroup, EndGroup, BeginSoftMask, EndSoftMask, ClearSoftMask>;

// A short name for the kind of a command ("fill", "group", "soft_mask", "group_end", ...), for summaries.
[[nodiscard]] std::string_view command_kind(const Command& command);

template <typename T, typename Variant>
struct IsAlternative;
template <typename T, typename... Alternatives>
struct IsAlternative<T, std::variant<Alternatives...>> : std::bool_constant<(std::is_same_v<T, Alternatives> || ...)> {};

// The command as a T when it holds one, boxed or not; nullptr otherwise.
template <typename T>
[[nodiscard]] const T* command_as(const Command& command) noexcept {
  if constexpr (IsAlternative<T, Command>::value) {
    return std::get_if<T>(&command);
  } else {
    static_assert(IsAlternative<Boxed<T>, Command>::value, "not a command type");
    const Boxed<T>* boxed = std::get_if<Boxed<T>>(&command);
    return boxed != nullptr ? &**boxed : nullptr;
  }
}

struct DisplayList {
  Rect media_box;  // the page boxes (§14.11.2), in default user space
  Rect crop_box;
  std::int32_t rotate = 0;  // /Rotate, normalized to 0, 90, 180, 270
  std::vector<Command> commands;
  std::vector<Color> colors;        // referred to by ColorIndex
  std::vector<StrokeStyle> styles;  // referred to by StyleIndex
  // What the interpreter could not do or had to guess: shown to a person, not a failure.
  std::vector<std::string> warnings;
  // Resources the page needed that the host did not supply (font/resources.hpp), sorted.
  std::vector<std::string> missing_resources;
};

// Where a page goes on an output (an SVG, a bitmap): the crop box, y flipped (outputs grow downwards),
// turned clockwise by /Rotate, scaled by `scale` output units per point (1 for SVG; dpi / 72 for pixels).
struct OutputGeometry {
  Matrix page_to_output;
  double width = 0;
  double height = 0;
};

[[nodiscard]] inline OutputGeometry output_geometry(const DisplayList& list, double scale) {
  const Rect box = list.crop_box;
  const double w = box.width();
  const double h = box.height();
  auto m = Matrix{1, 0, 0, -1, -box.x0, box.y1};
  OutputGeometry out{.page_to_output = m, .width = w, .height = h};
  switch (list.rotate) {
    case 90:
      out = {.page_to_output = m.then(Matrix{0, 1, -1, 0, h, 0}), .width = h, .height = w};
      break;
    case 180:
      out = {.page_to_output = m.then(Matrix{-1, 0, 0, -1, w, h}), .width = w, .height = h};
      break;
    case 270:
      out = {.page_to_output = m.then(Matrix{0, -1, 1, 0, 0, w}), .width = h, .height = w};
      break;
    default:
      break;
  }
  out.page_to_output = out.page_to_output.then(Matrix::scale(scale, scale));
  out.width *= scale;
  out.height *= scale;
  return out;
}

}  // namespace capriole::graphics
