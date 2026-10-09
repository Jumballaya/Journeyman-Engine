// Modal dialogs: Export Game and Project Settings.

#include <cmath>
#include <cstring>
#include <filesystem>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "CliRunner.hpp"
#include "Icons.hpp"
#include "FolderPicker.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace fs = std::filesystem;

namespace {

struct Target {
  const char* id;     // jm's --target
  const char* label;  // shown
  const char* icon;
  const char* exe;    // the player's file name
};

constexpr Target kTargets[] = {
    {"darwin-arm64", "macOS (Apple Silicon)", ICON_APPLE_LOGO, "journeyman_engine"},
    {"darwin-amd64", "macOS (Intel)", ICON_APPLE_LOGO, "journeyman_engine"},
    {"windows-amd64", "Windows", ICON_WINDOWS_LOGO, "journeyman_engine.exe"},
    {"linux-amd64", "Linux", ICON_LINUX_LOGO, "journeyman_engine"},
};

#if defined(__APPLE__) && defined(__aarch64__)
constexpr int kHostTarget = 0;
#elif defined(__APPLE__)
constexpr int kHostTarget = 1;
#elif defined(_WIN32)
constexpr int kHostTarget = 2;
#else
constexpr int kHostTarget = 3;
#endif

// The engine (or server) file a platform's export starts from.
std::string exeFor(const Target& target, bool server) {
  std::string exe = target.exe;
  if (server) exe.replace(exe.find("journeyman_engine"), std::strlen("journeyman_engine"), "journeyman_server");
  return exe;
}

// Where jm looks for other platforms' players (export.go: findPlayer).
std::optional<fs::path> findPlayer(const Target& target, bool server) {
  std::vector<fs::path> dirs;
  if (const char* env = std::getenv("JM_PLAYERS")) dirs.push_back(env);
  if (const fs::path jm = CliRunner::locate(); !jm.empty()) {
    dirs.push_back(jm.parent_path() / "players");
    dirs.push_back(jm.parent_path() / ".." / "players");
  }
  const std::string exe = exeFor(target, server);
  for (const fs::path& dir : dirs) {
    if (fs::exists(dir / target.id / exe)) return dir / target.id / exe;
  }
  return std::nullopt;
}

// A manifest value, or `fallback` when it's missing or of another type (a hand edit).
template <class T>
T field(const Json& object, const char* key, T fallback) {
  try {
    return object.value(key, fallback);
  } catch (const Json::exception&) {
    return fallback;
  }
}

// Removes empty objects, recursively: settings sections create them as they're shown.
void pruneEmptyObjects(Json& v) {
  if (!v.is_object()) return;
  for (auto it = v.begin(); it != v.end();) {
    pruneEmptyObjects(*it);
    it = it->is_object() && it->empty() ? v.erase(it) : std::next(it);
  }
}

// Cancel and Primary, right-aligned; either closes the popup. True when Primary is clicked.
bool dialogButtons(const char* primary, bool primaryEnabled) {
  ImGui::Dummy({0, 8});
  ImGui::Separator();
  ImGui::Dummy({0, 6});
  const float bw = 110.0f;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - bw * 2 - 8);
  const bool cancel = ui::button("Cancel", {bw, 0}) || ui::dismissPressed();
  ImGui::SameLine(0, 8);
  ImGui::BeginDisabled(!primaryEnabled);
  const bool ok = ui::primaryButton(primary, {bw, 0});
  ImGui::EndDisabled();
  if (cancel || ok) ImGui::CloseCurrentPopup();
  return ok;
}

}  // namespace

void ExportDialog::draw(Editor& editor) {
  if (_open) {
    ImGui::OpenPopup("Export Game");
    _open = false;
    _target = kHostTarget;
  }
  ui::centerNextWindow({580, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22, 20});
  if (!ImGui::BeginPopupModal("Export Game", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                          ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PopStyleVar();
    return;
  }
  Project& project = *editor.project();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle + 2);
  ImGui::TextUnformatted(ICON_PACKAGE "  Export Game");
  ImGui::PopFont();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 536);
  ui::dimText(_server ? "Builds one executable with the server and the game's files inside: run it where players can reach it."
                      : "Builds one executable with the engine and every asset inside. Players need nothing else installed.");
  ImGui::PopTextWrapPos();
  ImGui::Dummy({0, 10});

  // What: the game, or its dedicated multiplayer server.
  const bool multiplayer = project.manifest().contains("net");
  if (multiplayer) {
    ui::sectionLabel("Export");
    if (ui::beginProperties("what", 120)) {
      ui::propertyRow("What", "The server is the game without its window, sound or UI, for players to connect to");
      if (ui::beginCombo("##what", _server ? "Dedicated server" : "Game")) {
        if (ImGui::Selectable("Game", !_server)) _server = false;
        if (ImGui::Selectable("Dedicated server", _server)) _server = true;
        ImGui::EndCombo();
      }
      ui::endProperties();
    }
    ImGui::Dummy({0, 6});
  } else {
    _server = false;
  }

  // Target: one card per platform.
  ui::sectionLabel("Platform");
  for (int i = 0; i < static_cast<int>(std::size(kTargets)); ++i) {
    const Target& t = kTargets[i];
    const bool isHost = i == kHostTarget;
    const bool ready = isHost || findPlayer(t, _server).has_value();
    if (i % 2) ImGui::SameLine(0, 8);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = 264.0f, h = 52.0f;
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##target", {w, h})) _target = i;
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool selected = _target == i;
    draw->AddRectFilled(p, {p.x + w, p.y + h}, theme::u32(selected || hovered ? theme::bg3 : theme::bg1), theme::radiusOverlay);
    if (selected) draw->AddRect(p, {p.x + w, p.y + h}, theme::u32(theme::accent), theme::radiusOverlay, 1.5f);
    ImGui::PushFont(nullptr, 20.0f);
    draw->AddText({p.x + 14, p.y + 14}, theme::u32(selected ? theme::accent : theme::textDim), t.icon);
    ImGui::PopFont();
    ImGui::PushFont(theme::fonts().medium, 0.0f);
    draw->AddText({p.x + 46, p.y + 8}, theme::u32(theme::text), t.label);
    ImGui::PopFont();
    ImGui::PushFont(nullptr, theme::sizeSmall);
    draw->AddText({p.x + 46, p.y + 28}, theme::u32(ready ? (isHost ? theme::success : theme::textDim) : theme::warning),
                  isHost ? ICON_CHECK " This computer" : ready ? ICON_CHECK " Player found" : ICON_WARNING " Needs a player build");
    ImGui::PopFont();
  }
  const Target& target = kTargets[_target];
  const bool isHost = _target == kHostTarget;
  const std::optional<fs::path> player = isHost ? std::nullopt : findPlayer(target, _server);
  const bool ready = isHost || player;
  if (!ready) {
    ImGui::Dummy({0, 4});
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 536);
    ImGui::TextColored(theme::warning, ICON_INFO "  Exporting for %s needs the %s built on that platform. "
                                       "Download it from a release and put %s in players/%s/ beside jm.",
                       target.label, _server ? "server" : "engine", exeFor(target, _server).c_str(), target.id);
    ImGui::PopTextWrapPos();
  }

  ImGui::Dummy({0, 10});
  ui::sectionLabel("Output");
  if (ui::beginProperties("export", 120)) {
    ui::propertyRow("Folder", "Inside the project unless absolute");
    const float browse = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browse - 4);
    ImGui::InputText("##out", &_out);
    ImGui::SameLine(0, 4);
    if (ui::iconButton("browse", ICON_FOLDER_OPEN, "Choose a folder")) {
      const FolderPick pick = pickFolder(project.root());
      if (!pick.path.empty()) _out = reinterpret_cast<const char*>(pick.path.u8string().c_str());
    }
    if (std::string(target.id).starts_with("darwin") && !_server) {
      ui::propertyRow("Format", "An .app opens with a double-click; the bare binary runs from a terminal");
      bool app = !_bare;
      if (ui::toggle("##app", &app)) _bare = !app;
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ui::dimText(app ? "Application bundle (.app)" : "Single executable file");
    }
    ui::endProperties();
  }
  // What goes in.
  ImGui::Dummy({0, 6});
  const auto scenes = project.scenes();
  size_t assets = 0;
  for (const AssetFile& f : project.files()) assets += f.kind != AssetKind::Folder;
  char summary[160];
  std::snprintf(summary, sizeof(summary), ICON_GAME_CONTROLLER "  %s %s   ·   %zu scenes   ·   %zu files",
                project.name().c_str(), field(project.manifest(), "version", std::string()).c_str(), scenes.size(), assets);
  ui::smallText(summary, theme::textDim);

  if (dialogButtons(ICON_PACKAGE "  Export", ready)) {
    std::vector<std::string> args = {"--target", target.id};
    if (_bare && !_server) args.push_back("--bare");
    if (_server) args.push_back("--server");
    if (player) {
      args.push_back("--player");
      args.push_back(player->string());
    }
    editor.exportGame(args, _out);
  }
  ImGui::EndPopup();
  ImGui::PopStyleVar();
}

void SessionDialog::draw(Editor& editor) {
  if (_open) {
    ImGui::OpenPopup("Play with Players");
    _open = false;
  }
  ui::centerNextWindow({520, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22, 20});
  if (!ImGui::BeginPopupModal("Play with Players", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                                ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PopStyleVar();
    return;
  }
  Project& project = *editor.project();
  const Json net = field(project.manifest(), "net", Json::object());
  const bool p2p = field(net, "topology", std::string("server")) == "p2p";
  const bool server = !p2p || (net.is_object() && net.contains("server"));
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle + 2);
  ImGui::TextUnformatted(ICON_USERS "  Play with Players");
  ImGui::PopFont();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 476);
  if (!net.is_object() || net.empty()) {
    ui::dimText("This game has no multiplayer settings yet (Project Settings > Multiplayer). Each window plays alone until it hosts or joins.");
  } else if (server) {
    ui::dimText("Starts the game's server and a window per player, each joining it.");
  } else {
    ui::dimText("Opens a window per player: the first hosts, the others join it and each other.");
  }
  ImGui::PopTextWrapPos();
  ImGui::Dummy({0, 10});
  if (ui::beginProperties("session", 150)) {
    ui::propertyRow("Players", nullptr);
    ImGui::SliderInt("##players", &_players, 1, 8);
    ui::propertyRow("Latency", "Each message held this long, both ways: how it feels far apart");
    ImGui::SliderInt("##latency", &_latency, 0, 400, "%d ms");
    ui::propertyRow("Loss", "Position updates dropped on the way");
    ImGui::SliderInt("##loss", &_loss, 0, 50, "%d%%");
    ui::endProperties();
  }
  if (editor.sessionRunning()) {
    ImGui::Dummy({0, 4});
    ImGui::TextColored(theme::warning, ICON_INFO "  A session is running: starting another stops it.");
  }
  if (dialogButtons(ICON_PLAY "  Play", true)) {
    if (editor.sessionRunning()) editor.stopSession();
    editor.playSession(_players, _latency, static_cast<float>(_loss) / 100.0f);
  }
  ImGui::EndPopup();
  ImGui::PopStyleVar();
}

void SettingsDialog::draw(Editor& editor) {
  Project* project = editor.project();
  if (_open) {
    ImGui::OpenPopup("Project Settings");
    _open = false;
    _draft = project->manifest();
    _draftRoot = project->root();
  }
  ui::centerNextWindow({780, 600});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  const bool shown = ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
  ImGui::PopStyleVar();
  if (!shown) return;
  // Ctrl+O works over the dialog; the draft must never be saved into another project.
  if (!project || project->root() != _draftRoot) {
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }

  // Section list.
  struct Section {
    const char* icon;
    const char* label;
  };
  static constexpr Section kSections[] = {{ICON_INFO, "General"}, {ICON_STACK, "Content"}, {ICON_MONITOR, "Display"}, {ICON_TEXT_AA, "Interface"},
                                          {ICON_PACKAGE, "Export"}, {ICON_USERS, "Multiplayer"}};
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg1);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 18});
  ImGui::BeginChild("##sections", {180, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted("  Settings");
  ImGui::PopFont();
  ImGui::Dummy({0, 8});
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0.0f, 0.5f});
  for (int i = 0; i < static_cast<int>(std::size(kSections)); ++i) {
    const std::string item = std::string(kSections[i].icon) + "  " + kSections[i].label;
    if (ImGui::Selectable(item.c_str(), _section == i, 0, {0, 30})) _section = i;
  }
  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::SameLine(0, 0);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22, 18});
  ImGui::BeginChild("##form", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  auto object = [](Json& parent, const char* key) -> Json& {
    Json& o = parent[key];
    if (!o.is_object()) o = Json::object();
    return o;
  };
  Json& config = object(_draft, "config");
  auto text = [](Json& o, const char* key, const char* label, const char* hint) {
    ui::propertyRow(label, hint);
    std::string v = field(o, key, std::string());
    if (ImGui::InputText((std::string("##") + key).c_str(), &v)) o[key] = v;
  };
  auto integer = [](Json& o, const char* key, const char* label, int fallback, const char* hint) {
    ui::propertyRow(label, hint);
    int v = field(o, key, fallback);
    if (ImGui::InputInt((std::string("##") + key).c_str(), &v, 0)) o[key] = std::max(0, v);
  };
  auto boolean = [](Json& o, const char* key, const char* label, bool fallback, const char* hint) {
    ui::propertyRow(label, hint);
    bool v = field(o, key, fallback);
    if (ui::toggle((std::string("##") + key).c_str(), &v)) o[key] = v;
  };
  auto color = [](Json& o, const char* key, const char* label, const char* hint) {
    ui::propertyRow(label, hint);
    float c[4] = {0, 0, 0, 1};
    const auto v = field(o, key, std::vector<float>{0, 0, 0, 1});
    for (size_t i = 0; i < 4 && i < v.size(); ++i) c[i] = v[i];
    // A swatch and a hex code, as designers read colors; the manifest keeps 0..1 floats.
    if (ui::colorField(key, c)) {
      auto round = [](float v) { return std::round(v * 1000.0f) / 1000.0f; };
      o[key] = {round(c[0]), round(c[1]), round(c[2]), round(c[3])};
    }
  };

  const float formHeight = ImGui::GetContentRegionAvail().y - 64;
  ImGui::BeginChild("##fields", {0, formHeight});
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(kSections[_section].label);
  ImGui::PopFont();
  ImGui::Dummy({0, 8});
  if (_section == 1) {
    contentSection(*project);
  } else if (_section == 5) {
    multiplayerSection(*project);
  } else if (ui::beginProperties("settings", 170)) {
    switch (_section) {
      case 0: {
        text(_draft, "name", "Name", "The game's name: window title, save folder, exported file");
        text(_draft, "version", "Version", nullptr);
        ui::propertyRow("First scene", "Where the game starts");
        const std::string entry = field(_draft, "entryScene", std::string());
        if (ui::beginCombo("##entry", entry.c_str())) {
          for (const std::string& s : project->scenes()) {
            if (ImGui::Selectable(s.c_str(), s == entry)) _draft["entryScene"] = s;
          }
          ImGui::EndCombo();
        }
        break;
      }
      case 2: {
        Json& window = object(config, "window");
        Json& renderer = object(config, "renderer");
        integer(window, "width", "Window width", 1280, "Points");
        integer(window, "height", "Window height", 720, "Points");
        boolean(window, "resizable", "Resizable", true, nullptr);
        boolean(window, "fullscreen", "Start fullscreen", false, nullptr);
        boolean(window, "vsync", "VSync", true, "Match the display's refresh rate");
        boolean(window, "hideCursor", "Hide cursor", false, nullptr);
        integer(renderer, "logicalWidth", "Game width", 0, "The resolution the game draws at; 0 = the window's");
        integer(renderer, "logicalHeight", "Game height", 0, nullptr);
        color(renderer, "clearColor", "Background", "Behind everything in the game");
        color(renderer, "letterboxColor", "Letterbox", "The bars when the window's shape differs from the game's");
        break;
      }
      case 3:
        fileChoice(*project, object(config, "ui"), "defaultFont", "Default font", "Text in UI documents; none = the built-in pixel font",
                   AssetKind::Font, "Built-in pixel font");
        break;
      case 4: {
        Json& exportConfig = object(config, "export");
        fileChoice(*project, exportConfig, "icon", "App icon", "A PNG in the project (macOS)", AssetKind::Image, "None");
        text(exportConfig, "bundleId", "Bundle ID", "com.studio.game (macOS)");
        break;
      }
    }
    ui::endProperties();
  }
  ImGui::EndChild();
  Json changed = _draft, saved = project->manifest();
  pruneEmptyObjects(changed);
  pruneEmptyObjects(saved);
  if (dialogButtons("Save", changed != saved)) {
    project->manifest() = changed;
    std::string error;
    if (project->saveManifest(error)) {
      editor.toasts().show(Toasts::Kind::Success, "Project settings saved", "The project rebuilds with them now.");
    } else {
      editor.toasts().show(Toasts::Kind::Error, "Couldn't save settings", error);
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::EndPopup();
}

void SettingsDialog::fileChoice(const Project& project, Json& object, const char* key, const char* label, const char* hint,
                                AssetKind kind, const char* none) {
  ui::propertyRow(label, hint);
  const std::string current = field(object, key, std::string());
  if (ui::beginCombo((std::string("##") + key).c_str(), current.empty() ? none : current.c_str())) {
    if (ImGui::Selectable(none, current.empty())) object.erase(key);
    for (const AssetFile& f : project.files()) {
      if (f.kind == kind && ImGui::Selectable(f.path.c_str(), f.path == current)) object[key] = f.path;
    }
    ImGui::EndCombo();
  }
}

// .jm.json's "net" (docs/networking.md). Nothing is written until a field is
// changed, so a single-player game stays one.
void SettingsDialog::multiplayerSection(const Project& project) {
  Json& net = _draft["net"];
  if (!net.is_object()) net = Json::object();
  auto choice = [](Json& o, const char* key, const char* label, const char* hint, std::vector<std::pair<const char*, const char*>> options) {
    ui::propertyRow(label, hint);
    const std::string current = field(o, key, std::string(options[0].first));
    const char* shown = options[0].second;
    for (const auto& [value, text] : options) {
      if (current == value) shown = text;
    }
    if (ui::beginCombo((std::string("##") + key).c_str(), shown)) {
      for (const auto& [value, text] : options) {
        if (ImGui::Selectable(text, current == value)) o[key] = value;
      }
      ImGui::EndCombo();
    }
  };
  auto number = [](Json& o, const char* key, const char* label, int fallback, int low, int high, const char* hint) {
    ui::propertyRow(label, hint);
    int v = field(o, key, fallback);
    if (ImGui::InputInt((std::string("##") + key).c_str(), &v, 0)) o[key] = std::clamp(v, low, high);
  };
  auto sceneChoice = [&](Json& o, const char* key, const char* label, const char* hint, const char* none) {
    ui::propertyRow(label, hint);
    const std::string current = field(o, key, std::string());
    if (ui::beginCombo((std::string("##") + key).c_str(), current.empty() ? none : current.c_str())) {
      if (ImGui::Selectable(none, current.empty())) o.erase(key);
      for (const std::string& scene : project.scenes()) {
        if (ImGui::Selectable(scene.c_str(), scene == current)) o[key] = scene;
      }
      ImGui::EndCombo();
    }
  };

  if (!ui::beginProperties("net", 170)) return;
  choice(net, "topology", "Topology", "How players connect: through a host or server, or also to each other",
         {{"server", "Client / server"}, {"p2p", "Peer to peer"}});
  number(net, "port", "Port", 7777, 1, 65535, "The UDP port a host listens on");
  number(net, "maxPlayers", "Max players", 8, 1, 250, nullptr);
  ui::propertyRow("Player prefab", "Spawned for each player in scenes with entities named \"spawn\"");
  {
    const std::string current = field(net, "playerPrefab", std::string());
    if (ui::beginCombo("##playerPrefab", current.empty() ? "None" : current.c_str())) {
      if (ImGui::Selectable("None", current.empty())) net.erase("playerPrefab");
      for (const AssetFile& f : project.files()) {
        if (f.kind == AssetKind::Prefab && ImGui::Selectable(f.path.c_str(), f.path == current)) net["playerPrefab"] = f.path;
      }
      ImGui::EndCombo();
    }
  }
  number(net, "sendRate", "Updates a second", 30, 1, 120, "How often each machine sends what it simulates");
  ui::endProperties();

  ImGui::Dummy({0, 10});
  ui::sectionLabel("Dedicated server");
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
  ui::dimText("journeyman_server runs these same files without a window, sound or UI. These apply only there.");
  ImGui::PopTextWrapPos();
  if (ui::beginProperties("server", 170)) {
    Json& server = net["server"];
    if (!server.is_object()) server = Json::object();
    sceneChoice(server, "entryScene", "First scene", "Where the server starts; none = the game's first scene", "The game's");
    ui::propertyRow("Server scripts", "Scripts only the server runs, outside every scene (rules, bots, matchmaking)");
    {
      Json scripts = field(server, "scripts", Json::array());
      if (!scripts.is_array()) scripts = Json::array();
      std::optional<size_t> drop;
      for (size_t i = 0; i < scripts.size(); ++i) {
        if (!scripts[i].is_string()) continue;
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(scripts[i].get<std::string>().c_str());
        ImGui::SameLine();
        if (ui::iconButton("remove", ICON_X, "Remove")) drop = i;
        ImGui::PopID();
      }
      if (ui::beginCombo("##addScript", "Add a script...")) {
        for (const AssetFile& f : project.files()) {
          if (f.kind == AssetKind::Script && ImGui::Selectable(f.path.c_str())) scripts.push_back(f.path);
        }
        ImGui::EndCombo();
      }
      if (drop) scripts.erase(*drop);
      if (scripts != field(server, "scripts", Json::array())) {
        if (scripts.empty()) server.erase("scripts");
        else server["scripts"] = scripts;
      }
    }
    choice(server, "topology", "Server topology", "A matchmaker for p2p games still serves client / server",
           {{"", "As the game's"}, {"server", "Client / server"}, {"p2p", "Peer to peer"}});
    if (field(server, "topology", std::string()).empty()) server.erase("topology");
    number(server, "port", "Server port", field(net, "port", 7777), 1, 65535, nullptr);
    if (field(server, "port", 0) == field(net, "port", 7777)) server.erase("port");
    bool share = field(server, "shareScene", field(net, "shareScene", true));
    ui::propertyRow("Players follow its scene", "Off for a server that isn't a game world (a matchmaker)");
    if (ui::toggle("##shareScene", &share)) server["shareScene"] = share;
    ui::endProperties();
  }
}

void SettingsDialog::contentSection(const Project& project) {
  // Scenes, in the order the manifest lists them; the first scene is marked.
  ui::sectionLabel("Scenes the game can load");
  // Edits go to copies, written back when they differ: just showing a missing list isn't a change.
  const auto list = [&](const char* key) { Json v = field(_draft, key, Json()); return v.is_array() ? v : Json::array(); };
  Json scenes = list("scenes"), assets = list("assets");
  const std::string entry = field(_draft, "entryScene", std::string());
  std::optional<std::pair<size_t, int>> move;
  std::optional<size_t> drop;
  for (size_t i = 0; i < scenes.size(); ++i) {
    if (!scenes[i].is_string()) continue;
    const std::string s = scenes[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(s == entry ? theme::accent : theme::textFaint, s == entry ? ICON_FLAG : ICON_FILM_SLATE);
    if (s == entry) ui::tooltip("The first scene");
    ImGui::SameLine(0, 8);
    ImGui::TextColored(project.file(s) ? theme::text : theme::error, "%s", s.c_str());
    if (!project.file(s)) ui::tooltip("Missing: the file isn't in the project");
    const float buttons = 3 * (ImGui::GetFrameHeight() + 2);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - buttons);
    ImGui::BeginDisabled(i == 0);
    if (ui::iconButton("up", ICON_ARROW_UP, "Move up")) move = {i, -1};
    ImGui::EndDisabled();
    ImGui::SameLine(0, 2);
    ImGui::BeginDisabled(i + 1 == scenes.size());
    if (ui::iconButton("down", ICON_ARROW_DOWN, "Move down")) move = {i, 1};
    ImGui::EndDisabled();
    ImGui::SameLine(0, 2);
    if (ui::iconButton("remove", ICON_X, "Leave it out of the game")) drop = i;
    ImGui::PopID();
  }
  if (move) std::swap(scenes[move->first], scenes[move->first + move->second]);
  if (drop) scenes.erase(*drop);
  for (const std::string& s : project.scenes()) {
    if (manifestTakes(_draft, s)) continue;
    ImGui::PushID(s.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::textFaint, ICON_FILM_SLATE "  %s", s.c_str());
    ImGui::SameLine();
    ImGui::TextColored(theme::warning, "not in the game");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 60);
    if (ui::button("Add", {60, 0})) scenes.push_back(s);
    ImGui::PopID();
  }

  // Asset entries: paths or globs ("assets/sounds/*.wav"), each with what it takes now.
  ImGui::Dummy({0, 8});
  ui::sectionLabel("Files the game ships with");
  std::optional<size_t> dropAsset;
  for (size_t i = 0; i < assets.size(); ++i) {
    if (!assets[i].is_string()) continue;
    const std::string pattern = assets[i];
    const auto matches = static_cast<size_t>(std::count_if(project.files().begin(), project.files().end(), [&](const AssetFile& f) {
      return f.kind != AssetKind::Folder && manifestEntryMatches(pattern, f.path);
    }));
    ImGui::PushID(static_cast<int>(i));
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(theme::fonts().mono, theme::sizeSmall + 0.5f);
    ImGui::TextUnformatted(pattern.c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, 10);
    ImGui::TextColored(matches ? theme::textFaint : theme::warning, matches == 1 ? "1 file" : "%zu files", matches);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    if (ui::iconButton("remove", ICON_X, "Remove the entry")) dropAsset = i;
    ImGui::PopID();
  }
  if (dropAsset) assets.erase(*dropAsset);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
  const bool entered = ImGui::InputTextWithHint("##newEntry", "assets/sounds/*.wav  (* within a folder, ** across folders)", &_newEntry,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine(0, 4);
  if ((ui::button("Add", {66, 0}) || entered) && !_newEntry.empty()) {
    assets.push_back(_newEntry);
    _newEntry.clear();
  }

  // What nothing takes: files the game would fail to load if it asked for them.
  bool headed = false;
  for (const AssetFile& f : project.files()) {
    const AssetKind k = f.kind;
    if (k == AssetKind::Folder || k == AssetKind::Other || k == AssetKind::Script || k == AssetKind::Scene || k == AssetKind::Image) continue;
    if (manifestTakes(_draft, f.path)) continue;
    if (!std::exchange(headed, true)) {
      ImGui::Dummy({0, 8});
      ui::sectionLabel("Left out");
    }
    ImGui::PushID(f.path.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::warning, "%s", assetKindInfo(k).icon);
    ImGui::SameLine(0, 8);
    ImGui::TextUnformatted(f.path.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 60);
    if (ui::button("Add", {60, 0})) assets.push_back(f.path);
    ImGui::PopID();
  }
  if (scenes != list("scenes")) _draft["scenes"] = scenes;
  if (assets != list("assets")) _draft["assets"] = assets;
}
