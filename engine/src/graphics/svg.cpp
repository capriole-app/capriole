#include "capriole/graphics/svg.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "capriole/graphics/shading.hpp"
#include "capriole/render/png.hpp"

namespace capriole::graphics {

namespace {

// A number in SVG syntax: finite, up to 4 decimals, no trailing zeros.
std::string num(double v) {
  if (!std::isfinite(v)) {
    return "0";
  }
  std::string text = std::format("{:.4f}", v);
  while (text.back() == '0') {
    text.pop_back();
  }
  if (text.back() == '.') {
    text.pop_back();
  }
  return text == "-0" ? "0" : text;
}

std::string matrix(const Matrix& m) { return "matrix(" + num(m.a) + " " + num(m.b) + " " + num(m.c) + " " + num(m.d) + " " + num(m.e) + " " + num(m.f) + ")"; }

std::string color(const Rgb& rgb) {
  const auto channel = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
  return std::format("#{:02x}{:02x}{:02x}", channel(rgb.r), channel(rgb.g), channel(rgb.b));
}

std::string path_data(const Path& path) {
  std::string d;
  path.for_each([&d](SegmentKind kind, const Point* p) {
    switch (kind) {
      case SegmentKind::kMove:
        d += "M" + num(p[0].x) + " " + num(p[0].y);
        break;
      case SegmentKind::kLine:
        d += "L" + num(p[0].x) + " " + num(p[0].y);
        break;
      case SegmentKind::kCubic:
        d += "C" + num(p[0].x) + " " + num(p[0].y) + " " + num(p[1].x) + " " + num(p[1].y) + " " + num(p[2].x) + " " + num(p[2].y);
        break;
      case SegmentKind::kClose:
        d += "Z";
        break;
    }
  });
  return d;
}

// The CSS mix-blend-mode of a mode (CSS Compositing uses the formulas of §11.3.5): ColorDodge →
// color-dodge.
std::string blend_css(BlendMode mode) {
  std::string css;
  for (const char c : blend_mode_name(mode)) {
    if (c >= 'A' && c <= 'Z') {
      css += css.empty() ? "" : "-";
      css += static_cast<char>(c - 'A' + 'a');
    } else {
      css += c;
    }
  }
  return "mix-blend-mode:" + css;
}

std::string blend_style(BlendMode mode) { return mode == BlendMode::kNormal ? std::string() : " style=\"" + blend_css(mode) + "\""; }

std::string base64(std::span<const std::byte> bytes) {
  constexpr std::string_view kDigits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const std::size_t left = bytes.size() - i;
    const std::uint32_t v = (std::to_integer<std::uint32_t>(bytes[i]) << 16U) | (left > 1 ? std::to_integer<std::uint32_t>(bytes[i + 1]) << 8U : 0U) |
                            (left > 2 ? std::to_integer<std::uint32_t>(bytes[i + 2]) : 0U);
    out += kDigits[(v >> 18U) & 63U];
    out += kDigits[(v >> 12U) & 63U];
    out += left > 1 ? kDigits[(v >> 6U) & 63U] : '=';
    out += left > 2 ? kDigits[v & 63U] : '=';
  }
  return out;
}

std::string opacity(std::string_view attribute, double alpha) {
  return alpha < 1 ? std::format(" {}=\"{}\"", attribute, num(std::max(alpha, 0.0))) : std::string();
}

class Writer {
 public:
  explicit Writer(const SvgOptions& options, std::string prefix = {}) : options_(options), prefix_(std::move(prefix)) {}

  std::string write(const DisplayList& list) {
    list_ = &list;
    // Page space (y up) → SVG (y down), turned by /Rotate: see output_geometry.
    const OutputGeometry geometry = output_geometry(list, 1);
    const double out_w = geometry.width;
    const double out_h = geometry.height;
    const Matrix page = geometry.page_to_output;
    out_ =
        std::format(R"(<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}" viewBox="0 0 {} {}">)", num(out_w), num(out_h), num(out_w), num(out_h));
    out_ += R"(<rect width="100%" height="100%" fill="#ffffff"/>)";
    // Isolated from the white page: blend modes act on the content, not on the paper.
    out_ += "<g transform=\"" + matrix(page) + R"(" style="isolation:isolate">)";
    body(list);
    out_ += "</g></svg>";
    return std::move(out_);
  }

  // The commands of a list, balanced (every group opened is closed), appended to the output: a page's,
  // or a pattern cell's.
  void body(const DisplayList& list) {
    list_ = &list;
    open_.assign(1, {});
    page_box_ = list.crop_box;
    std::size_t written = 0;
    for (const Command& command : list.commands) {
      if (written++ >= options_.max_commands) {
        out_ += "<!-- more commands not written -->";
        break;
      }
      std::visit([this](const auto& c) { this->command(c); }, command);
    }
    while (!masks_.empty()) {
      end_mask();
    }
    while (!open_.empty()) {
      close_groups();
      open_.pop_back();
    }
  }

 private:
  // Indices out of range (a list built by hand) read as black and the default style.
  [[nodiscard]] Rgb rgb(ColorIndex index) const { return index < list_->colors.size() ? list_->colors[index].rgb : Rgb{}; }
  [[nodiscard]] StrokeStyle style(StyleIndex index) const { return index < list_->styles.size() ? list_->styles[index] : StrokeStyle{}; }

  template <typename T>
  void command(const Boxed<T>& boxed) {
    command(*boxed);
  }

  void close_groups() {
    for (std::size_t i = 0; i < open_.back().size(); ++i) {
      out_ += "</g>";
    }
    open_.back().clear();
  }

  void open_group(const std::string& attributes, bool mask = false) {
    out_ += "<g" + attributes + ">";
    open_.back().push_back(mask);
  }

  // A soft mask in force at this level ends where the next one starts or ClearSoftMask: its group can be
  // closed when nothing was opened inside it since.
  void close_mask_group() {
    if (!open_.back().empty() && open_.back().back()) {
      out_ += "</g>";
      open_.back().pop_back();
    }
  }

  void command(const Save& /*unused*/) { open_.emplace_back(); }

  void command(const Restore& /*unused*/) {
    // Never past the level a <mask> started at: its content is balanced.
    if (open_.size() > 1 && (masks_.empty() || open_.size() > masks_.back().level + 1)) {
      close_groups();
      open_.pop_back();
    }
  }

  void command(const ClipPath& c) {
    const std::string id = std::format("{}c{}", prefix_, next_id_++);
    out_ += "<clipPath id=\"" + id + "\"><path d=\"" + path_data(c.path) + "\" transform=\"" + matrix(c.ctm) + "\"" +
            (c.rule == FillRule::kEvenOdd ? " clip-rule=\"evenodd\"" : "") + "/></clipPath>";
    open_group(" clip-path=\"url(#" + id + ")\"");
  }

  // A group: its opacity and blend mode apply to it as a whole, as in §11.4.7.
  void command(const BeginGroup& c) {
    std::string style = c.blend != BlendMode::kNormal ? blend_css(c.blend) : "";
    if (c.isolated) {
      style += (style.empty() ? "" : ";") + std::string("isolation:isolate");
    }
    open_group(opacity("opacity", c.alpha) + (style.empty() ? "" : " style=\"" + style + "\""));
  }

  void command(const EndGroup& /*unused*/) {}  // closed with the state that holds it

  // The mask's group becomes an SVG <mask> (luminance over the backdrop colour, or alpha); what follows
  // at this level is drawn through it.
  void command(const BeginSoftMask& c) {
    const std::string id = std::format("{}m{}", prefix_, next_id_++);
    const Rect r = page_box_;
    out_ += std::format(R"(<mask id="{}" maskUnits="userSpaceOnUse" x="{}" y="{}" width="{}" height="{}" style="mask-type:{}">)", id, num(r.x0), num(r.y0),
                        num(r.width()), num(r.height()), c.kind == SoftMaskKind::kLuminosity ? "luminance" : "alpha");
    if (c.kind == SoftMaskKind::kLuminosity) {
      out_ +=
          std::format(R"(<rect x="{}" y="{}" width="{}" height="{}" fill="{}"/>)", num(r.x0), num(r.y0), num(r.width()), num(r.height()), color(c.backdrop));
    }
    masks_.push_back({.id = id, .level = open_.size()});
    open_.emplace_back();
  }

  void command(const EndSoftMask& /*unused*/) {
    if (masks_.empty()) {
      return;
    }
    const std::string id = end_mask();
    close_mask_group();
    open_group(" mask=\"url(#" + id + ")\"", true);
  }

  // Closes the innermost <mask> (and anything its content left open); its id.
  std::string end_mask() {
    while (open_.size() > masks_.back().level) {
      close_groups();
      open_.pop_back();
    }
    out_ += "</mask>";
    std::string id = masks_.back().id;
    masks_.pop_back();
    return id;
  }

  void command(const ClearSoftMask& /*unused*/) { close_mask_group(); }

  void command(const FillPath& c) {
    out_ += "<path d=\"" + path_data(c.path) + "\" transform=\"" + matrix(c.ctm) + "\" fill=\"" + color(rgb(c.color)) + "\"" +
            (c.rule == FillRule::kEvenOdd ? " fill-rule=\"evenodd\"" : "") + opacity("fill-opacity", c.alpha) + blend_style(c.blend) + "/>";
  }

  void command(const StrokePath& c) {
    const StrokeStyle stroke_style = style(c.style);
    std::string attributes = R"( fill="none" stroke=")" + color(rgb(c.color)) + "\"";
    if (stroke_style.width > 0) {
      attributes += " stroke-width=\"" + num(stroke_style.width) + "\"";
    } else {
      // Width 0: the thinnest line the device can draw (§8.4.3.2).
      attributes += R"( stroke-width="0.75" vector-effect="non-scaling-stroke")";
    }
    constexpr std::array<std::string_view, 3> kCaps = {"butt", "round", "square"};
    constexpr std::array<std::string_view, 3> kJoins = {"miter", "round", "bevel"};
    attributes += std::format(R"( stroke-linecap="{}" stroke-linejoin="{}" stroke-miterlimit="{}")", kCaps.at(static_cast<std::size_t>(stroke_style.cap)),
                              kJoins.at(static_cast<std::size_t>(stroke_style.join)), num(std::max(stroke_style.miter_limit, 1.0)));
    if (!stroke_style.dash.empty()) {
      bool all_zero = true;
      std::string dash;
      for (const double d : stroke_style.dash) {
        dash += (dash.empty() ? "" : " ") + num(d);
        all_zero = all_zero && d == 0;
      }
      if (!all_zero) {
        attributes += " stroke-dasharray=\"" + dash + "\" stroke-dashoffset=\"" + num(stroke_style.dash_phase) + "\"";
      }
    }
    out_ += "<path d=\"" + path_data(c.path) + "\" transform=\"" + matrix(c.ctm) + "\"" + attributes + opacity("stroke-opacity", c.alpha) +
            blend_style(c.blend) + "/>";
  }

  void command(const DrawImage& c) {
    if (c.pixels && c.pixels->width > 0 && c.pixels->height > 0) {
      const std::int64_t count = static_cast<std::int64_t>(c.pixels->width) * c.pixels->height;
      if (embedded_ + count <= options_.max_embedded_pixels) {
        auto png = render::encode_png(render::Bitmap{.width = c.pixels->width, .height = c.pixels->height, .rgba = c.pixels->rgba});
        if (png) {
          embedded_ += count;
          // The image's first row at the top of the unit square (§8.9.4): y flipped into it.
          std::string style = c.blend != BlendMode::kNormal ? blend_css(c.blend) : "";
          if (!c.interpolate) {
            style += (style.empty() ? "" : ";") + std::string("image-rendering:pixelated");
          }
          out_ += R"(<image width="1" height="1" preserveAspectRatio="none" transform=")" + matrix(Matrix{1, 0, 0, -1, 0, 1}.then(c.ctm)) + "\"" +
                  opacity("opacity", c.alpha) + (style.empty() ? "" : " style=\"" + style + "\"") + R"( href="data:image/png;base64,)" + base64(*png) + "\"/>";
          return;
        }
      }
    }
    // The unit square of the image's CTM, framed: the samples are decoded in a later phase.
    const std::string fill = c.mask ? color(rgb(c.color)) : "#dfe5f3";
    out_ += R"(<rect width="1" height="1" transform=")" + matrix(c.ctm) + R"(" fill=")" + fill + "\"" + (c.mask ? R"( fill-opacity="0.35")" : "") +
            R"( stroke="#7d88b3" stroke-width="0.75" vector-effect="non-scaling-stroke" stroke-dasharray="3 2"/>)";
  }

  // A shading: its pixels over the page (or its /BBox), 1.5 per point, embedded as PNG; the clip that
  // holds it is the group around. A pattern fill paints its background first.
  void command(const PaintShading& c) {
    if (!c.shading) {
      return;
    }
    const Rect page = page_box_.normalized();
    const std::string paint = opacity("opacity", c.alpha) + blend_style(c.blend);
    if (c.background && c.shading->background) {
      out_ += std::format(R"(<rect x="{}" y="{}" width="{}" height="{}" fill="{}"{}/>)", num(page.x0), num(page.y0), num(page.width()), num(page.height()),
                          color(*c.shading->background), paint);
    }
    constexpr double kPerPoint = 1.5;
    const int w = static_cast<int>(std::ceil(page.width() * kPerPoint));
    const int h = static_cast<int>(std::ceil(page.height() * kPerPoint));
    const std::int64_t count = static_cast<std::int64_t>(w) * h;
    if (w <= 0 || h <= 0 || embedded_ + count > options_.max_embedded_pixels) {
      return;
    }
    const Matrix page_to_pixels{kPerPoint, 0, 0, -kPerPoint, -page.x0 * kPerPoint, page.y1 * kPerPoint};
    const ImagePixels pixels = shade(*c.shading, c.ctm.then(page_to_pixels), w, h);
    auto png = render::encode_png(render::Bitmap{.width = pixels.width, .height = pixels.height, .rgba = pixels.rgba});
    if (!png) {
      return;
    }
    embedded_ += count;
    out_ += std::format(R"(<image width="{}" height="{}" preserveAspectRatio="none" transform="{}"{} href="data:image/png;base64,{}"/>)", w, h,
                        matrix(Matrix{1 / kPerPoint, 0, 0, -1 / kPerPoint, page.x0, page.y1}), paint, base64(*png));
  }

  // A tiling pattern: an SVG <pattern> holding its cell's content, over the page.
  void command(const PaintTiling& c) {
    if (!c.cell || depth_ >= kMaxPatternDepth) {
      return;
    }
    const std::string id = std::format("{}p{}", prefix_, next_id_++);
    Writer cell(options_, id + "_");
    cell.depth_ = depth_ + 1;
    cell.body(*c.cell);
    const std::string content = std::move(cell.out_);
    out_ +=
        std::format(R"(<defs><pattern id="{}" patternUnits="userSpaceOnUse" x="{}" y="{}" width="{}" height="{}" patternTransform="{}">{}</pattern></defs>)",
                    id, num(c.bbox.x0), num(c.bbox.y0), num(c.xstep), num(c.ystep), matrix(c.matrix), content);
    const Rect page = page_box_.normalized();
    out_ += std::format(R"svg(<rect x="{}" y="{}" width="{}" height="{}" fill="url(#{})"{}/>)svg", num(page.x0), num(page.y0), num(page.width()),
                        num(page.height()), id, opacity("opacity", c.alpha) + blend_style(c.blend));
  }

  // The id of a glyph outline, defined (<defs>) the first time it is used.
  std::size_t glyph_id(const Path& outline) {
    const auto [at, added] = glyph_ids_.try_emplace(&outline, glyph_ids_.size());
    if (added) {
      out_ += std::format(R"(<defs><path id="{}g{}" d="{}"/></defs>)", prefix_, at->second, path_data(outline));
    }
    return at->second;
  }

  void command(const ShowText& c) {
    if (c.render_mode == 3 || c.render_mode == 7 || c.glyphs.empty() || c.glyph_procedures) {
      return;  // invisible text (e.g. the OCR layer of a scan), or Type 3 glyphs (the commands that follow)
    }
    // The glyphs as paths, filled and/or stroked as the rendering mode says.
    if (std::ranges::any_of(c.glyphs, [](const Glyph& g) { return g.outline != nullptr; })) {
      const int mode = c.render_mode % 4;
      if (mode == 0) {
        // Filled: each distinct glyph defined once, then placed — a page of text stays small.
        const double h = c.horizontal_scale != 0 ? c.horizontal_scale : 1;
        out_ += "<g transform=\"" + matrix(c.ctm) + "\" fill=\"" + color(rgb(c.fill)) + "\"" + opacity("fill-opacity", c.alpha) + blend_style(c.blend) + ">";
        for (const Glyph& glyph : c.glyphs) {
          if (glyph.outline) {
            const Matrix m = Matrix{c.font_size * h, 0, 0, c.font_size, glyph.origin.x, glyph.origin.y}.then(c.tm);
            out_ += std::format(R"(<use href="#{}g{}" transform="{}"/>)", prefix_, glyph_id(*glyph.outline), matrix(m));
          }
        }
        out_ += "</g>";
        return;
      }
      Path path = glyph_run_path(c);
      if (mode == 0 || mode == 2) {
        command(FillPath{.path = path, .ctm = c.ctm, .rule = FillRule::kNonZero, .blend = c.blend, .color = c.fill, .alpha = c.alpha});
      }
      if (mode == 1 || mode == 2) {
        command(StrokePath{.path = std::move(path), .ctm = c.ctm, .style = c.stroke_style, .color = c.stroke, .alpha = c.stroke_alpha, .blend = c.blend});
      }
      return;
    }
    // No outlines (a CMap or CJK font the host has not supplied, glyphs a font lacks): nothing drawn, as
    // in the bitmap; the page's warnings say why.
  }

  SvgOptions options_;
  std::int64_t embedded_ = 0;
  const DisplayList* list_ = nullptr;
  std::string out_;
  struct OpenMask {
    std::string id;
    std::size_t level = 0;  // open_.size() when it started
  };

  std::vector<std::vector<bool>> open_;  // per saved state: the groups opened in it (true: a soft mask's)
  std::vector<OpenMask> masks_;
  Rect page_box_;
  std::size_t next_id_ = 0;
  std::string prefix_;  // of every id: a pattern cell's content lives in the same document as the page
  int depth_ = 0;       // pattern cells inside pattern cells
  static constexpr int kMaxPatternDepth = 8;
  std::map<const Path*, std::size_t> glyph_ids_;  // outlines are shared: one per glyph of a font
};

}  // namespace

std::string to_svg(const DisplayList& list, const SvgOptions& options) { return Writer(options).write(list); }

}  // namespace capriole::graphics
