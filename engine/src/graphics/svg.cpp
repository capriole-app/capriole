#include "capriole/graphics/svg.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

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

// A character code as text: printable Latin-1 as itself (as a character reference beyond ASCII),
// anything else as a placeholder box. Mapping codes to Unicode properly needs the font's encoding and
// ToUnicode (the text phase).
std::string glyph_text(std::uint32_t code) {
  if (code >= 0x20 && code < 0x7F && code != '<' && code != '>' && code != '&' && code != '"' && code != '\'') {
    return {static_cast<char>(code)};
  }
  if (code >= 0xA0 && code <= 0xFF) {
    return std::format("&#x{:X};", code);
  }
  return "&#x25AF;";
}

class Writer {
 public:
  explicit Writer(const SvgOptions& options) : options_(options) {}

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
    out_ += "</g></svg>";
    return std::move(out_);
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
    const std::string id = std::format("c{}", next_id_++);
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
    const std::string id = std::format("m{}", next_id_++);
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

  void command(const PaintShading& /*unused*/) {}

  void command(const ShowText& c) {
    if (c.render_mode == 3 || c.render_mode == 7 || c.glyphs.empty()) {
      return;  // invisible text (e.g. the OCR layer of a scan)
    }
    const double scale = c.horizontal_scale != 0 ? c.horizontal_scale : 1;
    const double size = c.font_size;
    if (size == 0) {
      return;
    }
    // Text element space: x in glyph units along the baseline, y down; see ShowText for the matrix.
    const Matrix element = Matrix{size * scale, 0, 0, -size, 0, c.glyphs.front().origin.y}.then(c.text_matrix);
    if (!c.latin_codes) {
      // Codes that do not read as text (a custom encoding): one outlined box per glyph, with its real
      // advance — where the text is and how it flows, without inventing characters.
      std::string d;
      for (const Glyph& glyph : c.glyphs) {
        const double x = glyph.origin.x / (size * scale);
        const double w = std::max(glyph.advance / (size * scale) * 0.85, 0.05);
        d += "M" + num(x) + " 0h" + num(w) + "v-0.68h" + num(-w) + "Z";
      }
      out_ += "<path d=\"" + d + "\" transform=\"" + matrix(element) + "\" fill=\"" + color(rgb(c.fill)) + R"(" fill-opacity="0.18" stroke=")" +
              color(rgb(c.fill)) + R"(" stroke-width="0.5" vector-effect="non-scaling-stroke"/>)";
      return;
    }
    std::string xs;
    std::string text;
    for (const Glyph& glyph : c.glyphs) {
      xs += (xs.empty() ? "" : " ") + num(glyph.origin.x / (size * scale));
      text += glyph_text(glyph.code);
    }
    const bool stroke = c.render_mode == 1 || c.render_mode == 2 || c.render_mode == 5 || c.render_mode == 6;
    const bool fill = c.render_mode != 1 && c.render_mode != 5;
    std::string paint = " fill=\"" + (fill ? color(rgb(c.fill)) : std::string("none")) + "\"" + opacity("fill-opacity", c.alpha) + blend_style(c.blend);
    if (stroke) {
      paint += " stroke=\"" + color(rgb(c.stroke)) + R"(" stroke-width="0.75" vector-effect="non-scaling-stroke")";
    }
    out_ += "<text transform=\"" + matrix(element) + "\" x=\"" + xs + R"(" font-size="1" font-family="Helvetica, Arial, sans-serif")" + paint + ">" + text +
            "</text>";
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
};

}  // namespace

std::string to_svg(const DisplayList& list, const SvgOptions& options) { return Writer(options).write(list); }

}  // namespace capriole::graphics
