// Temporary: panels not written yet.
#include "Panels.hpp"
#include "Ui.hpp"
#include "Icons.hpp"

void HierarchyPanel::draw(Editor&) { ui::emptyState(ICON_TREE_STRUCTURE, "Hierarchy", "Coming next."); }
void InspectorPanel::draw(Editor&) { ui::emptyState(ICON_SLIDERS_HORIZONTAL, "Inspector", "Coming next."); }
void ScenePanel::draw(Editor&, float) { ui::emptyState(ICON_FILM_SLATE, "Scene", "Coming next."); }
void ScenePanel::frameSelection(Editor&) {}
void ScenePanel::frameAll(Editor&) {}
void ScenePanel::setZoom(float z) { _zoom = z; }
void GamePanel::draw(Editor&, float) { ui::emptyState(ICON_GAME_CONTROLLER, "Game", "Coming next."); }
void AssetsPanel::draw(Editor&) { ui::emptyState(ICON_FOLDER_SIMPLE, "Assets", "Coming next."); }
void AssetsPanel::reveal(const std::string&) {}
void ConsolePanel::draw(Editor&) { ui::emptyState(ICON_TERMINAL, "Console", "Coming next."); }
void CommandPalette::open(const std::string&) { _open = true; }
void CommandPalette::draw(Editor&) {}
void ExportDialog::draw(Editor&) {}
void SettingsDialog::draw(Editor&) {}
