// Phase 6c benchmark: renders pages with each backend, times them, writes PNGs, and compares.
//
//   raster_bench <file.pdf> [--pages 1-3] [--dpi 150] [--repeat 3] [--out dir] [--reference-ppm-prefix p]
//
// For each page: interpret once, then render with every backend `repeat` times (the fastest run is
// kept). Prints CSV: backend,page,commands,width,height,ms, and the mean absolute difference between
// the backends (and against a pdftoppm reference, `p-<page>.ppm`, when given).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <zlib.h>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "raster_backend.hpp"

using namespace capriole;
using experiments::Bitmap;

namespace {

void put32(std::string& out, std::uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(char((v >> shift) & 0xFF));
  }
}

void chunk(std::string& png, const char* type, const std::string& data) {
  put32(png, std::uint32_t(data.size()));
  std::string body = std::string(type, 4) + data;
  png += body;
  put32(png, std::uint32_t(crc32(0, reinterpret_cast<const Bytef*>(body.data()), uInt(body.size()))));
}

void write_png(const Bitmap& b, const std::string& path) {
  std::string raw;
  raw.reserve(std::size_t(b.height) * (std::size_t(b.width) * 4 + 1));
  for (int y = 0; y < b.height; ++y) {
    raw.push_back(0);
    raw.append(reinterpret_cast<const char*>(&b.rgba[std::size_t(y) * std::size_t(b.width) * 4]), std::size_t(b.width) * 4);
  }
  uLongf size = compressBound(uLong(raw.size()));
  std::string compressed(size, '\0');
  compress2(reinterpret_cast<Bytef*>(compressed.data()), &size, reinterpret_cast<const Bytef*>(raw.data()), uLong(raw.size()), 6);
  compressed.resize(size);
  std::string png = "\x89PNG\r\n\x1a\n";
  std::string header;
  put32(header, std::uint32_t(b.width));
  put32(header, std::uint32_t(b.height));
  header += std::string("\x08\x06\x00\x00\x00", 5);
  chunk(png, "IHDR", header);
  chunk(png, "IDAT", compressed);
  chunk(png, "IEND", "");
  std::ofstream(path, std::ios::binary) << png;
}

// A binary PPM (P6), as pdftoppm writes it.
std::optional<Bitmap> read_ppm(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::string magic;
  int w = 0;
  int h = 0;
  int max = 0;
  if (!(in >> magic >> w >> h >> max) || magic != "P6" || max != 255) {
    return std::nullopt;
  }
  in.get();
  Bitmap b{w, h, std::vector<std::uint8_t>(std::size_t(w) * std::size_t(h) * 4, 255)};
  for (std::size_t i = 0; i < std::size_t(w) * std::size_t(h); ++i) {
    char rgb[3];
    in.read(rgb, 3);
    b.rgba[i * 4] = std::uint8_t(rgb[0]);
    b.rgba[i * 4 + 1] = std::uint8_t(rgb[1]);
    b.rgba[i * 4 + 2] = std::uint8_t(rgb[2]);
  }
  return b;
}

// Mean absolute difference per channel (0–255) and the share of pixels differing by more than 32,
// over the common area.
std::pair<double, double> compare(const Bitmap& a, const Bitmap& b) {
  const int w = std::min(a.width, b.width);
  const int h = std::min(a.height, b.height);
  double sum = 0;
  std::size_t different = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int worst = 0;
      for (int c = 0; c < 3; ++c) {
        const int d = std::abs(int(a.rgba[(std::size_t(y) * std::size_t(a.width) + std::size_t(x)) * 4 + std::size_t(c)]) -
                               int(b.rgba[(std::size_t(y) * std::size_t(b.width) + std::size_t(x)) * 4 + std::size_t(c)]));
        sum += d;
        worst = std::max(worst, d);
      }
      different += worst > 32 ? 1 : 0;
    }
  }
  const double pixels = double(w) * double(h);
  return {pixels > 0 ? sum / (pixels * 3) : 0, pixels > 0 ? 100.0 * double(different) / pixels : 0};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: raster_bench <file.pdf> [--pages a-b] [--dpi n] [--repeat n] [--out dir] [--reference-ppm-prefix p]\n";
    return 2;
  }
  std::string file = argv[1];
  int first = 1;
  int last = 1;
  double dpi = 150;
  int repeat = 3;
  std::string out_dir;
  std::string reference;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string key = argv[i];
    const std::string value = argv[i + 1];
    if (key == "--pages") {
      const auto dash = value.find('-');
      first = std::stoi(value.substr(0, dash));
      last = dash == std::string::npos ? first : std::stoi(value.substr(dash + 1));
    } else if (key == "--dpi") {
      dpi = std::stod(value);
    } else if (key == "--repeat") {
      repeat = std::stoi(value);
    } else if (key == "--out") {
      out_dir = value;
    } else if (key == "--reference-ppm-prefix") {
      reference = value;
    }
  }
  auto source = FileByteSource::open(file);
  if (!source) {
    std::cerr << source.error().message << "\n";
    return 1;
  }
  auto document = Document::open(std::move(*source));
  if (!document) {
    std::cerr << document.error().message << "\n";
    return 1;
  }
  auto pages = document->page_refs();
  if (!pages) {
    std::cerr << pages.error().message << "\n";
    return 1;
  }
  std::vector<std::unique_ptr<experiments::RasterBackend>> backends;
#ifndef RASTER_WITHOUT_SKIA
  backends.push_back(experiments::make_skia_backend());
#endif
#ifndef RASTER_WITHOUT_BLEND2D
  backends.push_back(experiments::make_blend2d_backend());
#endif
  const double scale = dpi / 72;
  for (int page = first; page <= last && page <= int(pages->size()); ++page) {
    auto list = content::interpret_page(*document, (*pages)[std::size_t(page - 1)]);
    if (!list) {
      continue;
    }
    std::vector<Bitmap> results;
    for (auto& backend : backends) {
      double best = 1e18;
      Bitmap bitmap;
      for (int r = 0; r < repeat; ++r) {
        const auto start = std::chrono::steady_clock::now();
        bitmap = backend->render(*list, scale);
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
      }
      std::cout << backend->name() << "," << page << "," << list->commands.size() << "," << bitmap.width << "," << bitmap.height << "," << best << "\n";
      if (!out_dir.empty()) {
        write_png(bitmap, out_dir + "/p" + std::to_string(page) + "-" + backend->name() + ".png");
      }
      results.push_back(std::move(bitmap));
    }
    if (results.size() == 2) {
      const auto [mean, share] = compare(results[0], results[1]);
      std::cout << "diff,skia-vs-blend2d," << page << ",mean " << mean << ",pixels>32 " << share << "%\n";
    }
    if (!reference.empty()) {
      if (auto ref = read_ppm(reference + "-" + std::to_string(page) + ".ppm")) {
        for (std::size_t i = 0; i < backends.size(); ++i) {
          const auto [m, sh] = compare(results[i], *ref);
          std::cout << "diff," << backends[i]->name() << "-vs-reference," << page << ",mean " << m << ",pixels>32 " << sh << "%\n";
        }
      }
    }
  }
  return 0;
}
