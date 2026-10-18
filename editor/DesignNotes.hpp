#pragma once

#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include "Project.hpp"

// The game's design notes (design/), as `jm memory` tells them: the notes,
// the facts kept in mind, one note opened with its world, and a search. jm
// runs on threads of its own, one ask of each kind at a time (the newest wins);
// poll() takes the answers on the UI thread and asks for the list again when
// design/ (or the person's ~/.jm/memory.md) changes on disk (checked every 2 s, without jm).
class DesignNotes {
 public:
  struct Note {
    std::string title, path, type;
    std::string group;    // "Design", a dossier's folder ("Characters"), or "Decisions"
    std::string summary;  // the first paragraph under its heading
    Json props = Json::object();
    bool dossier() const { return group != "Design" && group != "Decisions"; }
  };
  struct Edge {
    std::string from, type, to, fromPath, toPath, details;  // toPath "": not written yet
  };
  struct Opened {
    std::string path, text, obsidian;  // text: the body, without its frontmatter
    std::vector<Edge> world;
  };
  struct Hit {
    std::string title, path, text;
  };

  void setFolder(const std::filesystem::path& root);  // the open game ("" none)
  void poll(double now);
  // By group, in the panel's order: Design, the dossier folders, Decisions.
  const std::vector<Note>& notes() const { return _notes; }
  void setNotes(std::vector<Note> notes) { _notes = std::move(notes); }  // poll() does, from jm
  const std::vector<std::string>& facts() const { return _facts; }
  const Note* find(const std::string& path) const;
  // A [[link]]'s note: by title, file name or path in design/ (any case; a #heading in it ignored).
  const Note* resolve(const std::string& link) const;
  void open(const std::string& path);
  const std::optional<Opened>& opened() const { return _opened; }
  void search(const std::string& query);  // "" clears it
  const std::vector<Hit>& hits() const { return _hits; }
  // The dossier about an entity: titled as its name or its prefab, else one whose
  // prefab or scene property names its prefab or scene file.
  const Note* dossierFor(const std::string& name, const std::string& prefab, const std::string& scene) const;

  // jm's JSON answers (tests feed them directly). `root` reads each note's summary.
  static std::vector<Note> parseNotes(const Json& list, const std::filesystem::path& root);
  static Opened parseOpened(const Json& show, const Json& world);
  // The JSON object in jm's output (stderr lines may come before it).
  static Json parseOutput(const std::string& output);

 private:
  struct Ask {
    std::future<Json> answer;
    std::filesystem::path root;  // another game's answer is dropped
    std::string note;            // the note asked about, if any
    bool valid() const { return answer.valid(); }
  };
  // Runs `jms` (argument lists) in the game folder; the answer is an array of their JSON.
  Ask ask(std::vector<std::vector<std::string>> jms, bool summaries = false);
  // The answer if it's here and still this game's.
  std::optional<Json> take(Ask& ask);
  long long designStamp() const;

  std::filesystem::path _root;
  std::vector<Note> _notes;
  std::vector<std::string> _facts;
  std::optional<Opened> _opened;
  std::vector<Hit> _hits;
  Ask _listing, _opening, _searching;
  std::optional<std::string> _wantOpen, _wantSearch;
  std::string _query;  // asked again in another game
  bool _wantList = false;
  long long _stamp = -1;
  double _checked = -100;
};
