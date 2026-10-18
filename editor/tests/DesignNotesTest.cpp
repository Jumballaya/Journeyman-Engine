#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "DesignNotes.hpp"

namespace fs = std::filesystem;

namespace {

Json note(const std::string& title, const std::string& path, const std::string& type, Json props = Json::object()) {
  return {{"title", title}, {"path", path}, {"type", type}, {"props", props}};
}

}  // namespace

TEST(DesignNotes, GroupsNotesAndReadsTheirSummaries) {
  const fs::path root = fs::temp_directory_path() / "jm-design-notes-test";
  fs::create_directories(root / "design/Characters");
  std::ofstream(root / "design/Characters/Mara.md") << "---\ntitle: Mara\nhome: \"[[Irongate]]\"\n---\n\n# Mara\n\nKeeps the\nforge lit.\n\n## History\n";
  const Json list = {{"notes",
                      {note("Use tiles", "design/decisions/tiles.md", "decision"), note("Mara", "design/Characters/Mara.md", "character"),
                       note("Notes", "design/Notes.md", "note"), note("Design", "design/Design.md", "index"), {{"title", "no path"}}}}};
  const auto notes = DesignNotes::parseNotes(list, root);
  ASSERT_EQ(notes.size(), 4u);
  EXPECT_EQ(notes[0].title, "Design");
  EXPECT_EQ(notes[1].title, "Notes");
  EXPECT_EQ(notes[2].group, "Characters");
  EXPECT_TRUE(notes[2].dossier());
  EXPECT_EQ(notes[2].summary, "Keeps the forge lit.");
  EXPECT_EQ(notes[3].group, "Decisions");
  EXPECT_FALSE(notes[3].dossier());
  fs::remove_all(root);
}

TEST(DesignNotes, OpensANoteWithoutItsFrontmatterAndWithItsWorld) {
  const Json show = {{"note", {{"path", "design/Characters/Kael.md"}}}, {"text", "---\ntype: character\n---\n# Kael\n"}, {"obsidian", "obsidian://x"}};
  const Json world = {{"edges",
                       {{{"from", "Kael"}, {"type", "ally_of"}, {"to", "Mara"}, {"attrs", {{"since", "childhood"}}},
                         {"fromPath", "design/Characters/Kael.md"}}}}};
  const auto opened = DesignNotes::parseOpened(show, world);
  EXPECT_EQ(opened.path, "design/Characters/Kael.md");
  EXPECT_EQ(opened.text, "# Kael\n");
  ASSERT_EQ(opened.world.size(), 1u);
  EXPECT_EQ(opened.world[0].details, "since: childhood");
  EXPECT_EQ(opened.world[0].toPath, "");  // not written yet
}

TEST(DesignNotes, ReadsJsonAfterOtherOutput) {
  EXPECT_EQ(DesignNotes::parseOutput("warning: x\n{\"a\": 1}\n")["a"], 1);
  EXPECT_TRUE(DesignNotes::parseOutput("jm: no such note").is_null());
}

TEST(DesignNotes, FindsTheDossierAboutAnEntity) {
  const Json list = {{"notes",
                      {note("Pitch", "design/Pitch.md", "note"), note("Kael", "design/Characters/Kael.md", "character"),
                       note("Knight", "design/Characters/Knight.md", "character"),
                       note("Gate Guard", "design/Characters/Guard.md", "character", {{"prefab", "[[assets/prefabs/guard.prefab.json]]"}}),
                       note("Ashvale", "design/Locations/Ashvale.md", "location", {{"scene", "`scenes/ashvale.scene.json`"}})}}};
  DesignNotes notes;
  notes.setNotes(DesignNotes::parseNotes(list, {}));
  EXPECT_EQ(notes.dossierFor("kael", "", "")->title, "Kael");
  EXPECT_EQ(notes.dossierFor("Sir Rob", "assets/prefabs/knight.prefab.json", "")->title, "Knight");
  EXPECT_EQ(notes.dossierFor("Guard 2", "assets/prefabs/guard.prefab.json", "")->title, "Gate Guard");
  EXPECT_EQ(notes.dossierFor("Tree", "", "scenes/ashvale.scene.json")->title, "Ashvale");
  EXPECT_EQ(notes.dossierFor("Pitch", "", "scenes/other.scene.json"), nullptr);  // not a dossier
  EXPECT_EQ(notes.resolve("Characters/guard#History")->title, "Gate Guard");
  EXPECT_EQ(notes.resolve("knight.md")->title, "Knight");
  EXPECT_EQ(notes.resolve("Nobody"), nullptr);
  notes.setNotes(DesignNotes::parseNotes({{"notes", {note("The Doctor", "design/Characters/Dr.Smith.md", "character")}}}, {}));
  EXPECT_EQ(notes.resolve("Dr.Smith")->title, "The Doctor");
  EXPECT_EQ(notes.resolve("Dr"), nullptr);
}
