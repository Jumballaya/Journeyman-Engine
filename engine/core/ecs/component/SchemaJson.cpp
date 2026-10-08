#include "SchemaJson.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

const char* kindName(FieldSchema::Kind kind) {
  switch (kind) {
    case FieldSchema::Kind::Number: return "number";
    case FieldSchema::Kind::Integer: return "integer";
    case FieldSchema::Kind::Bool: return "bool";
    case FieldSchema::Kind::Text: return "text";
    case FieldSchema::Kind::Vec2: return "vec2";
    case FieldSchema::Kind::Vec3: return "vec3";
    case FieldSchema::Kind::Color: return "color";
    case FieldSchema::Kind::Angle: return "angle";
    case FieldSchema::Kind::Mask: return "mask";
    case FieldSchema::Kind::Choice: return "choice";
    case FieldSchema::Kind::Asset: return "asset";
    case FieldSchema::Kind::Group: return "group";
    case FieldSchema::Kind::StringMap: return "stringMap";
    case FieldSchema::Kind::Json: return "json";
  }
  return "json";
}

// Floats as written in the source (0.05, not 0.05000000074505806 from the
// float's widening to double), in numbers and arrays of them.
nlohmann::json tidy(nlohmann::json v) {
  if (v.is_number_float()) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.7g", v.get<double>());
    return std::strtod(text, nullptr);
  }
  if (v.is_array()) {
    for (auto& item : v) item = tidy(item);
  }
  return v;
}

nlohmann::json fieldJson(const FieldSchema& f) {
  nlohmann::json out = {{"key", f.key}, {"kind", kindName(f.kind)}, {"default", tidy(f.defaultValue)}};
  if (!f.hint.empty()) out["hint"] = f.hint;
  if (f.min != f.max) {
    out["min"] = tidy(f.min);
    out["max"] = tidy(f.max);
  }
  if (f.step > 0.0f) out["step"] = tidy(f.step);
  if (f.multiline) out["multiline"] = true;
  if (!f.choices.empty()) out["choices"] = f.choices;
  if (!f.assetTypes.empty()) out["assetTypes"] = f.assetTypes;
  if (f.kind == FieldSchema::Kind::Group) {
    out["fields"] = nlohmann::json::array();
    for (const FieldSchema& sub : f.fields) out["fields"].push_back(fieldJson(sub));
  }
  return out;
}

}  // namespace

nlohmann::json schemaJson(const ComponentRegistry& registry, const std::map<std::string, std::string>& hostFunctions) {
  nlohmann::json components = nlohmann::json::object();  // sorted by name: stable output
  registry.forEachRegisteredComponent([&](ComponentId id) {
    const ComponentInfo* info = registry.getInfo(id);
    if (!info) return;
    nlohmann::json fields = nlohmann::json::array(), scriptFields = nlohmann::json::array();
    for (const FieldSchema& f : info->schema.fields) fields.push_back(fieldJson(f));
    for (const ScriptField& f : info->scriptFields) scriptFields.push_back({{"name", f.name}, {"type", f.integer ? "u32" : "f32"}});
    components[info->name] = {{"label", info->schema.label},
                              {"category", info->schema.category},
                              {"summary", info->schema.summary},
                              {"fields", std::move(fields)},
                              {"scriptFields", std::move(scriptFields)}};
  });
  nlohmann::json out = {{"schemaVersion", 1}, {"components", std::move(components)}};
  if (!hostFunctions.empty()) out["hostFunctions"] = hostFunctions;
  return out;
}
