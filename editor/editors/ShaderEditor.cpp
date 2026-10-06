// The shader editor (*.frag): the code beside a live preview of the open scene
// drawn through it, recompiled as you type, with compile errors inline. Its
// uniforms get controls in the Inspector; transitions scrub or loop their
// progress.

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "AssetEditor.hpp"
#include "Editor.hpp"
#include "HostedEngine.hpp"
#include "Icons.hpp"
#include "LogBook.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

// A uniform the shader declares (other than the ones the engine provides).
struct Uniform {
  std::string type, name;
};

std::vector<Uniform> declaredUniforms(const std::string& source) {
  static const std::set<std::string> kProvided = {"u_primary", "u_aux", "u_progress", "u_resolution", "u_viewport", "u_logical", "u_time"};
  static const std::regex declaration(R"(uniform\s+(float|int|vec2|vec3|vec4)\s+(\w+)\s*;)");
  std::vector<Uniform> out;
  for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it) {
    if (!kProvided.contains((*it)[2])) out.push_back({(*it)[1], (*it)[2]});
  }
  return out;
}

bool isColor(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  return lower.find("color") != std::string::npos || lower.find("colour") != std::string::npos || lower.find("tint") != std::string::npos;
}

class ShaderEditor final : public AssetEditor {
 public:
  void draw(Editor& editor, AssetDocument& doc) override;
  bool drawInspector(Editor& editor, AssetDocument& doc) override;

 private:
  std::unique_ptr<HostedEngine> _engine;
  std::string _engineScene;
  uint64_t _engineBuild = ~0ull;
  std::string _engineError;
  uint64_t _compiled = ~0ull;  // the document revision last compiled
  std::string _error;
  std::map<std::string, glm::vec4> _values;  // uniform values set from the Inspector
  float _progress = 0.5f;
  bool _playing = true;

  bool isTransition(const AssetDocument& doc) const { return doc.text().find("u_progress") != std::string::npos; }
  // Visible by default: strengths and colors at 1.
  glm::vec4& valueOf(const Uniform& u) { return _values.try_emplace(u.name, 1.0f).first->second; }
  void setUniform(const Uniform& u, const glm::vec4& v);
};

void ShaderEditor::setUniform(const Uniform& u, const glm::vec4& v) {
  _values[u.name] = v;
  if (!_engine) return;
  Renderer2DModule& r = _engine->renderer();
  if (u.type == "float") r.setPostEffectUniform(u.name, v.x);
  else if (u.type == "int") r.setPostEffectUniform(u.name, static_cast<int>(v.x));
  else if (u.type == "vec2") r.setPostEffectUniform(u.name, glm::vec2(v));
  else if (u.type == "vec3") r.setPostEffectUniform(u.name, glm::vec3(v));
  else r.setPostEffectUniform(u.name, v);
}

void ShaderEditor::draw(Editor& editor, AssetDocument& doc) {
  // An engine of its own showing the open scene (or the game's first), restarted on builds
  // (not every frame while it fails to start).
  const std::string scene = editor.scene() && !editor.scene()->isPrefab() ? editor.scene()->path()
                                                                           : editor.project()->manifest().value("entryScene", std::string());
  if (_engineScene != scene || _engineBuild != editor.buildGeneration()) {
    _engine.reset();
    _engineScene = scene;
    _engineBuild = editor.buildGeneration();
    HostedEngine::Options options;
    options.simulate = false;
    options.entryScene = scene;
    options.saveDir = settingsDir() / "preview-saves";
    MuteEngineLog mute;
    _engine = HostedEngine::create(editor.project()->buildDir(), options, _engineError);
    _compiled = ~0ull;
  }
  // Recompile as the text changes; uniforms carry over.
  if (_engine && _compiled != doc.revision()) {
    _compiled = doc.revision();
    MuteEngineLog mute;
    const bool compiled = _engine->renderer().showPostEffect(doc.text(), _error);
    // "ERROR: 0:22: ..." → "Line 22: ..." (the engine's prelude ends with #line 1).
    static const std::regex location(R"(ERROR:\s*\d+:(\d+):\s*)");
    _error = std::regex_replace(_error, location, "Line $1: ");
    if (const std::string header = "Shader compilation failed:\n"; _error.starts_with(header)) _error.erase(0, header.size());
    if (compiled) {
      for (const Uniform& u : declaredUniforms(doc.text())) setUniform(u, valueOf(u));
    }
  }
  if (_engine && isTransition(doc)) {
    if (_playing) _progress = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.5f, 1.4f) - 0.2f;  // a pause at each end
    _engine->renderer().setPostEffectUniform("u_progress", std::clamp(_progress, 0.0f, 1.0f));
  }

  const float right = ui::beginDocumentBar(ICON_SPARKLE, doc.title().c_str(), doc.path().c_str());
  const char* status = _error.empty() ? ICON_CHECK_CIRCLE "  Compiled" : ICON_WARNING_OCTAGON "  Doesn't compile";
  ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
  const float statusW = ImGui::CalcTextSize(status).x + 12;
  ImGui::PopFont();
  ImGui::SameLine(right - statusW);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
  ui::badge(status, _error.empty() ? theme::success : theme::error);
  ui::endDocumentBar();

  // Code | preview.
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float codeW = std::floor(avail.x * 0.5f);
  ImGui::BeginChild("##codeSide", {codeW, 0});
  const float errorH = _error.empty() ? 0.0f : std::min(140.0f, 24.0f + ImGui::GetTextLineHeightWithSpacing() * 4);
  std::string text = doc.text();
  ImGui::PushFont(theme::fonts().mono, theme::sizeSmall + 1);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::bg0);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12, 10});
  if (ImGui::InputTextMultiline("##code", &text, {-1, ImGui::GetContentRegionAvail().y - errorH}, ImGuiInputTextFlags_AllowTabInput)) {
    doc.setText(text, "Edit Shader", "code");
  }
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  if (!_error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::withAlpha(theme::error, 0.10f));
    ImGui::BeginChild("##error", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::error);
    ImGui::TextWrapped("%s", _error.c_str());
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
  ImGui::EndChild();
  ImGui::SameLine(0, 0);

  ImGui::BeginChild("##previewSide", {0, 0}, 0, ImGuiWindowFlags_NoScrollbar);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, theme::u32(theme::bg0));
  if (!_engine) {
    ui::emptyState(ICON_HAMMER, "Preview unavailable", _engineError.empty() ? "Build the project to preview shaders." : _engineError.c_str());
  } else {
    // The game's frame, fitted; the effect draws over it like in the game.
    const nlohmann::json& config = _engine->engine().getManifest().config;
    const auto renderer = config.value("renderer", nlohmann::json::object());
    const glm::vec2 game(renderer.value("logicalWidth", 320), renderer.value("logicalHeight", 240));
    const float fit = std::min((size.x - 24) / game.x, (size.y - 64) / game.y);
    const ImVec2 s{std::floor(game.x * fit), std::floor(game.y * fit)};
    const ImVec2 at{std::floor(origin.x + (size.x - s.x) * 0.5f), std::floor(origin.y + (size.y - s.y) * 0.5f)};
    const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
    const unsigned texture = _engine->frame(static_cast<int>(s.x * fb), static_cast<int>(s.y * fb), 0.0f);
    draw->AddImage(static_cast<ImTextureID>(texture), at, {at.x + s.x, at.y + s.y}, {0, 1}, {1, 0});
    draw->AddRect({at.x - 1, at.y - 1}, {at.x + s.x + 1, at.y + s.y + 1}, theme::u32(theme::border));
    const std::string caption = ICON_FILM_SLATE "  " + std::filesystem::path(_engineScene).filename().string() +
                                (isTransition(doc) ? "   transition into black" : "");
    draw->AddText({at.x, at.y + s.y + 8}, theme::u32(theme::textFaint), caption.c_str());
  }
  ImGui::EndChild();
}

bool ShaderEditor::drawInspector(Editor& editor, AssetDocument& doc) {
  ImGui::PushFont(nullptr, 20.0f);
  ImGui::TextColored(theme::accent, ICON_SPARKLE);
  ImGui::PopFont();
  ImGui::SameLine(0, 10);
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(doc.title().c_str());
  ImGui::PopFont();
  ui::smallText(isTransition(doc) ? "Scene transition" : "Post effect", theme::textFaint);
  ImGui::EndGroup();
  ImGui::Dummy({0, 6});

  if (isTransition(doc)) {
    ui::sectionLabel("Progress");
    ImGui::Dummy({0, 2});
    if (ui::iconButton("play", _playing ? ICON_PAUSE : ICON_PLAY, _playing ? "Pause" : "Loop it")) _playing = !_playing;
    ImGui::SameLine(0, 6);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##progress", &_progress, 0.0f, 1.0f, "%.2f")) _playing = false;
    ui::smallText("0 shows the outgoing scene (black here), 1 the new one.", theme::textFaint);
    ImGui::Dummy({0, 6});
  }

  const auto uniforms = declaredUniforms(doc.text());
  ui::sectionLabel("Uniforms");
  if (uniforms.empty()) {
    ui::smallText("Declare a uniform (uniform float u_strength;) to get a control here.", theme::textFaint);
  } else if (ui::beginProperties("uniforms", 110)) {
    for (const Uniform& u : uniforms) {
      ImGui::PushID(u.name.c_str());
      ui::propertyRow(u.name.c_str(), (u.type + "; scripts set it with PostEffect's setFloat / setVec2...").c_str());
      glm::vec4 v = valueOf(u);
      bool changed = false;
      if (u.type == "float" || u.type == "int") changed = ImGui::SliderFloat("##v", &v.x, 0.0f, 2.0f, "%.3f");
      else if (u.type == "vec2") changed = ImGui::DragFloat2("##v", &v.x, 0.01f);
      else if (u.type == "vec3") changed = isColor(u.name) ? ImGui::ColorEdit3("##v", &v.x, ImGuiColorEditFlags_Float) : ImGui::DragFloat3("##v", &v.x, 0.01f);
      else changed = isColor(u.name) ? ImGui::ColorEdit4("##v", &v.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar) : ImGui::DragFloat4("##v", &v.x, 0.01f);
      if (changed) setUniform(u, v);
      ImGui::PopID();
    }
    ui::endProperties();
  }
  ImGui::Dummy({0, 8});
  ui::smallText("These values are for previewing; the game sets its own.", theme::textFaint);
  ImGui::Dummy({0, 8});
  if (ui::button(ICON_CODE "  Open in Code Editor", {-1, 0})) editor.openInCodeEditor(doc.path());
  return true;
}

}  // namespace

std::unique_ptr<AssetEditor> makeShaderEditor() { return std::make_unique<ShaderEditor>(); }
