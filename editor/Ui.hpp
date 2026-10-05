#pragma once

#include <functional>
#include <string>

#include <imgui.h>

// The editor's own widgets, built on ImGui and the theme. Panels use these
// instead of raw ImGui so every control looks and behaves the same.
namespace ui {

// A delayed tooltip for the last item, with an optional shortcut chip.
void tooltip(const char* text, ImGuiKeyChord shortcut = 0);

// A square, frameless icon button; `active` draws it selected (a toggled tool).
bool iconButton(const char* id, const char* icon, const char* tip = nullptr, bool active = false,
                ImGuiKeyChord shortcut = 0, float size = 0.0f);
// Text buttons: primary (accent fill) for the main action, plain otherwise.
bool primaryButton(const char* label, ImVec2 size = {});
bool button(const char* label, ImVec2 size = {});
bool dangerButton(const char* label, ImVec2 size = {});

// A rounded search box with a magnifier and a clear (x) button. True when changed.
bool searchField(const char* id, std::string& text, const char* hint, float width = -1.0f);

// An on/off switch.
bool toggle(const char* id, bool* value);

// A centered, quiet message for an empty panel, with an optional action.
// Returns true when the action is clicked.
bool emptyState(const char* icon, const char* title, const char* body, const char* action = nullptr);

// A small rotating arc (work in progress).
void spinner(float radius, ImU32 color);

// Text in the semibold face / dim color / small size.
void heading(const char* text);
void dimText(const char* text);
void smallText(const char* text, ImVec4 color);

// A small label with a hairline after it, grouping what follows; the line runs
// to `width` (default: the rest of the row).
void sectionLabel(const char* text, float width = 0.0f);

// `text` cut to fit `width` in the current font, ending in "…" when cut.
std::string ellipsize(const std::string& text, float width);

// A path for display: the home folder shortened to "~".
std::string displayPath(const std::string& path);

// Property grid: a label column and a value column, like an inspector.
// Rows between begin/end; `propertyRow` draws the label and leaves the cursor
// in the value cell, sized to fill it. `modified` marks a value that differs
// from its default (an accent bar in the gutter, Unity's override marker).
bool beginProperties(const char* id, float labelWidth = 0.0f);
void propertyRow(const char* label, const char* hint = nullptr, bool modified = false);
void endProperties();

// Drag fields for 1-4 floats with colored axis letters; returns true while editing.
bool dragVector(const char* id, float* values, int count, float speed = 0.5f, const char* format = "%.2f");

// A collapsible header for a component in the inspector: icon, title, and a
// "..." button that opens `menu`. Returns whether it is open.
bool componentHeader(const char* id, const char* icon, const char* title, const std::function<void()>& menu,
                     bool defaultOpen = true);

// A dropdown with the theme's caret instead of ImGui's arrow box; pair with ImGui::EndCombo.
bool beginCombo(const char* id, const char* preview);

// Small rounded label (asset type, status).
void badge(const char* text, ImVec4 color);

// Fuzzy subsequence match; higher is better, negative = no match.
int fuzzyScore(std::string_view text, std::string_view query);
// Draws `text` with the characters matching `query` highlighted.
void fuzzyText(std::string_view text, std::string_view query, ImU32 color, ImU32 highlight);

// The top bar of an asset tab: icon, title and a faint subtitle on the left.
// Returns the x where right-aligned actions should end; draw them with
// ImGui::SameLine(rightEdge - theirWidth). Call endDocumentBar() after them.
float beginDocumentBar(const char* icon, const char* title, const char* subtitle);
void endDocumentBar();

// A small rounded token (a key binding, a tag). With `removable`, an x shows
// on hover; `removed` is set when it is clicked. True when the chip is clicked.
bool chip(const char* id, const char* label, bool removable = false, bool* removed = nullptr, bool keycap = false);

// Centers the next window on the main viewport (popups, dialogs).
void centerNextWindow(ImVec2 size);

}  // namespace ui
