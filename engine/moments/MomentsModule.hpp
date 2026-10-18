#pragma once

#include <array>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "../core/app/EngineModule.hpp"
#include "../core/app/agent/AgentRun.hpp"
#include "../core/ecs/entity/EntityId.hpp"
#include "../ui/DebugDraw.hpp"
#include "NoteEdit.hpp"

struct GLFWwindow;
class Renderer2D;
class Renderer2DModule;
class UIModule;

// F8's moment on screen (AgentRun::hold; agent build only: journeyman_engine
// links this, the player doesn't). While the run is held: a click picks the
// entity under the pointer, or the point there when there's none; a drag
// picks the area it spans (whatever it starts on). Shift adds, or takes away
// what's clicked again (a point near it, an area on its edge). The picks are
// outlined and numbered over the frame, under a "Paused · F8" badge; Enter
// saves the moment, F8 or Esc goes on without, `.` steps a frame. Typing
// writes a note: the latest pick's, or the one clicked or Tabbed to (the
// moment's when none is); Enter keeps it (then Enter saves), Esc undoes it.
// The game's devices are muted then: this reads the window's itself.
class MomentsModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  // The window's keys and mouse, while held.
  void draw(Engine& app, float dt) override;
  // The driver's `pick x y [add]` (a click; add: shift-click) and
  // `area x0 y0 x1 y1 [add]` (a drag), in logical px; `note [n] <text>`
  // (pick n's note, the moment's for none or 0: as if typed).
  bool driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) override;
  const char* name() const override { return "MomentsModule"; }

  // The entities drawn under a point (logical px from the game's top-left),
  // topmost first: sprites, then colliders of entities without one. Tile maps never.
  std::vector<EntityId> under(glm::vec2 point) const;

 private:
  Engine* _app = nullptr;
  AgentRun* _run = nullptr;
  Renderer2DModule* _renderer = nullptr;
  UIModule* _ui = nullptr;
  DebugDraw _marks, _labels;  // the picks' outlines, then their names and notes over them, made each held frame
  bool _wasClicked = false;   // last frame's button
  bool _wasHeld = false;
  // Keys pressed (or repeated) and characters typed while held, in order, to
  // be read with the next held frame: a character has `ch`.
  struct Typed {
    int key = 0, mods = 0;
    bool repeat = false;
    char32_t ch = 0;
  };
  std::vector<Typed> _typed;
  void (*_passKey)(GLFWwindow*, int, int, int, int) = nullptr;  // the window's own callbacks
  void (*_passChar)(GLFWwindow*, unsigned int) = nullptr;
  bool _listening = false;
  NoteEdit _edit;
  size_t _focus = 0;  // whose note typing writes: pick n (1 the first), 0 the moment's
  std::optional<glm::vec2> _pressed;  // where the button went down (logical px), while it's down
  glm::vec2 _pointer{0.0f};

  using Pick = AgentRun::Pick;
  // A click at a point, and a drag from a to b (logical px): what they pick.
  void click(glm::vec2 at, bool add);
  void type(const Typed& typed);
  void focus(size_t n);  // keeps the note being typed, then typing writes pick n's
  static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void onChar(GLFWwindow* window, unsigned int ch);
  void drag(glm::vec2 a, glm::vec2 b, bool add);
  glm::vec2 toWorld(glm::vec2 point) const;  // logical px (y down) to the world (y up)
  // Where an entity is drawn in the world, as a quad (counter-clockwise): its
  // sprite, else its collider's box; null if neither.
  std::optional<std::array<glm::vec2, 4>> quadOf(EntityId id) const;
  // A pick's box on screen (logical px); null for an entity that isn't drawn.
  std::optional<std::array<glm::vec2, 2>> screenBox(const Pick& pick) const;
  // What a saved pick adds: AgentRun::setDescriber.
  nlohmann::json describe(const Pick& pick) const;
  nlohmann::json tilesIn(glm::vec2 lo, glm::vec2 hi) const;  // a world box's tile cells, by map and layer
  std::string nameOf(EntityId id) const;
  void paint(Renderer2D& renderer);  // the overlay pass
  // A label (the focused pick's number and name, then its note; the
  // moment's; the other picks' notes): on a dark tag beside the box lo-hi,
  // kept off what's `taken` if it can be (it's taken then too). The focused
  // one is underlined; a placeholder's note dim. Where it went.
  struct Label {
    glm::vec2 lo, hi;
    std::vector<std::string> lines;
    bool focused = false, placeholder = false;
    bool numbered = false;  // each line's first word is a pick's number (yellow), not a heading line
  };
  glm::vec4 label(Renderer2D& renderer, const Label& label, std::vector<glm::vec4>& taken);
  void tag(Renderer2D& renderer, glm::vec4 box, const std::string& text, bool lit);  // a number's
  // A note as label lines: wrapped to `width`, at most `most`; while it's
  // typed its end shows, with a caret.
  std::vector<std::string> noteLines(const std::string& note, bool typing, float width, size_t most);
};
