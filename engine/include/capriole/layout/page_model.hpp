#pragma once

// The page model (CAPRIOLE.md §5.2, PLAN.md phase 10a): what a page shows, as a reader sees it — its
// paragraphs (lines, words, styles, margins, alignment), columns, header and footer, images, drawings
// (rules, boxes, tables, backgrounds), shadings and annotations —, built over the low model, not instead of
// it. Every element keeps where it comes from: the commands of the page's display list that draw it, the
// operators of the content streams (the page's, its forms') behind them, the annotation's object.
//
// Edits go through it (edit/paragraph_edit, edit/content_move) and write back only the operators of what
// they change (§5.3). Coordinates are the page's as shown (text/text.hpp): points, y down.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/font/resources.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/graphics/geometry.hpp"
#include "capriole/model/object.hpp"
#include "capriole/text/text.hpp"

namespace capriole::layout {

enum class Alignment : std::uint8_t { kLeft, kJustified, kCentre, kRight };

// A style of a paragraph's text: font, size, colour and horizontal scaling.
struct Style {
  std::optional<model::ObjectRef> font;
  double size = 0;
  graphics::Rgb colour{0, 0, 0};
  double th = 1;
};

// A word of a paragraph (a word broken at a line's end, "infor-" "mazione", is one again), and the style
// of each of its letters.
struct Word {
  std::u32string text;
  std::vector<std::size_t> styles;
};

// Lines of the page's text read as a paragraph.
struct Paragraph {
  std::vector<Style> styles;
  std::size_t main_style = 0;  // the one most letters have
  std::vector<Word> words;
  std::vector<std::size_t> lines;  // in the page's text
  std::vector<std::size_t> chars;
  std::vector<double> baselines;  // of each line
  double left = 0;                // of the lines after the first
  double indent = 0;              // of the first line, from left (negative: hanging)
  double right = 0;
  double top = 0;     // of the first line's glyph boxes
  double bottom = 0;  // of the last line's
  double ascent = 0;  // above a baseline, below it (the glyph boxes')
  double descent = 0;
  double pitch = 0;  // from baseline to baseline
  double size = 0;
  double gap = 0;  // between words, as set (not justified)
  Alignment alignment = Alignment::kLeft;
  // Why it cannot be laid out anew (edit/paragraph_edit): its text is invisible, an annotation's, not
  // horizontal, has superscripts or subscripts, its lines do not run downwards. Empty: it can.
  std::string refusal;
};

// Lines of the page's text (indices in text.lines, consecutive) as a paragraph. The page's display list is
// the one text was extracted from (content::Purpose::kText).
[[nodiscard]] Paragraph paragraph_of(const text::PageText& text, const graphics::DisplayList& list, std::span<const std::size_t> lines);

// Where an element is drawn from: an operator (its ordinal) of a content stream (graphics::Invocation).
struct Source {
  std::uint32_t invocation = 0;
  std::uint32_t operation = 0;
  friend bool operator==(const Source&, const Source&) = default;
};

enum class Kind : std::uint8_t {
  kParagraph,
  kGlyphs,  // text with nothing to read in it (spaces, glyphs whose font does not say what they are)
  kImage,
  kDrawing,
  kShading,  // shadings or patterns painted over clips, alone (with paths they touch: a drawing)
  kAnnotation,
};

// What a drawing is, read from its shape.
enum class Role : std::uint8_t {
  kNone,        // text, an annotation
  kBackground,  // one simple shape, image or shading as large as the page (90% of each side)
  kRule,        // one thin line (3 points at most across)
  kBox,         // a rectangle (filled or outlined), with what is drawn touching it
  kTable,       // a grid: two thin horizontal lines and two vertical ones at least
  kFigure,      // anything else (an image is one)
};

enum class Zone : std::uint8_t { kBody, kHeader, kFooter };

struct Element {
  Kind kind = Kind::kDrawing;
  Role role = Role::kNone;
  Zone zone = Zone::kBody;
  graphics::Rect box;  // what it covers as shown, strokes' width and glyphs' outlines included
  // The display list's commands that draw it (a paragraph: its runs, which may show text of others too).
  std::vector<std::size_t> commands;
  std::vector<Source> sources;                 // their operators, each once, in order
  std::size_t paragraph = 0;                   // kParagraph: in PageModel::paragraphs
  std::optional<std::size_t> column;           // in PageModel::columns
  std::optional<model::ObjectRef> annotation;  // kAnnotation: its object (none for a direct dictionary)
  std::string subtype;                         // kAnnotation: /Subtype
};

// Paragraphs one above the other, overlapping across (half the narrower's width), with nothing beside
// them that breaks the run: a column of text.
struct Column {
  graphics::Rect box;
  std::vector<std::size_t> elements;  // its paragraphs, top to bottom
};

struct PageModel {
  double width = 0;  // of the page as shown
  double height = 0;
  graphics::DisplayList list;  // content::Purpose::kText
  text::PageText text;         // as laid out (text::Order::kLayout)
  std::vector<Paragraph> paragraphs;
  std::vector<Element> elements;  // top to bottom, then left to right
  std::vector<Column> columns;
  // The display list's commands that the page's content streams draw (not annotations, not glyph
  // procedures): each is in one element exactly — a text run in the paragraphs whose characters it shows.
  std::vector<std::size_t> drawn;
};

// Bounds against hostile pages: those of reading the content and its text (the rest of the work is bounded
// by them, and grouping paths compares a bounded number of pairs).
struct ModelLimits {
  content::InterpreterLimits interpreter;
  text::TextLimits text;
};

// The model of a page (page_index from 0); resources: the fonts and CMaps the host can supply
// (font/resources.hpp). Errors only when the page cannot be read.
[[nodiscard]] Result<PageModel> page_model(Document& document, std::uint32_t page_index, const font::Resources* resources = nullptr,
                                           const ModelLimits& limits = {});

}  // namespace capriole::layout
