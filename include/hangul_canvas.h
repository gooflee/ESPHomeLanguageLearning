#pragma once
// Built-in pure-vector Hangul & symbol canvas renderer for ESPHome e-ink displays.
// Decomposes Korean Hangul syllables (U+AC00..U+D7A3), standalone Jamo (U+3131..U+3163),
// numbers, and symbols into crisp strokes directly on esphome::display::Display
// so zero external CJK TTF font files are required and all 11,172 Hangul syllables work offline.

#include "esphome/components/display/display.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace sticky_hangul {

struct Pt {
  float x;
  float y;
};

class CanvasTransform {
 public:
  esphome::display::Display *disp;
  int origin_x;
  int origin_y;
  float scale;
  int radius;
  esphome::Color color;

  CanvasTransform(esphome::display::Display *d, int ox, int oy, float cell_size, float stroke_width, esphome::Color c)
      : disp(d), origin_x(ox), origin_y(oy), color(c) {
    scale = cell_size / 100.0f;
    float px_sw = std::max(1.5f, stroke_width * scale);
    radius = std::max(0, static_cast<int>(std::floor((px_sw - 0.5f) * 0.5f)));
  }

  inline int tx(float vx) const { return origin_x + static_cast<int>(std::lround(vx * scale)); }
  inline int ty(float vy) const { return origin_y + static_cast<int>(std::lround(vy * scale)); }

  void draw_brush(int px, int py) const {
    if (radius <= 0) {
      disp->draw_pixel_at(px, py, color);
      return;
    }
    if (radius == 1) {
      disp->filled_rectangle(px - 1, py - 1, 3, 3, color);
      return;
    }
    disp->filled_circle(px, py, radius, color);
  }

  void draw_segmentPx(int x0, int y0, int x1, int y1) const {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
      draw_brush(x0, y0);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  void line(float x1, float y1, float x2, float y2) const {
    draw_segmentPx(tx(x1), ty(y1), tx(x2), ty(y2));
  }

  void poly(const std::vector<Pt> &pts, bool close_path = false) const {
    if (pts.empty()) return;
    for (size_t i = 0; i + 1 < pts.size(); i++) {
      line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y);
    }
    if (close_path && pts.size() > 2) {
      line(pts.back().x, pts.back().y, pts.front().x, pts.front().y);
    }
  }

  void fill_poly(const std::vector<Pt> &pts) const {
    if (pts.size() < 3) return;
    std::vector<Pt> px_pts;
    px_pts.reserve(pts.size());
    int min_y = 100000, max_y = -100000;
    for (const auto &p : pts) {
      int px = tx(p.x);
      int py = ty(p.y);
      px_pts.push_back({static_cast<float>(px), static_cast<float>(py)});
      if (py < min_y) min_y = py;
      if (py > max_y) max_y = py;
    }
    for (int y = min_y; y <= max_y; y++) {
      std::vector<int> nodes;
      size_t j = px_pts.size() - 1;
      for (size_t i = 0; i < px_pts.size(); i++) {
        float yi = px_pts[i].y, yj = px_pts[j].y;
        float xi = px_pts[i].x, xj = px_pts[j].x;
        if ((yi < y && yj >= y) || (yj < y && yi >= y)) {
          nodes.push_back(static_cast<int>( std::lround(xi + (y - yi) / (yj - yi) * (xj - xi)) ));
        }
        j = i;
      }
      std::sort(nodes.begin(), nodes.end());
      for (size_t i = 0; i + 1 < nodes.size(); i += 2) {
        disp->horizontal_line(nodes[i], y, std::max(1, nodes[i + 1] - nodes[i] + 1), color);
      }
    }
  }

  void stroke_ellipse(float cx, float cy, float rx, float ry) const {
    const int steps = 24;
    std::vector<Pt> pts;
    pts.reserve(steps);
    for (int i = 0; i < steps; i++) {
      float ang = (2.0f * 3.14159265f * i) / steps;
      pts.push_back({cx + std::cos(ang) * rx, cy + std::sin(ang) * ry});
    }
    poly(pts, true);
  }

  void fill_ellipse(float cx, float cy, float rx, float ry) const {
    int pcx = tx(cx);
    int pcy = ty(cy);
    int prx = std::max(1, static_cast<int>(std::lround(rx * scale)));
    int pry = std::max(1, static_cast<int>(std::lround(ry * scale)));
    for (int dy = -pry; dy <= pry; dy++) {
      float norm_y = static_cast<float>(dy) / static_cast<float>(pry);
      int dx = static_cast<int>(std::lround(prx * std::sqrt(std::max(0.0f, 1.0f - norm_y * norm_y))));
      disp->horizontal_line(pcx - dx, pcy + dy, 2 * dx + 1, color);
    }
  }
};

// Base consonant indices:
// 0:ㄱ 1:ㄲ 2:ㄴ 3:ㄷ 4:ㄸ 5:ㄹ 6:ㅁ 7:ㅂ 8:ㅃ 9:ㅅ 10:ㅆ 11:ㅇ 12:ㅈ 13:ㅉ 14:ㅊ 15:ㅋ 16:ㅌ 17:ㅍ 18:ㅎ
inline void draw_base_consonant(const CanvasTransform &ct, int id, float x, float y, float w, float h) {
  const float xL = x;
  const float xR = x + w;
  const float xM = x + w * 0.5f;
  const float yT = y;
  const float yB = y + h;
  const float yM = y + h * 0.5f;

  switch (id) {
    case 0: // ㄱ
      ct.poly({{xL, yT}, {xR, yT}, {xR, yB}});
      break;
    case 1: // ㄲ
      draw_base_consonant(ct, 0, x, y, w * 0.44f, h);
      draw_base_consonant(ct, 0, x + w * 0.56f, y, w * 0.44f, h);
      break;
    case 2: // ㄴ
      ct.poly({{xL, yT}, {xL, yB}, {xR, yB}});
      break;
    case 3: // ㄷ
      ct.line(xL, yT, xR, yT);
      ct.poly({{xL, yT}, {xL, yB}, {xR, yB}});
      break;
    case 4: // ㄸ
      draw_base_consonant(ct, 3, x, y, w * 0.44f, h);
      draw_base_consonant(ct, 3, x + w * 0.56f, y, w * 0.44f, h);
      break;
    case 5: // ㄹ
      ct.poly({{xL, yT}, {xR, yT}, {xR, yM}, {xL, yM}, {xL, yB}, {xR, yB}});
      break;
    case 6: // ㅁ
      ct.poly({{xL, yT}, {xR, yT}, {xR, yB}, {xL, yB}}, true);
      break;
    case 7: // ㅂ
      ct.line(xL, yT, xL, yB);
      ct.line(xR, yT, xR, yB);
      ct.line(xL, yM, xR, yM);
      ct.line(xL, yB, xR, yB);
      break;
    case 8: // ㅃ
      draw_base_consonant(ct, 7, x, y, w * 0.44f, h);
      draw_base_consonant(ct, 7, x + w * 0.56f, y, w * 0.44f, h);
      break;
    case 9: // ㅅ
      ct.line(xM, yT, xL, yB);
      ct.line(xM * 0.45f + xL * 0.55f, y + h * 0.42f, xR, yB);
      break;
    case 10: // ㅆ
      draw_base_consonant(ct, 9, x, y, w * 0.46f, h);
      draw_base_consonant(ct, 9, x + w * 0.54f, y, w * 0.46f, h);
      break;
    case 11: // ㅇ
      ct.stroke_ellipse(xM, yM, w * 0.44f, h * 0.44f);
      break;
    case 12: // ㅈ
      ct.line(xL, yT, xR, yT);
      ct.line(xM, yT, xL, yB);
      ct.line(xM * 0.5f + xL * 0.5f, y + h * 0.46f, xR, yB);
      break;
    case 13: // ㅉ
      draw_base_consonant(ct, 12, x, y, w * 0.45f, h);
      draw_base_consonant(ct, 12, x + w * 0.55f, y, w * 0.45f, h);
      break;
    case 14: // ㅊ
      ct.line(x + w * 0.28f, yT, x + w * 0.72f, yT);
      ct.line(xL, y + h * 0.24f, xR, y + h * 0.24f);
      ct.line(xM, y + h * 0.24f, xL, yB);
      ct.line(xM * 0.5f + xL * 0.5f, y + h * 0.56f, xR, yB);
      break;
    case 15: // ㅋ
      ct.poly({{xL, yT}, {xR, yT}, {xR, yB}});
      ct.line(xL, yM, xR, yM);
      break;
    case 16: // ㅌ
      ct.line(xL, yT, xR, yT);
      ct.line(xL, yM, xR, yM);
      ct.poly({{xL, yT}, {xL, yB}, {xR, yB}});
      break;
    case 17: // ㅍ
      ct.line(xL, yT, xR, yT);
      ct.line(x + w * 0.24f, yT, x + w * 0.24f, yB);
      ct.line(x + w * 0.76f, yT, x + w * 0.76f, yB);
      ct.line(xL, yB, xR, yB);
      break;
    case 18: // ㅎ
      ct.line(x + w * 0.3f, yT, x + w * 0.7f, yT);
      ct.line(xL, y + h * 0.22f, xR, y + h * 0.22f);
      ct.stroke_ellipse(xM, y + h * 0.64f, w * 0.38f, h * 0.33f);
      break;
    default:
      break;
  }
}

// Vertical vowel helper in box (x, y, w, h)
// jung indices: 0:ㅏ 1:ㅐ 2:ㅑ 3:ㅒ 4:ㅓ 5:ㅔ 6:ㅕ 7:ㅖ 20:ㅣ
inline void draw_vertical_vowel(const CanvasTransform &ct, int jung, float x, float y, float w, float h) {
  const float yT = y;
  const float yB = y + h;
  const float yM = y + h * 0.5f;
  const float y1 = y + h * 0.36f;
  const float y2 = y + h * 0.64f;

  switch (jung) {
    case 0: // ㅏ
      ct.line(x + w * 0.25f, yT, x + w * 0.25f, yB);
      ct.line(x + w * 0.25f, yM, x + w * 0.9f, yM);
      break;
    case 1: // ㅐ
      ct.line(x + w * 0.15f, yT, x + w * 0.15f, yB);
      ct.line(x + w * 0.15f, yM, x + w * 0.58f, yM);
      ct.line(x + w * 0.82f, yT, x + w * 0.82f, yB);
      break;
    case 2: // ㅑ
      ct.line(x + w * 0.25f, yT, x + w * 0.25f, yB);
      ct.line(x + w * 0.25f, y1, x + w * 0.9f, y1);
      ct.line(x + w * 0.25f, y2, x + w * 0.9f, y2);
      break;
    case 3: // ㅒ
      ct.line(x + w * 0.15f, yT, x + w * 0.15f, yB);
      ct.line(x + w * 0.15f, y1, x + w * 0.58f, y1);
      ct.line(x + w * 0.15f, y2, x + w * 0.58f, y2);
      ct.line(x + w * 0.82f, yT, x + w * 0.82f, yB);
      break;
    case 4: // ㅓ
      ct.line(x + w * 0.75f, yT, x + w * 0.75f, yB);
      ct.line(x + w * 0.1f, yM, x + w * 0.75f, yM);
      break;
    case 5: // ㅔ
      ct.line(x + w * 0.08f, yM, x + w * 0.48f, yM);
      ct.line(x + w * 0.48f, yT, x + w * 0.48f, yB);
      ct.line(x + w * 0.85f, yT, x + w * 0.85f, yB);
      break;
    case 6: // ㅕ
      ct.line(x + w * 0.75f, yT, x + w * 0.75f, yB);
      ct.line(x + w * 0.1f, y1, x + w * 0.75f, y1);
      ct.line(x + w * 0.1f, y2, x + w * 0.75f, y2);
      break;
    case 7: // ㅖ
      ct.line(x + w * 0.08f, y1, x + w * 0.48f, y1);
      ct.line(x + w * 0.08f, y2, x + w * 0.48f, y2);
      ct.line(x + w * 0.48f, yT, x + w * 0.48f, yB);
      ct.line(x + w * 0.85f, yT, x + w * 0.85f, yB);
      break;
    case 20: // ㅣ
      ct.line(x + w * 0.5f, yT, x + w * 0.5f, yB);
      break;
    default:
      break;
  }
}

// Horizontal vowel helper in box (x, y, w, h)
// jung indices: 8:ㅗ 12:ㅛ 13:ㅜ 17:ㅠ 18:ㅡ
inline void draw_horizontal_vowel(const CanvasTransform &ct, int jung, float x, float y, float w, float h) {
  const float xL = x;
  const float xR = x + w;
  const float xM = x + w * 0.5f;
  const float x1 = x + w * 0.35f;
  const float x2 = x + w * 0.65f;
  const float yT = y;
  const float yB = y + h;
  const float yM = y + h * 0.5f;

  switch (jung) {
    case 8: // ㅗ
      ct.line(xL, yB, xR, yB);
      ct.line(xM, yT, xM, yB);
      break;
    case 12: // ㅛ
      ct.line(xL, yB, xR, yB);
      ct.line(x1, yT, x1, yB);
      ct.line(x2, yT, x2, yB);
      break;
    case 13: // ㅜ
      ct.line(xL, yT, xR, yT);
      ct.line(xM, yT, xM, yB);
      break;
    case 17: // ㅠ
      ct.line(xL, yT, xR, yT);
      ct.line(x1, yT, x1, yB);
      ct.line(x2, yT, x2, yB);
      break;
    case 18: // ㅡ
      ct.line(xL, yM, xR, yM);
      break;
    default:
      break;
  }
}

// Mixed vowel helper: draws horizontal part in hBox and vertical part in vBox
// jung indices: 9:ㅘ 10:ㅙ 11:ㅚ 14:ㅝ 15:ㅞ 16:ㅟ 19:ㅢ
inline void draw_mixed_vowel(const CanvasTransform &ct, int jung,
                             float hx, float hy, float hw, float hh,
                             float vx, float vy, float vw, float vh) {
  switch (jung) {
    case 9: // ㅘ = ㅗ + ㅏ
      draw_horizontal_vowel(ct, 8, hx, hy, hw, hh);
      draw_vertical_vowel(ct, 0, vx, vy, vw, vh);
      break;
    case 10: // ㅙ = ㅗ + ㅐ
      draw_horizontal_vowel(ct, 8, hx, hy, hw, hh);
      draw_vertical_vowel(ct, 1, vx, vy, vw, vh);
      break;
    case 11: // ㅚ = ㅗ + ㅣ
      draw_horizontal_vowel(ct, 8, hx, hy, hw, hh);
      draw_vertical_vowel(ct, 20, vx, vy, vw, vh);
      break;
    case 14: // ㅝ = ㅜ + ㅓ
      draw_horizontal_vowel(ct, 13, hx, hy, hw, hh);
      draw_vertical_vowel(ct, 4, vx, vy, vw, vh);
      break;
    case 15: // ㅞ = ㅜ + ㅔ
      draw_horizontal_vowel(ct, 13, hx, hy, hw, hh);
      draw_vertical_vowel(ct, 5, vx, vy, vw, vh);
      break;
    case 16: // ㅟ = ㅜ + ㅣ
      draw_horizontal_vowel(ct, 13, hx, hy, hw, hh);
      draw_vertical_vowel(ct, 20, vx, vy, vw, vh);
      break;
    case 19: // ㅢ = ㅡ + ㅣ
      draw_horizontal_vowel(ct, 18, hx, hy, hw, hh);
      draw_vertical_vowel(ct, 20, vx, vy, vw, vh);
      break;
    default:
      break;
  }
}

inline void draw_jongseong(const CanvasTransform &ct, int jong, float x, float y, float w, float h) {
  static const int JONG_LEFT[28] = {
      -1, 0, 1, 0, 2, 2, 2, 3, 5, 5, 5, 5, 5, 5, 5, 5, 6, 7, 7, 9, 10, 11, 12, 14, 15, 16, 17, 18};
  static const int JONG_RIGHT[28] = {
      -1, -1, -1, 9, -1, 12, 18, -1, -1, 0, 6, 7, 9, 16, 17, 18, -1, -1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1};

  if (jong <= 0 || jong >= 28) return;
  int left_id = JONG_LEFT[jong];
  int right_id = JONG_RIGHT[jong];
  if (right_id == -1) {
    draw_base_consonant(ct, left_id, x, y, w, h);
  } else {
    draw_base_consonant(ct, left_id, x, y, w * 0.45f, h);
    draw_base_consonant(ct, right_id, x + w * 0.55f, y, w * 0.45f, h);
  }
}

inline bool is_vertical_vowel(int jung) { return jung <= 7 || jung == 20; }
inline bool is_horizontal_vowel(int jung) {
  return jung == 8 || jung == 12 || jung == 13 || jung == 17 || jung == 18;
}

inline void draw_hangul_syllable(const CanvasTransform &ct, uint32_t code, float ox, float oy) {
  int idx = static_cast<int>(code - 0xAC00);
  int jong = idx % 28;
  int jung = (idx / 28) % 21;
  int cho = idx / (28 * 21);

  if (is_vertical_vowel(jung)) {
    if (jong == 0) {
      draw_base_consonant(ct, cho, ox + 10, oy + 18, 36, 62);
      draw_vertical_vowel(ct, jung, ox + 52, oy + 12, 36, 76);
    } else {
      draw_base_consonant(ct, cho, ox + 10, oy + 12, 36, 40);
      draw_vertical_vowel(ct, jung, ox + 52, oy + 10, 36, 46);
      draw_jongseong(ct, jong, ox + 18, oy + 62, 64, 28);
    }
  } else if (is_horizontal_vowel(jung)) {
    if (jong == 0) {
      draw_base_consonant(ct, cho, ox + 20, oy + 12, 60, 40);
      draw_horizontal_vowel(ct, jung, ox + 10, oy + 60, 80, 26);
    } else {
      draw_base_consonant(ct, cho, ox + 22, oy + 10, 56, 28);
      draw_horizontal_vowel(ct, jung, ox + 12, oy + 44, 76, 15);
      draw_jongseong(ct, jong, ox + 20, oy + 65, 60, 25);
    }
  } else {
    if (jong == 0) {
      draw_base_consonant(ct, cho, ox + 10, oy + 12, 36, 36);
      draw_mixed_vowel(ct, jung, ox + 8, oy + 54, 42, 24, ox + 54, oy + 12, 34, 76);
    } else {
      draw_base_consonant(ct, cho, ox + 10, oy + 10, 36, 26);
      draw_mixed_vowel(ct, jung, ox + 8, oy + 41, 42, 15, ox + 54, oy + 10, 34, 48);
      draw_jongseong(ct, jong, ox + 18, oy + 64, 64, 26);
    }
  }
}

inline int compat_consonant_to_base(uint32_t code) {
  switch (code) {
    case 0x3131: return 0;  // ㄱ
    case 0x3132: return 1;  // ㄲ
    case 0x3134: return 2;  // ㄴ
    case 0x3137: return 3;  // ㄷ
    case 0x3138: return 4;  // ㄸ
    case 0x3139: return 5;  // ㄹ
    case 0x3141: return 6;  // ㅁ
    case 0x3142: return 7;  // ㅂ
    case 0x3143: return 8;  // ㅃ
    case 0x3145: return 9;  // ㅅ
    case 0x3146: return 10; // ㅆ
    case 0x3147: return 11; // ㅇ
    case 0x3148: return 12; // ㅈ
    case 0x3149: return 13; // ㅉ
    case 0x314A: return 14; // ㅊ
    case 0x314B: return 15; // ㅋ
    case 0x314C: return 16; // ㅌ
    case 0x314D: return 17; // ㅍ
    case 0x314E: return 18; // ㅎ
    default: return -1;
  }
}

inline void draw_digit_or_symbol(const CanvasTransform &ct, uint32_t code, float ox, float oy) {
  switch (code) {
    case '.':
      ct.fill_ellipse(ox + 28, oy + 82, 5.5f, 5.5f);
      break;
    case ',':
      ct.poly({{ox + 30, oy + 78}, {ox + 28, oy + 88}, {ox + 22, oy + 94}});
      break;
    case '?':
      ct.poly({{ox + 26, oy + 28},
               {ox + 34, oy + 16},
               {ox + 64, oy + 16},
               {ox + 72, oy + 28},
               {ox + 72, oy + 40},
               {ox + 48, oy + 52},
               {ox + 48, oy + 65}});
      ct.fill_ellipse(ox + 48, oy + 82, 5.0f, 5.0f);
      break;
    case '!':
      ct.line(ox + 48, oy + 16, ox + 48, oy + 64);
      ct.fill_ellipse(ox + 48, oy + 82, 5.0f, 5.0f);
      break;
    case '/':
      ct.line(ox + 68, oy + 16, ox + 26, oy + 84);
      break;
    case '%':
      ct.line(ox + 72, oy + 18, ox + 24, oy + 82);
      ct.stroke_ellipse(ox + 30, oy + 30, 10.0f, 10.0f);
      ct.stroke_ellipse(ox + 66, oy + 70, 10.0f, 10.0f);
      break;
    case '+':
      ct.line(ox + 48, oy + 24, ox + 48, oy + 76);
      ct.line(ox + 22, oy + 50, ox + 74, oy + 50);
      break;
    case '-':
      ct.line(ox + 22, oy + 50, ox + 74, oy + 50);
      break;
    case '~':
      ct.poly({{ox + 16, oy + 54}, {ox + 34, oy + 42}, {ox + 62, oy + 58}, {ox + 80, oy + 46}});
      break;
    case ':':
      ct.fill_ellipse(ox + 36, oy + 36, 5.0f, 5.0f);
      ct.fill_ellipse(ox + 36, oy + 68, 5.0f, 5.0f);
      break;
    case '(':
      ct.poly({{ox + 58, oy + 14}, {ox + 38, oy + 34}, {ox + 38, oy + 66}, {ox + 58, oy + 86}});
      break;
    case ')':
      ct.poly({{ox + 34, oy + 14}, {ox + 54, oy + 34}, {ox + 54, oy + 66}, {ox + 34, oy + 86}});
      break;
    case '[':
      ct.poly({{ox + 60, oy + 14}, {ox + 34, oy + 14}, {ox + 34, oy + 86}, {ox + 60, oy + 86}});
      break;
    case ']':
      ct.poly({{ox + 32, oy + 14}, {ox + 58, oy + 14}, {ox + 58, oy + 86}, {ox + 32, oy + 86}});
      break;
    case 0x2605: // ★
    case 0x2606: // ☆
    {
      float cx = ox + 50;
      float cy = oy + 50;
      float rOut = 38;
      float rIn = 16;
      std::vector<Pt> pts;
      for (int i = 0; i < 10; i++) {
        float ang = -3.14159265f / 2.0f + (i * 3.14159265f) / 5.0f;
        float r = (i % 2 == 0) ? rOut : rIn;
        pts.push_back({cx + std::cos(ang) * r, cy + std::sin(ang) * r});
      }
      if (code == 0x2605) {
        ct.fill_poly(pts);
      } else {
        ct.poly(pts, true);
      }
      break;
    }
    case 0x25C0: // ◀
      ct.fill_poly({{ox + 72, oy + 20}, {ox + 22, oy + 50}, {ox + 72, oy + 80}});
      break;
    case 0x25B6: // ▶
      ct.fill_poly({{ox + 26, oy + 20}, {ox + 76, oy + 50}, {ox + 26, oy + 80}});
      break;
    case 0x25B2: // ▲
      ct.fill_poly({{ox + 50, oy + 22}, {ox + 20, oy + 76}, {ox + 80, oy + 76}});
      break;
    case 0x25BC: // ▼
      ct.fill_poly({{ox + 20, oy + 24}, {ox + 80, oy + 24}, {ox + 50, oy + 78}});
      break;
    case 0x25CF: // ●
      ct.fill_ellipse(ox + 50, oy + 50, 26, 26);
      break;
    case '0':
      ct.stroke_ellipse(ox + 38, oy + 50, 22, 34);
      break;
    case '1':
      ct.poly({{ox + 24, oy + 28}, {ox + 40, oy + 16}, {ox + 40, oy + 84}});
      ct.line(ox + 22, oy + 84, ox + 58, oy + 84);
      break;
    case '2':
      ct.poly({{ox + 18, oy + 30},
               {ox + 28, oy + 16},
               {ox + 50, oy + 16},
               {ox + 60, oy + 28},
               {ox + 60, oy + 42},
               {ox + 18, oy + 84},
               {ox + 62, oy + 84}});
      break;
    case '3':
      ct.poly({{ox + 18, oy + 22},
               {ox + 30, oy + 16},
               {ox + 52, oy + 16},
               {ox + 60, oy + 28},
               {ox + 50, oy + 48},
               {ox + 32, oy + 48}});
      ct.poly({{ox + 50, oy + 48},
               {ox + 62, oy + 62},
               {ox + 52, oy + 84},
               {ox + 28, oy + 84},
               {ox + 18, oy + 76}});
      break;
    case '4':
      ct.poly({{ox + 48, oy + 84}, {ox + 48, oy + 16}, {ox + 16, oy + 62}, {ox + 64, oy + 62}});
      break;
    case '5':
      ct.poly({{ox + 58, oy + 16},
               {ox + 20, oy + 16},
               {ox + 20, oy + 46},
               {ox + 48, oy + 46},
               {ox + 60, oy + 58},
               {ox + 58, oy + 74},
               {ox + 46, oy + 84},
               {ox + 20, oy + 82}});
      break;
    case '6':
      ct.poly({{ox + 54, oy + 20},
               {ox + 34, oy + 16},
               {ox + 18, oy + 36},
               {ox + 18, oy + 70},
               {ox + 30, oy + 84},
               {ox + 50, oy + 84},
               {ox + 60, oy + 70},
               {ox + 58, oy + 54},
               {ox + 44, oy + 46},
               {ox + 18, oy + 52}});
      break;
    case '7':
      ct.poly({{ox + 16, oy + 18}, {ox + 60, oy + 18}, {ox + 30, oy + 84}});
      break;
    case '8':
      ct.stroke_ellipse(ox + 38, oy + 32, 18, 16);
      ct.stroke_ellipse(ox + 38, oy + 66, 21, 18);
      break;
    case '9':
      ct.poly({{ox + 58, oy + 46},
               {ox + 34, oy + 52},
               {ox + 18, oy + 42},
               {ox + 18, oy + 26},
               {ox + 32, oy + 16},
               {ox + 50, oy + 16},
               {ox + 60, oy + 30},
               {ox + 58, oy + 68},
               {ox + 42, oy + 84},
               {ox + 22, oy + 80}});
      break;
    default:
      break;
  }
}

// Decode UTF-8 string into Unicode code points
inline std::vector<uint32_t> utf8_to_codepoints(const std::string &text) {
  std::vector<uint32_t> cps;
  size_t i = 0;
  while (i < text.size()) {
    uint8_t c = static_cast<uint8_t>(text[i]);
    if (c < 0x80) {
      cps.push_back(c);
      i += 1;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
      uint32_t cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(text[i + 1]) & 0x3F);
      cps.push_back(cp);
      i += 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
      uint32_t cp = ((c & 0x0F) << 12) |
                    ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 6) |
                    (static_cast<uint8_t>(text[i + 2]) & 0x3F);
      cps.push_back(cp);
      i += 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
      uint32_t cp = ((c & 0x07) << 18) |
                    ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 12) |
                    ((static_cast<uint8_t>(text[i + 2]) & 0x3F) << 6) |
                    (static_cast<uint8_t>(text[i + 3]) & 0x3F);
      cps.push_back(cp);
      i += 4;
    } else {
      i += 1;
    }
  }
  return cps;
}

inline int codepoint_advance(uint32_t code) {
  if (code == ' ') return 42;
  if (code == '.' || code == ',' || code == ':') return 52;
  if (code == '(' || code == ')' || code == '[' || code == ']') return 68;
  if (code >= '0' && code <= '9') return 76;
  if (code == '/' || code == '-' || code == '+' || code == '?' || code == '!') return 82;
  if (code == '%') return 92;
  if (code >= 0xAC00 && code <= 0xD7A3) return 104;
  return 96;
}

inline int measure_hangul_line_px(const std::string &text, float cell_size) {
  auto cps = utf8_to_codepoints(text);
  int cursor_x = 8;
  for (uint32_t cp : cps) {
    cursor_x += codepoint_advance(cp);
  }
  int vb_width = std::max(40, cursor_x + 8);
  return std::max(8, static_cast<int>(std::lround((vb_width / 100.0f) * cell_size)));
}

inline void draw_hangul_line(esphome::display::Display &it, int x, int y, const std::string &text,
                             float cell_size, float stroke_width, esphome::Color color,
                             bool center_x = false) {
  int px_width = measure_hangul_line_px(text, cell_size);
  int start_x = center_x ? (x - px_width / 2) : x;
  CanvasTransform ct(&it, start_x, y, cell_size, stroke_width, color);

  auto cps = utf8_to_codepoints(text);
  float cursor_x = 8.0f;
  for (uint32_t cp : cps) {
    int adv = codepoint_advance(cp);
    if (cp != ' ') {
      if (cp >= 0xAC00 && cp <= 0xD7A3) {
        draw_hangul_syllable(ct, cp, cursor_x, 0.0f);
      } else {
        int base_c = compat_consonant_to_base(cp);
        if (base_c >= 0) {
          draw_base_consonant(ct, base_c, cursor_x + 18.0f, 18.0f, 64.0f, 64.0f);
        } else {
          draw_digit_or_symbol(ct, cp, cursor_x, 0.0f);
        }
      }
    }
    cursor_x += adv;
  }
}

inline std::vector<std::string> wrap_korean_sentence(const std::string &sentence, size_t max_chars_per_line) {
  std::vector<std::string> lines;
  // Split by whitespace
  std::vector<std::string> words;
  size_t i = 0;
  while (i < sentence.size()) {
    while (i < sentence.size() && (sentence[i] == ' ' || sentence[i] == '\t' || sentence[i] == '\r' || sentence[i] == '\n')) {
      i++;
    }
    if (i >= sentence.size()) break;
    size_t start = i;
    while (i < sentence.size() && sentence[i] != ' ' && sentence[i] != '\t' && sentence[i] != '\r' && sentence[i] != '\n') {
      i++;
    }
    words.push_back(sentence.substr(start, i - start));
  }

  std::string current;
  size_t current_cp_len = 0;
  for (const auto &word : words) {
    size_t word_cp_len = utf8_to_codepoints(word).size();
    if (current.empty()) {
      current = word;
      current_cp_len = word_cp_len;
    } else if (current_cp_len + 1 + word_cp_len <= max_chars_per_line) {
      current += " " + word;
      current_cp_len += 1 + word_cp_len;
    } else {
      lines.push_back(current);
      current = word;
      current_cp_len = word_cp_len;
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  return lines;
}

inline void draw_hangul_sentence_centered(esphome::display::Display &it, int center_x, int center_y,
                                          const std::string &sentence, esphome::Color color) {
  size_t len = utf8_to_codepoints(sentence).size();
  size_t max_chars = 11;
  float cell_size = 54.0f;
  float stroke_width = 9.0f;

  if (len <= 10) {
    max_chars = 10;
    cell_size = 58.0f;
    stroke_width = 9.2f;
  } else if (len <= 20) {
    max_chars = 11;
    cell_size = 50.0f;
    stroke_width = 8.8f;
  } else {
    max_chars = 13;
    cell_size = 44.0f;
    stroke_width = 8.5f;
  }

  auto lines = wrap_korean_sentence(sentence, max_chars);
  if (lines.empty()) return;

  int line_height = static_cast<int>(std::lround(cell_size));
  int gap = 20;
  int total_h = static_cast<int>(lines.size()) * line_height + (static_cast<int>(lines.size()) - 1) * gap;
  int cur_y = center_y - total_h / 2;

  for (const auto &ln : lines) {
    draw_hangul_line(it, center_x, cur_y, ln, cell_size, stroke_width, color, true);
    cur_y += line_height + gap;
  }
}

}  // namespace sticky_hangul
