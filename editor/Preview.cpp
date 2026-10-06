#include "Preview.hpp"

#include <algorithm>
#include <cmath>

#include "LogBook.hpp"
#include "Project.hpp"
#include "physics2d/BoxColliderComponent.hpp"
#include "physics2d/TransformComponent.hpp"
#include "renderer2d/SpriteComponent.hpp"
#include "tilemap/TileMapComponent.hpp"

namespace {

constexpr float kPointRadius = 8.0f;  // pickable size of an entity with no visual

// Text has no component the editor can measure; estimate from its JSON.
std::optional<glm::vec2> textHalfSize(const Json& entry) {
  const Json components = entry.value("components", Json::object());
  auto it = components.find("TextComponent");
  if (it == components.end()) return std::nullopt;
  const std::string text = it->value("text", std::string());
  const float size = it->value("size", 8.0f);
  size_t longest = 0, current = 0, lines = 1;
  for (char c : text) {
    if (c == '\n') {
      ++lines;
      current = 0;
    } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
      longest = std::max(longest, ++current);
    }
  }
  return glm::vec2(std::max<size_t>(longest, 1) * size * 0.32f, lines * size * 0.6f);
}

}  // namespace

bool Preview::start(const Project& project) {
  stop();
  _engine = HostedEngine::createPreview(project.buildDir(), _error);
  if (!_engine) return false;
  _error.clear();
  _gameSize = _engine->gameSize();
  return true;
}

void Preview::stop() {
  _spawned.clear();
  _syncedPath.clear();
  _syncedRevision = ~0ull;
  _engine.reset();
}

void Preview::sync(const SceneDocument& doc, const std::function<Json(const Json&)>& resolve,
                   const std::function<bool(EntityUid)>& visible) {
  if (!_engine) return;
  // Visibility changes don't bump the revision: compare a checksum of who's visible.
  size_t shown = 0;
  for (size_t i = 0; i < doc.size(); ++i) {
    if (visible(doc.uid(i))) shown += static_cast<size_t>(doc.uid(i)) * 2654435761u;
  }
  if (doc.path() == _syncedPath && doc.revision() == _syncedRevision && shown == _syncedVisible) return;
  _syncedVisible = shown;
  SceneManager& scenes = _engine->engine().getSceneManager();
  if (doc.path() != _syncedPath) {
    scenes.unload();
    _spawned.clear();
  }

  std::map<EntityUid, Spawned> next;
  for (size_t i = 0; i < doc.size(); ++i) {
    const EntityUid uid = doc.uid(i);
    if (!visible(uid)) continue;
    Json json = resolve(doc.entity(i));
    if (auto it = _spawned.find(uid); it != _spawned.end()) {
      if (it->second.json == json) {
        next.insert(_spawned.extract(it));
        continue;
      }
      if (!it->second.failed) scenes.destroyEntity(it->second.id);
      _spawned.erase(it);
    }
    Spawned spawned{EntityId{}, json};
    try {
      spawned.id = scenes.spawn(nlohmann::json::parse(json.dump()));
    } catch (const std::exception& e) {
      spawned.failed = true;
      LogBook::instance().add(LogBook::Level::Error, LogBook::Source::Editor,
                              "'" + doc.displayName(i) + "' couldn't be built: " + e.what());
    }
    next.emplace(uid, std::move(spawned));
  }
  for (auto& [uid, gone] : _spawned) {
    if (!gone.failed) scenes.destroyEntity(gone.id);
  }
  _spawned = std::move(next);
  _syncedPath = doc.path();
  _syncedRevision = doc.revision();
}

unsigned Preview::render(glm::vec2 center, float zoom, int width, int height, float scale, bool showUi, float dt) {
  if (!_engine) return 0;
  Renderer2DModule::EditorView view;
  view.center = center;
  view.zoom = zoom;
  view.showUi = showUi;
  view.gameSize = _gameSize;
  view.logicalSize = {std::max(1, static_cast<int>(std::round(width / scale))),
                      std::max(1, static_cast<int>(std::round(height / scale)))};
  _engine->renderer().setEditorView(view);
  return _engine->frame(width, height, dt);
}

std::optional<Preview::Bounds> Preview::bounds(EntityUid uid) const {
  auto it = _spawned.find(uid);
  if (it == _spawned.end() || it->second.failed) return std::nullopt;
  World& world = _engine->engine().getWorld();
  const EntityId id = it->second.id;
  auto* transform = world.getComponent<TransformComponent>(id);
  if (!transform) return std::nullopt;

  Bounds b;
  b.position = glm::vec2(transform->position);
  b.z = transform->position.z;
  glm::vec2 half(kPointRadius);
  glm::vec2 center = b.position;
  float rotation = 0.0f;
  if (auto* map = world.getComponent<TileMapComponent>(id)) {
    const glm::vec2 size = glm::vec2(map->grid.width(), map->grid.height()) * map->grid.tileSize();
    half = size * 0.5f;
    center = b.position + half;
  } else if (world.getComponent<SpriteComponent>(id)) {
    half = glm::abs(transform->scale);
    rotation = transform->rotationRad;
  } else if (auto text = textHalfSize(it->second.json)) {
    half = *text;
  } else {
    b.point = true;
  }
  const glm::vec2 axisX(std::cos(rotation), std::sin(rotation)), axisY(-axisX.y, axisX.x);
  const glm::vec2 offsets[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
  b.min = glm::vec2(1e30f);
  b.max = glm::vec2(-1e30f);
  for (int i = 0; i < 4; ++i) {
    b.corners[i] = center + axisX * (offsets[i].x * half.x) + axisY * (offsets[i].y * half.y);
    b.min = glm::min(b.min, b.corners[i]);
    b.max = glm::max(b.max, b.corners[i]);
  }
  return b;
}

std::vector<Preview::Collider> Preview::colliders(EntityUid uid) const {
  const auto id = entityOf(uid);
  if (!id) return {};
  World& world = _engine->engine().getWorld();
  auto* transform = world.getComponent<TransformComponent>(*id);
  auto* box = world.getComponent<BoxColliderComponent>(*id);
  if (!transform || !box) return {};
  return {{glm::vec2(transform->position) + box->offset, box->halfExtents}};
}

std::vector<EntityUid> Preview::pick(glm::vec2 world) const {
  std::vector<std::pair<float, EntityUid>> hits;
  for (const auto& [uid, _] : _spawned) {
    auto b = bounds(uid);
    if (!b) continue;
    // Inside the rotated quad: same side of every edge.
    bool inside = true;
    for (int i = 0; i < 4 && inside; ++i) {
      const glm::vec2 e = b->corners[(i + 1) % 4] - b->corners[i], p = world - b->corners[i];
      inside = e.x * p.y - e.y * p.x >= 0.0f;
    }
    // Maps sit under everything; prefer what's drawn on them.
    if (inside) hits.emplace_back(tileGrid(uid) ? -1e9f : b->z, uid);
  }
  std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<EntityUid> out;
  for (const auto& [_, uid] : hits) out.push_back(uid);
  return out;
}

std::vector<EntityUid> Preview::pickRect(glm::vec2 a, glm::vec2 b) const {
  const glm::vec2 lo = glm::min(a, b), hi = glm::max(a, b);
  std::vector<EntityUid> out;
  for (const auto& [uid, _] : _spawned) {
    auto bb = bounds(uid);
    if (bb && !tileGrid(uid) && bb->max.x >= lo.x && bb->min.x <= hi.x && bb->max.y >= lo.y && bb->min.y <= hi.y) {
      out.push_back(uid);
    }
  }
  return out;
}

const TileGrid* Preview::tileGrid(EntityUid uid, glm::vec2* origin) const {
  const auto id = entityOf(uid);
  if (!id) return nullptr;
  World& world = _engine->engine().getWorld();
  auto* map = world.getComponent<TileMapComponent>(*id);
  auto* transform = world.getComponent<TransformComponent>(*id);
  if (!map || !transform) return nullptr;
  if (origin) *origin = glm::vec2(transform->position);
  return &map->grid;
}

bool Preview::failed(EntityUid uid) const {
  auto it = _spawned.find(uid);
  return it != _spawned.end() && it->second.failed;
}

std::optional<std::pair<glm::vec2, glm::vec2>> Preview::extent() const {
  std::optional<std::pair<glm::vec2, glm::vec2>> out;
  for (const auto& [uid, _] : _spawned) {
    auto b = bounds(uid);
    if (!b) continue;
    if (!out) out = std::pair{b->min, b->max};
    out->first = glm::min(out->first, b->min);
    out->second = glm::max(out->second, b->max);
  }
  return out;
}
