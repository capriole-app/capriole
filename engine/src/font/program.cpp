#include "program.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <utility>

// clang-format off
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_FONT_FORMATS_H
#include FT_MODULE_H
#include FT_OUTLINE_H
// clang-format on

namespace capriole::font::detail {

namespace {

// FreeType's allocator with a budget. Its free function is not told the size of the block, so every
// block starts with a header that records it (as large as the strictest alignment).
struct Budget {
  std::size_t used = 0;
  std::size_t limit = 0;
};

constexpr std::size_t kHeader = alignof(std::max_align_t);

void* budget_alloc(FT_Memory memory, long size) {  // NOLINT(google-runtime-int): FreeType's signature
  auto* budget = static_cast<Budget*>(memory->user);
  const auto bytes = static_cast<std::size_t>(size);
  if (size <= 0 || bytes > budget->limit - budget->used) {
    return nullptr;
  }
  // FreeType's allocator: C memory, freed by budget_free when FreeType says so.
  auto* block = static_cast<unsigned char*>(std::malloc(bytes + kHeader));  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
  if (block == nullptr) {
    return nullptr;
  }
  std::memcpy(block, &bytes, sizeof(bytes));
  budget->used += bytes;
  return block + kHeader;
}

void budget_free(FT_Memory memory, void* pointer) {
  if (pointer == nullptr) {
    return;
  }
  auto* block = static_cast<unsigned char*>(pointer) - kHeader;
  std::size_t bytes = 0;
  std::memcpy(&bytes, block, sizeof(bytes));
  static_cast<Budget*>(memory->user)->used -= bytes;
  std::free(block);  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
}

void* budget_realloc(FT_Memory memory, long /*current*/, long size, void* pointer) {  // NOLINT(google-runtime-int)
  if (pointer == nullptr) {
    return budget_alloc(memory, size);
  }
  auto* block = static_cast<unsigned char*>(pointer) - kHeader;
  std::size_t old_bytes = 0;
  std::memcpy(&old_bytes, block, sizeof(old_bytes));
  void* grown = budget_alloc(memory, size);
  if (grown == nullptr) {
    return nullptr;  // FreeType keeps the old block
  }
  std::memcpy(grown, pointer, std::min(old_bytes, static_cast<std::size_t>(size)));
  budget_free(memory, pointer);
  return grown;
}

FT_Encoding encoding_of(Charmap charmap) {
  switch (charmap) {
    case Charmap::kUnicode:
      return FT_ENCODING_UNICODE;
    case Charmap::kMicrosoftSymbol:
      return FT_ENCODING_MS_SYMBOL;
    case Charmap::kMacRoman:
      return FT_ENCODING_APPLE_ROMAN;
    case Charmap::kBuiltin:
      break;
  }
  return FT_ENCODING_ADOBE_CUSTOM;
}

// FreeType's outline, in 26.6 pixels of a 1000-pixel em: glyph space with 1/64 of a unit.
struct Decomposer {
  graphics::Path path;
  graphics::Point last;
  bool open = false;
  static graphics::Point point(const FT_Vector* v) { return {static_cast<double>(v->x) / 64, static_cast<double>(v->y) / 64}; }
};

int move_to(const FT_Vector* to, void* user) {
  auto* d = static_cast<Decomposer*>(user);
  if (d->open) {
    d->path.close();
  }
  d->last = Decomposer::point(to);
  d->path.move_to(d->last);
  d->open = true;
  return 0;
}

int line_to(const FT_Vector* to, void* user) {
  auto* d = static_cast<Decomposer*>(user);
  d->last = Decomposer::point(to);
  d->path.line_to(d->last);
  return 0;
}

// A quadratic Bézier (TrueType) as the cubic that draws the same curve.
int conic_to(const FT_Vector* control, const FT_Vector* to, void* user) {
  auto* d = static_cast<Decomposer*>(user);
  const graphics::Point c = Decomposer::point(control);
  const graphics::Point end = Decomposer::point(to);
  const graphics::Point c1{d->last.x + ((c.x - d->last.x) * 2 / 3), d->last.y + ((c.y - d->last.y) * 2 / 3)};
  const graphics::Point c2{end.x + ((c.x - end.x) * 2 / 3), end.y + ((c.y - end.y) * 2 / 3)};
  d->path.cubic_to(c1, c2, end);
  d->last = end;
  return 0;
}

int cubic_to(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user) {
  auto* d = static_cast<Decomposer*>(user);
  d->last = Decomposer::point(to);
  d->path.cubic_to(Decomposer::point(control1), Decomposer::point(control2), d->last);
  return 0;
}

constexpr FT_Int32 kLoadFlags = FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT;

}  // namespace

struct FontProgram::Impl {
  std::shared_ptr<const std::vector<std::byte>> data;  // FreeType reads it in place: kept alive
  Budget budget;
  FT_MemoryRec_ memory{};
  FT_Library library = nullptr;
  FT_Face face = nullptr;

  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  ~Impl() {
    if (face != nullptr) {
      FT_Done_Face(face);
    }
    if (library != nullptr) {
      FT_Done_Library(library);
    }
  }

  [[nodiscard]] FT_CharMap charmap(Charmap which) const {
    const FT_Encoding wanted = encoding_of(which);
    for (FT_Int i = 0; i < face->num_charmaps; ++i) {
      const FT_Encoding encoding = face->charmaps[i]->encoding;
      const bool builtin = encoding == FT_ENCODING_ADOBE_CUSTOM || encoding == FT_ENCODING_ADOBE_STANDARD || encoding == FT_ENCODING_ADOBE_EXPERT ||
                           encoding == FT_ENCODING_ADOBE_LATIN_1;
      if (encoding == wanted || (which == Charmap::kBuiltin && builtin)) {
        return face->charmaps[i];
      }
    }
    return nullptr;
  }

  [[nodiscard]] bool load(std::uint32_t glyph) const { return FT_Load_Glyph(face, glyph, kLoadFlags) == 0 && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE; }
};

FontProgram::FontProgram(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FontProgram::~FontProgram() = default;

Result<std::unique_ptr<FontProgram>> FontProgram::open(std::shared_ptr<const std::vector<std::byte>> data, const ProgramLimits& limits) {
  if (!data || data->empty()) {
    return make_error(ErrorCode::kCorruptData, "font program: empty");
  }
  auto impl = std::make_unique<Impl>();
  impl->data = std::move(data);
  impl->budget.limit = limits.max_memory;
  impl->memory.user = &impl->budget;
  impl->memory.alloc = budget_alloc;
  impl->memory.free = budget_free;
  impl->memory.realloc = budget_realloc;
  if (FT_New_Library(&impl->memory, &impl->library) != 0) {
    return make_error(ErrorCode::kLimitExceeded, "font program: cannot start FreeType");
  }
  FT_Add_Default_Modules(impl->library);
  const auto* bytes =
      reinterpret_cast<const FT_Byte*>(impl->data->data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): std::byte → FreeType's bytes
  if (FT_New_Memory_Face(impl->library, bytes, static_cast<FT_Long>(impl->data->size()), 0, &impl->face) != 0) {
    return make_error(ErrorCode::kCorruptData, "font program: not a font FreeType can read");
  }
  if ((impl->face->face_flags & FT_FACE_FLAG_SCALABLE) == 0) {
    return make_error(ErrorCode::kCorruptData, "font program: no outlines");
  }
  // A 1000-pixel em: loaded outlines are then in glyph space (1000 units per em), whatever the font's own
  // units and font matrix.
  constexpr FT_F26Dot6 kEm = FT_F26Dot6{1000} * 64;
  if (FT_Set_Char_Size(impl->face, 0, kEm, 72, 72) != 0) {
    return make_error(ErrorCode::kCorruptData, "font program: unusable metrics");
  }
  return std::unique_ptr<FontProgram>(new FontProgram(std::move(impl)));
}

bool FontProgram::is_truetype() const { return std::string_view(FT_Get_Font_Format(impl_->face)) == "TrueType"; }

bool FontProgram::is_cid_keyed() const { return (impl_->face->face_flags & FT_FACE_FLAG_CID_KEYED) != 0; }

std::uint32_t FontProgram::glyph_count() const { return static_cast<std::uint32_t>(std::max<FT_Long>(impl_->face->num_glyphs, 0)); }

bool FontProgram::has_charmap(Charmap charmap) const { return impl_->charmap(charmap) != nullptr; }

std::uint32_t FontProgram::glyph_for(Charmap charmap, std::uint32_t code) const {
  FT_CharMap map = impl_->charmap(charmap);
  if (map == nullptr || FT_Set_Charmap(impl_->face, map) != 0) {
    return 0;
  }
  return FT_Get_Char_Index(impl_->face, code);
}

std::uint32_t FontProgram::glyph_named(std::string_view name) const {
  if (name.empty() || (impl_->face->face_flags & FT_FACE_FLAG_GLYPH_NAMES) == 0) {
    return 0;
  }
  const std::string terminated(name);
  return FT_Get_Name_Index(impl_->face, terminated.c_str());
}

std::optional<graphics::Path> FontProgram::outline(std::uint32_t glyph) const {
  if (!impl_->load(glyph)) {
    return std::nullopt;
  }
  static constexpr FT_Outline_Funcs kFuncs = {.move_to = move_to, .line_to = line_to, .conic_to = conic_to, .cubic_to = cubic_to, .shift = 0, .delta = 0};
  Decomposer d;
  if (FT_Outline_Decompose(&impl_->face->glyph->outline, &kFuncs, &d) != 0) {
    return std::nullopt;
  }
  if (d.open) {
    d.path.close();
  }
  return std::move(d.path);
}

std::optional<double> FontProgram::advance(std::uint32_t glyph) const {
  if (!impl_->load(glyph)) {
    return std::nullopt;
  }
  return static_cast<double>(impl_->face->glyph->linearHoriAdvance) / 65536;  // 16.16 pixels of the 1000-pixel em
}

std::optional<std::string> FontProgram::glyph_name(std::uint32_t glyph) const {
  if ((impl_->face->face_flags & FT_FACE_FLAG_GLYPH_NAMES) == 0) {
    return std::nullopt;
  }
  std::array<char, 128> name{};
  if (FT_Get_Glyph_Name(impl_->face, glyph, name.data(), static_cast<FT_UInt>(name.size())) != 0 || name[0] == '\0') {
    return std::nullopt;
  }
  return std::string(name.data());
}

std::unordered_map<std::uint32_t, char32_t> FontProgram::unicode_of_glyphs() const {
  std::unordered_map<std::uint32_t, char32_t> map;
  FT_CharMap unicode = impl_->charmap(Charmap::kUnicode);
  if (unicode == nullptr || FT_Set_Charmap(impl_->face, unicode) != 0) {
    return map;
  }
  constexpr std::size_t kMaxEntries = 1'000'000;
  FT_UInt glyph = 0;
  for (FT_ULong c = FT_Get_First_Char(impl_->face, &glyph); glyph != 0 && map.size() < kMaxEntries; c = FT_Get_Next_Char(impl_->face, c, &glyph)) {
    map.emplace(glyph, static_cast<char32_t>(c));
  }
  return map;
}

std::pair<double, double> FontProgram::ascent_descent() const {
  const FT_FaceRec* face = impl_->face;
  if (face->units_per_EM == 0) {
    return {0, 0};
  }
  const double scale = 1000.0 / face->units_per_EM;
  return {face->ascender * scale, face->descender * scale};
}

}  // namespace capriole::font::detail
