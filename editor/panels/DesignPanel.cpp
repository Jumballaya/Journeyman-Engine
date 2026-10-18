// The Design panel: the game's design notes (design/) by group, a search,
// the facts kept in mind, and the selected note with its properties, text and
// world, as `jm memory` tells them (DesignNotes).

#include <sstream>

#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

const char* groupIcon(const std::string& group) {
  return group == "Design" ? ICON_FILE_TEXT : group == "Decisions" ? ICON_SEAL_CHECK : ICON_IDENTIFICATION_CARD;
}

std::string propertyText(const Json& value) {
  if (value.is_string()) return value.get<std::string>();
  if (!value.is_array()) return value.is_null() ? std::string() : value.dump();
  std::string out;
  for (const Json& v : value) out += (out.empty() ? "" : ", ") + propertyText(v);
  return out;
}

}  // namespace

void DesignPanel::flow(const DesignNotes& notes, const std::string& line, ImVec4 color) {
  struct Word {
    std::string text, link;  // link: the note it names ("" plain text)
    bool glued;              // no space before it
  };
  std::vector<Word> words;
  for (size_t i = 0, n = line.size(); i < n;) {
    const size_t start = line.find_first_not_of(' ', i);
    if (start == std::string::npos) break;
    const bool glued = start == i && !words.empty();
    if (line.compare(start, 2, "[[") == 0 && line.find("]]", start) != std::string::npos) {
      const size_t end = line.find("]]", start);
      const std::string inside = line.substr(start + 2, end - start - 2);
      const size_t bar = inside.find('|');
      words.push_back({bar == std::string::npos ? inside : inside.substr(bar + 1), inside.substr(0, bar), glued});
      i = end + 2;
    } else {
      size_t end = std::min(line.find(' ', start), line.find("[[", start + 1));
      if (end == std::string::npos) end = n;
      words.push_back({line.substr(start, end - start), {}, glued});
      i = end;
    }
  }
  const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
  const float space = ImGui::CalcTextSize(" ").x;
  for (size_t w = 0; w < words.size(); ++w) {
    const Word& word = words[w];
    const float width = ImGui::CalcTextSize(word.text.c_str()).x;
    if (w > 0) {
      const float gap = word.glued ? 0.0f : space;
      if (ImGui::GetItemRectMax().x + gap + width <= right) ImGui::SameLine(0, gap);
    }
    if (word.link.empty()) {
      ImGui::TextColored(color, "%s", word.text.c_str());
      continue;
    }
    const DesignNotes::Note* target = notes.resolve(word.link);
    ImGui::TextColored(target ? theme::accentBright : theme::textFaint, "%s", word.text.c_str());
    if (!target) {
      ui::tooltip("Not written yet");
    } else if (ImGui::IsItemHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddLine({min.x, max.y}, max, theme::u32(theme::accentBright));
      if (ImGui::IsItemClicked()) _selected = target->path;
    }
  }
}

void DesignPanel::draw(Editor& editor) {
  DesignNotes& notes = editor.design();
  if (notes.notes().empty()) {
    ui::emptyState(ICON_BOOK_OPEN, "No design notes yet",
                   "design/ holds what the game is and why: jm memory init makes it, and your agent fills it in as you plan.");
    return;
  }
  if (!ImGui::BeginTable("##design", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) return;
  ImGui::TableSetupColumn("notes", ImGuiTableColumnFlags_WidthFixed, 240.0f);
  ImGui::TableSetupColumn("note", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableNextColumn();
  ImGui::BeginChild("##list", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  drawList(notes);
  ImGui::EndChild();
  ImGui::TableNextColumn();
  ImGui::BeginChild("##note", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  drawNote(editor, notes);
  ImGui::EndChild();
  ImGui::EndTable();
  if (!_selected.empty()) notes.open(_selected);  // a link clicked this frame, or show()
}

void DesignPanel::drawList(DesignNotes& notes) {
  if (ui::searchField("##search", _query, "Search notes")) notes.search(_query);
  ImGui::Dummy({0, 2});
  auto row = [&](const std::string& path, const char* icon, const std::string& title) {
    ImGui::PushID(path.c_str());
    if (ImGui::Selectable((std::string(icon) + "  " + title).c_str(), path == _selected)) _selected = path;
    ImGui::PopID();
  };
  if (!_query.empty()) {
    if (notes.hits().empty()) ui::dimText("Nothing found");
    for (const auto& hit : notes.hits()) {
      const DesignNotes::Note* n = notes.find(hit.path);
      row(hit.path, groupIcon(n ? n->group : "Design"), hit.title);
      if (!hit.text.empty()) ui::smallText(ui::ellipsize(hit.text, ImGui::GetContentRegionAvail().x).c_str(), theme::textDim);
    }
    return;
  }
  std::string group;
  for (const DesignNotes::Note& n : notes.notes()) {
    if (n.group != group) ui::sectionLabel((group = n.group).c_str());
    row(n.path, groupIcon(n.group), n.title);
  }
  if (notes.facts().empty()) return;
  ImGui::Dummy({0, 6});
  ui::sectionLabel("Keep in mind");
  ImGui::PushTextWrapPos(0.0f);
  for (const std::string& fact : notes.facts()) {
    ImGui::Bullet();
    ImGui::TextColored(theme::textDim, "%s", fact.c_str());
  }
  ImGui::PopTextWrapPos();
}

void DesignPanel::drawNote(Editor& editor, const DesignNotes& notes) {
  const DesignNotes::Note* note = notes.find(_selected);
  if (!note) {
    ui::emptyState(ICON_NOTEBOOK, "Pick a note", "Its properties, its text and what it's connected to show here.");
    return;
  }
  ImGui::PushFont(theme::fonts().semibold, 18.0f);
  ImGui::TextUnformatted(note->title.c_str());
  ImGui::PopFont();
  if (!note->type.empty()) {
    ImGui::SameLine(0, 10);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
    ui::badge(note->type.c_str(), note->dossier() ? theme::info : theme::accent);
  }
  ui::smallText(note->path.c_str(), theme::textFaint);
  const auto& opened = notes.opened();
  const bool loaded = opened && opened->path == note->path;
  ImGui::BeginDisabled(!loaded || opened->obsidian.empty());
  if (ui::button(ICON_ARROW_SQUARE_OUT "  Open in Obsidian")) editor.openUrl(opened->obsidian);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ui::button(ICON_CODE "  Open in Code Editor")) editor.openInCodeEditor(note->path);
  ImGui::SameLine();
  if (ui::button(ICON_CHAT_CIRCLE_TEXT "  Ask about this")) {
    editor.promptText("Ask Your Agent", "What about " + note->title + "?",
                      [&editor, path = note->path](const std::string& question) { editor.askAbout(path, question); });
  }

  if (!note->props.empty() && ui::beginProperties("##props", 110.0f)) {
    for (const auto& [key, value] : note->props.items()) {
      if (key == "tags") continue;  // its type, again
      ui::propertyRow(key.c_str());
      const std::string text = propertyText(value);
      if (text.empty()) ui::dimText("-");
      else flow(notes, text, theme::text);
    }
    ui::endProperties();
  }
  if (!loaded) {
    ui::spinner(7, theme::u32(theme::textDim));
    return;
  }

  // Its text: headings, list items and paragraphs. The # heading is the title, above;
  // a ## section with nothing under it (a template's, not filled in yet) is left out.
  std::vector<std::string> lines;
  std::istringstream in(opened->text);
  for (std::string line; std::getline(in, line);) lines.push_back(line.ends_with('\r') ? line.substr(0, line.size() - 1) : line);
  ImGui::Dummy({0, 4});
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    if (line.starts_with("# ")) continue;
    if (line.starts_with("#")) {
      const size_t text = line.find_first_not_of("# ");
      size_t next = i + 1;
      while (next < lines.size() && lines[next].find_first_not_of(" \t") == std::string::npos) ++next;
      if (text == std::string::npos || next == lines.size() || lines[next].starts_with("#")) continue;
      ui::sectionLabel(line.substr(text).c_str());
    } else if (line.starts_with("- ") || line.starts_with("* ")) {
      // Its wrapped lines stay under its text, not under the bullet.
      const float indent = ImGui::GetFrameHeight();
      ImGui::Indent(indent);
      const ImVec2 at = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddCircleFilled({at.x - indent * 0.5f, at.y + ImGui::GetTextLineHeight() * 0.5f}, 2.0f,
                                                  theme::u32(theme::textDim));
      flow(notes, line.substr(2), theme::text);
      ImGui::Unindent(indent);
    } else if (line.find_first_not_of(" \t") == std::string::npos) {
      ImGui::Dummy({0, 2});
    } else {
      flow(notes, line, theme::text);
    }
  }

  ImGui::Dummy({0, 6});
  ui::sectionLabel("World");
  if (opened->world.empty()) ui::dimText("Not connected to anything yet.");
  auto end = [&](const std::string& title, const std::string& path) {
    ImGui::TextColored(path.empty() ? theme::textFaint : theme::accentBright, "%s", title.c_str());
    if (path.empty()) {
      ui::tooltip("Not written yet");
    } else if (ImGui::IsItemHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      if (ImGui::IsItemClicked()) _selected = path;
    }
  };
  for (const DesignNotes::Edge& e : opened->world) {
    ImGui::PushID(&e);
    end(e.from, e.fromPath);
    ImGui::SameLine();
    ImGui::TextColored(theme::textDim, "%s", e.type.c_str());
    ImGui::SameLine();
    end(e.to, e.toPath);
    if (!e.details.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(theme::textFaint, "(%s)", e.details.c_str());
    }
    ImGui::PopID();
  }
}
