// Draw list renderer for the GeekMagic SmallTV Pro with ESPHome.
//
// The clock fetches a small JSON document from a server and draws it itself, because a full
// 240x240 RGB565 frame (115 KB) does not fit into the memory of this ESP32 without PSRAM.
//
// Format:
//   {"brightness": 0..100 (optional, 0 = backlight off), "ops": [ ... ]}
// Operations (colors are [r, g, b]):
//   ["f", color]                              fill the background (should be the first op)
//   ["r", x, y, w, h, color]                  filled rectangle
//   ["c", x, y, radius, color]                filled circle
//   ["p", [x1, y1, x2, y2, ...], color]       filled convex polygon
//   ["l", x1, y1, x2, y2, color]              line
//   ["t", x, y, font, color, text, align?]    text; font is a key like "b24" (see the yaml),
//                                             align "l" (default), "c" or "r" relative to x
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "esphome/components/display/display.h"
#include "esphome/components/json/json_util.h"

namespace drawlist {

struct Op {
  char kind{0};
  std::vector<int16_t> v;
  esphome::Color color;
  std::string font;
  std::string text;
  char align{'l'};
};

// State lives here instead of in "globals": ESPHome includes this file after the globals,
// so custom types are not known there yet.
inline std::vector<Op> &ops() {
  static std::vector<Op> list;
  return list;
}
inline esphome::Color &background() {
  static esphome::Color c(16, 16, 20);
  return c;
}
// Brightness from the last document in percent, -1 if the server did not send one.
inline int &brightness() {
  static int b = -1;
  return b;
}

inline esphome::Color color_of(JsonVariant v) {
  JsonArray a = v.as<JsonArray>();
  return esphome::Color(a[0].as<int>(), a[1].as<int>(), a[2].as<int>());
}

// Parses a document. On success it replaces ops(), background() and brightness();
// on failure everything stays as it was.
inline bool parse(const std::string &body) {
  std::vector<Op> list;
  esphome::Color bg = background();
  int bright = -1;
  bool ok = esphome::json::parse_json(body, [&list, &bg, &bright](JsonObject root) -> bool {
    JsonArray in = root["ops"].as<JsonArray>();
    if (in.isNull())
      return false;
    if (!root["brightness"].isNull())
      bright = root["brightness"].as<int>();
    for (JsonVariant ev : in) {
      JsonArray e = ev.as<JsonArray>();
      const char *kind = e[0].as<const char *>();
      if (kind == nullptr || kind[0] == 0)
        continue;
      Op op;
      op.kind = kind[0];
      switch (op.kind) {
        case 'f':
          op.color = color_of(e[1]);
          bg = op.color;
          break;
        case 'r':
        case 'l':
        case 'c': {
          size_t n = op.kind == 'c' ? 3 : 4;
          for (size_t i = 1; i <= n; i++)
            op.v.push_back(e[i].as<int>());
          op.color = color_of(e[n + 1]);
          break;
        }
        case 'p':
          for (JsonVariant p : e[1].as<JsonArray>())
            op.v.push_back(p.as<int>());
          op.color = color_of(e[2]);
          break;
        case 't': {
          op.v.push_back(e[1].as<int>());
          op.v.push_back(e[2].as<int>());
          op.font = e[3].as<std::string>();
          op.color = color_of(e[4]);
          op.text = e[5].as<std::string>();
          const char *a = e[6].as<const char *>();
          if (a != nullptr && (a[0] == 'c' || a[0] == 'r'))
            op.align = a[0];
          break;
        }
        default:
          continue;  // unknown operations are skipped, not fatal
      }
      list.push_back(std::move(op));
    }
    return true;
  });
  if (ok) {
    ops().swap(list);
    background() = bg;
    brightness() = bright;
  }
  return ok;
}

// Draws the current list. font_for maps a key like "b24" to a font (or nullptr).
// With a partial display buffer, every pass must start with a full fill; keep "f" first.
template<typename F> void draw(esphome::display::Display &it, F font_for) {
  for (const auto &op : ops()) {
    switch (op.kind) {
      case 'f':
        it.fill(op.color);
        break;
      case 'r':
        it.filled_rectangle(op.v[0], op.v[1], op.v[2], op.v[3], op.color);
        break;
      case 'c':
        it.filled_circle(op.v[0], op.v[1], op.v[2], op.color);
        break;
      case 'l':
        it.line(op.v[0], op.v[1], op.v[2], op.v[3], op.color);
        break;
      case 'p':
        // fan triangulation, fine for convex polygons
        for (size_t i = 2; i + 3 < op.v.size(); i += 2)
          it.filled_triangle(op.v[0], op.v[1], op.v[i], op.v[i + 1], op.v[i + 2], op.v[i + 3], op.color);
        break;
      case 't': {
        auto *f = font_for(op.font);
        if (f == nullptr)
          break;
        auto align = op.align == 'c'   ? esphome::display::TextAlign::TOP_CENTER
                     : op.align == 'r' ? esphome::display::TextAlign::TOP_RIGHT
                                       : esphome::display::TextAlign::TOP_LEFT;
        it.print(op.v[0], op.v[1], f, op.color, align, op.text.c_str(), background());
        break;
      }
      default:
        break;
    }
  }
}

}  // namespace drawlist
