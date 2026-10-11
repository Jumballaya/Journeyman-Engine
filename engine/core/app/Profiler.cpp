#include "Profiler.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

double ms(double seconds) { return std::round(seconds * 1e5) / 100.0; }  // 0.01 ms

const char* kindName(Profiler::Kind kind) {
  switch (kind) {
    case Profiler::Kind::System: return "systems";
    case Profiler::Kind::Script: return "scripts";
    case Profiler::Kind::Module: return "modules";
    case Profiler::Kind::Count: return "counts";
  }
  return "";
}

}  // namespace

void Profiler::add(Kind kind, std::string_view name, double value) {
  _probe.first = kind;
  _probe.second.assign(name);
  auto it = _series.find(_probe);
  if (it == _series.end()) it = _series.emplace(_probe, Series{}).first;
  it->second.pending += value;
}

void Profiler::endFrame(double seconds) {
  const size_t slot = _frames % kWindow;
  _frame.values[slot] = seconds;
  for (auto it = _series.begin(); it != _series.end();) {
    Series& series = it->second;
    series.values[slot] = std::exchange(series.pending, 0.0);
    // Gone a whole window (its entity or scene is): dropped.
    const bool idle = _frames >= kWindow && std::ranges::all_of(series.values, [](double v) { return v == 0.0; });
    it = idle ? _series.erase(it) : std::next(it);
  }
  ++_frames;
}

nlohmann::json Profiler::report() const {
  const size_t n = std::min(_frames, kWindow);
  const auto stats = [n](const Series& s) {
    double sum = 0.0, worst = 0.0;
    for (size_t i = 0; i < n; ++i) {
      sum += s.values[i];
      worst = std::max(worst, s.values[i]);
    }
    return std::pair(n ? sum / static_cast<double>(n) : 0.0, worst);
  };
  const auto [frameAvg, frameWorst] = stats(_frame);
  nlohmann::json out = {{"frames", n}, {"frame", {{"avgMs", ms(frameAvg)}, {"worstMs", ms(frameWorst)}}}};
  for (const Kind kind : {Kind::System, Kind::Script, Kind::Module}) out[kindName(kind)] = nlohmann::json::array();
  out["counts"] = nlohmann::json::object();
  for (const auto& [key, series] : _series) {
    const auto [avg, worst] = stats(series);
    if (key.first == Kind::Count) {
      out["counts"][key.second] = {{"avg", std::round(avg * 10.0) / 10.0}, {"max", worst}};
    } else {
      out[kindName(key.first)].push_back({{"name", key.second}, {"avgMs", ms(avg)}, {"worstMs", ms(worst)}, {"avg", avg}});
    }
  }
  for (const Kind kind : {Kind::System, Kind::Script, Kind::Module}) {
    nlohmann::json& list = out[kindName(kind)];
    std::stable_sort(list.begin(), list.end(),
                     [](const nlohmann::json& a, const nlohmann::json& b) { return a["avg"] > b["avg"]; });
    for (nlohmann::json& entry : list) entry.erase("avg");  // sorted by the unrounded value
  }
  return out;
}
