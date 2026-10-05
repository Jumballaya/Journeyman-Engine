#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// How one key of a component's scene JSON is edited: what it holds, its
// default, and hints for the widget. Editors build inspectors from these.
struct FieldSchema {
  enum class Kind {
    Number,   // float; min/max/step bound a slider or drag
    Integer,
    Bool,
    Text,     // one line; `multiline` for more
    Vec2,     // [x, y]
    Vec3,     // [x, y, z]
    Color,    // [r, g, b, a], 0..1
    Angle,    // radians, shown in degrees
    Mask,     // 32 layer bits, as an unsigned number
    Choice,   // one of `choices` (a string)
    Asset,    // a project path; `assetTypes` lists accepted suffixes
    Group,    // a nested object of `fields`, absent unless enabled
    StringMap,  // {"name": "value", ...}
    Json,     // anything else, edited as JSON text
  };

  std::string key;
  Kind kind = Kind::Number;
  nlohmann::json defaultValue;  // null = absent unless set
  std::string hint;             // one-line tooltip
  float min = 0.0f, max = 0.0f, step = 0.0f;  // min == max = unbounded
  bool multiline = false;
  std::vector<std::string> choices;
  // Asset suffixes like ".png", ".atlas.json"; "#" after an atlas allows a region ("assets/a.atlas.json#ship").
  std::vector<std::string> assetTypes;
  std::vector<FieldSchema> fields;  // Group

  // Builders keep component registrations short.
  static FieldSchema number(std::string key, float def, std::string hint = {}, float min = 0, float max = 0,
                            float step = 0) {
    return {std::move(key), Kind::Number, def, std::move(hint), min, max, step};
  }
  static FieldSchema integer(std::string key, int def, std::string hint = {}) {
    return {std::move(key), Kind::Integer, def, std::move(hint)};
  }
  static FieldSchema boolean(std::string key, bool def, std::string hint = {}) {
    return {std::move(key), Kind::Bool, def, std::move(hint)};
  }
  static FieldSchema text(std::string key, std::string def, std::string hint = {}, bool multiline = false) {
    FieldSchema f{std::move(key), Kind::Text, std::move(def), std::move(hint)};
    f.multiline = multiline;
    return f;
  }
  static FieldSchema vec2(std::string key, float x, float y, std::string hint = {}) {
    return {std::move(key), Kind::Vec2, nlohmann::json::array({x, y}), std::move(hint)};
  }
  static FieldSchema vec3(std::string key, float x, float y, float z, std::string hint = {}) {
    return {std::move(key), Kind::Vec3, nlohmann::json::array({x, y, z}), std::move(hint)};
  }
  static FieldSchema color(std::string key, nlohmann::json def, std::string hint = {}) {
    return {std::move(key), Kind::Color, std::move(def), std::move(hint)};
  }
  static FieldSchema angle(std::string key, std::string hint = {}) {
    return {std::move(key), Kind::Angle, 0.0f, std::move(hint)};
  }
  static FieldSchema mask(std::string key, uint32_t def, std::string hint = {}) {
    return {std::move(key), Kind::Mask, def, std::move(hint)};
  }
  static FieldSchema choice(std::string key, std::vector<std::string> choices, std::string hint = {}) {
    FieldSchema f{std::move(key), Kind::Choice, choices.empty() ? nlohmann::json() : nlohmann::json(choices[0]),
                  std::move(hint)};
    f.choices = std::move(choices);
    return f;
  }
  static FieldSchema asset(std::string key, std::vector<std::string> types, std::string hint = {}) {
    FieldSchema f{std::move(key), Kind::Asset, "", std::move(hint)};
    f.assetTypes = std::move(types);
    return f;
  }
  static FieldSchema group(std::string key, std::vector<FieldSchema> fields, std::string hint = {}) {
    FieldSchema f{std::move(key), Kind::Group, nlohmann::json(), std::move(hint)};
    f.fields = std::move(fields);
    return f;
  }
  static FieldSchema stringMap(std::string key, std::string hint = {}) {
    return {std::move(key), Kind::StringMap, nlohmann::json(), std::move(hint)};
  }
  static FieldSchema json(std::string key, std::string hint = {}) {
    return {std::move(key), Kind::Json, nlohmann::json(), std::move(hint)};
  }
};

// What an editor shows for a component as a whole.
struct ComponentSchema {
  std::string label;     // "Sprite"; empty = derived from the name
  std::string category;  // "Rendering", "Physics", ...
  std::string summary;   // one line for the add-component menu
  std::vector<FieldSchema> fields;
};
