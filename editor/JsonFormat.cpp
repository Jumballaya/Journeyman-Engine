#include "JsonFormat.hpp"

#include "Entities.hpp"

namespace {

constexpr size_t kInlineWidth = 72;

void write(std::string& out, const Json& value, int depth);

void indent(std::string& out, int depth) { out.append(static_cast<size_t>(depth) * 2, ' '); }

// An array of scalars on one line, if it fits.
bool inlineArray(const Json& array, std::string& line) {
  line = "[";
  for (size_t i = 0; i < array.size(); ++i) {
    if (array[i].is_structured()) return false;
    line += (i ? ", " : "") + array[i].dump();
  }
  line += "]";
  return line.size() <= kInlineWidth;
}

void write(std::string& out, const Json& value, int depth) {
  if (value.is_object()) {
    if (value.empty()) {
      out += "{}";
      return;
    }
    out += "{\n";
    size_t i = 0;
    for (const auto& [key, item] : value.items()) {
      indent(out, depth + 1);
      out += Json(key).dump() + ": ";
      write(out, item, depth + 1);
      out += ++i < value.size() ? ",\n" : "\n";
    }
    indent(out, depth);
    out += "}";
  } else if (value.is_array()) {
    if (value.empty()) {
      out += "[]";
      return;
    }
    if (std::string line; inlineArray(value, line)) {
      out += line;
      return;
    }
    out += "[\n";
    for (size_t i = 0; i < value.size(); ++i) {
      indent(out, depth + 1);
      write(out, value[i], depth + 1);
      out += i + 1 < value.size() ? ",\n" : "\n";
    }
    indent(out, depth);
    out += "]";
  } else {
    out += value.dump();
  }
}

}  // namespace

std::string formatJson(const Json& value) {
  Json clean = value;
  wholeNumbersAsIntegers(clean);
  std::string out;
  write(out, clean, 0);
  return out + "\n";
}
