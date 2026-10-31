// Copyright 2026 The Fury Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_FURY_FLAT_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_FURY_FLAT_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "base/containers/span.h"

// Canvas noise that looks like a different rasteriser, not like noise.
//
// The first noise touched one opaque pixel in eight, anywhere, by +/-1 in one
// colour channel. pixelscan still said "Masking detected" after everything
// else it tests was passing (29.09.2026), and the pattern it had to go on is
// measurable: a single-colour string drawn on a transparent canvas has every
// opaque pixel exactly the fill colour in real Chrome -- 429 of 429 measured --
// and 55 of them were not, once noised. No rasteriser paints the inside of a
// glyph a different colour.
//
// What does differ between machines is coverage at edges and the arithmetic of
// blends. So:
//   - transparent pixels are never touched;
//   - a partly covered pixel (0 < alpha < 255) gets its coverage moved by one
//     step, in premultiplied space, which is exactly what another rasteriser
//     produces there -- the colour stays the fill colour;
//   - an opaque pixel is touched only where it is a transition inside opaque
//     content: different from every neighbour, and every neighbour opaque, as
//     antialiased text over a filled rect or overlapping blended shapes are;
//   - anything flat, every neighbour the same, stays exact.
//
// WebGL passes opaque_blends = false. Its scenes are smooth shaded
// triangles, and an opaque pixel inside a gradient differs from all four
// neighbours by construction: noising those broke the ramp, and pixelscan
// reported "Masking detected" with WebGL noise alone and not with 2D noise
// alone (measured on the Windows build box, 29.09.2026). Coverage at the
// edges is what differs between GPUs there, so that is all WebGL moves.
//
// Pixels are PREMULTIPLIED here, on every path. toDataURL and toBlob encode
// from premultiplied pixels, and getImageData converts its noised premultiplied
// copy with Skia, so all three agree byte for byte. Coordinates are absolute
// canvas coordinates; a neighbour outside the canvas is not considered, so a
// partial read decides every pixel as a whole-canvas read does.
namespace fury_flat {

inline uint64_t Mix(uint64_t z) {
  z += 0x9e3779b97f4a7c15ULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

// `original` and `noised` are the same premultiplied 4-byte pixels covering
// the canvas rectangle [ox, ox+ow) x [oy, oy+oh), `row_bytes` apart. Pixels
// inside [nx, nx+nw) x [ny, ny+nh) are noised in `noised`; the rest of the
// rectangle is the margin a partial read supplies for its neighbours. The
// canvas is [0, cw) x [0, ch). `bgra` says where red and blue sit.
inline void ApplyEdgeNoise(uint64_t seed,
                           base::span<const uint8_t> original,
                           base::span<uint8_t> noised,
                           size_t row_bytes,
                           int ox,
                           int oy,
                           int ow,
                           int oh,
                           int nx,
                           int ny,
                           int nw,
                           int nh,
                           int cw,
                           int ch,
                           bool bgra,
                           bool opaque_blends = true) {
  auto usable = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < cw && y < ch && x >= ox && y >= oy &&
           x < ox + ow && y < oy + oh;
  };
  auto at = [&](int x, int y) -> size_t {
    return static_cast<size_t>(y - oy) * row_bytes +
           static_cast<size_t>(x - ox) * 4;
  };
  auto equal = [&](size_t a, size_t b) {
    return original[a] == original[b] && original[a + 1] == original[b + 1] &&
           original[a + 2] == original[b + 2] &&
           original[a + 3] == original[b + 3];
  };
  static constexpr std::array<std::pair<int, int>, 4> kNeighbours = {
      {{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
  for (int y = ny; y < ny + nh; ++y) {
    for (int x = nx; x < nx + nw; ++x) {
      if (!usable(x, y)) {
        continue;
      }
      const size_t p = at(x, y);
      if (p + 3 >= original.size() || p + 3 >= noised.size()) {
        continue;
      }
      const uint8_t alpha = original[p + 3];
      if (alpha == 0) {
        continue;
      }
      bool all_equal = true;
      bool none_equal = true;
      bool all_opaque = true;
      for (const auto& d : kNeighbours) {
        const int qx = x + d.first;
        const int qy = y + d.second;
        if (!usable(qx, qy)) {
          continue;
        }
        const size_t q = at(qx, qy);
        if (q + 3 >= original.size()) {
          continue;
        }
        const bool same = equal(p, q);
        all_equal = all_equal && same;
        none_equal = none_equal && !same;
        all_opaque = all_opaque && original[q + 3] == 255;
      }
      if (all_equal) {
        continue;
      }
      const uint64_t h = Mix(seed ^ (static_cast<uint64_t>(x) * 0x100000001b3ULL) ^
                             (static_cast<uint64_t>(y) << 32));
      if (h & 1) {
        continue;
      }
      if (alpha < 255) {
        // Coverage, one step, and the SAME colour at the new coverage: the
        // premultiplied channels scale with alpha, as they would out of a
        // rasteriser that covered this pixel a little more or less.
        //
        // The first version only clamped RGB to the new alpha, which keeps
        // the premultiplied numbers and so changes the colour: getImageData
        // unpremultiplies, and a pixel at alpha 70 moved 2-4 levels off the
        // fill colour, one at alpha 1 -> 2 moved 127. verify-0030 caught it on
        // the first macOS 155 build (30.09.2026); no Windows run had executed
        // that script.
        const int next = (h & 2) ? alpha + 1 : alpha - 1;
        const int a2 = next < 1 ? 1 : next > 254 ? 254 : next;
        noised[p + 3] = static_cast<uint8_t>(a2);
        for (size_t c = 0; c < 3; ++c) {
          const int scaled = (original[p + c] * a2 + alpha / 2) / alpha;
          noised[p + c] = static_cast<uint8_t>(scaled > a2 ? a2 : scaled);
        }
      } else if (opaque_blends && none_equal && all_opaque) {
        const size_t colour = (h >> 3) % 3;
        const size_t channel = (bgra && colour != 1) ? 2 - colour : colour;
        uint8_t& v = noised[p + channel];
        v = ((h >> 5) & 1) ? (v < 255 ? v + 1 : v - 1) : (v > 0 ? v - 1 : v + 1);
      }
    }
  }
}

}  // namespace fury_flat

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_FURY_FLAT_H_
