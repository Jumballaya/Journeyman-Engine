#include "DesignNotes.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "CliRunner.hpp"

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string text(const Json& j, const char* key) {
  return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

// The note's text after its frontmatter.
std::string body(const std::string& note) {
  if (!note.starts_with("---")) return note;
  const size_t end = note.find("\n---", 3);
  if (end == std::string::npos) return note;
  const size_t next = note.find('\n', end + 4);
  return next == std::string::npos ? std::string() : note.substr(next + 1);
}

// The first paragraph under the note's # heading, on one line.
std::string summaryOf(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream all;
  all << in.rdbuf();
  std::istringstream lines(body(all.str()));
  std::string summary;
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const bool blank = line.find_first_not_of(" \t") == std::string::npos;
    if (line.starts_with("# ") || (blank && summary.empty())) continue;
    if (blank || line.starts_with("#") || line.starts_with("- ")) break;
    summary += (summary.empty() ? "" : " ") + line;
  }
  return summary;
}

// "Design" for design/'s own notes, "Decisions" for design/decisions/, else the folder.
std::string groupOf(const std::string& path) {
  const std::string rest = path.starts_with("design/") ? path.substr(7) : path;
  const size_t slash = rest.find('/');
  if (slash == std::string::npos) return "Design";
  const std::string folder = rest.substr(0, slash);
  return lower(folder) == "decisions" ? "Decisions" : folder;
}

// A property naming a file, as written: "[[x]]", `x`, "./x" all name x.
std::string fileNamed(std::string value) {
  std::erase_if(value, [](char c) { return c == '[' || c == ']' || c == '`' || c == '"'; });
  if (value.starts_with("./")) value.erase(0, 2);
  return lower(value);
}

std::string stem(const std::string& path) {
  std::string name = fs::path(path).filename().string();
  return name.substr(0, name.find('.'));  // knight.prefab.json: knight
}

}  // namespace

Json DesignNotes::parseOutput(const std::string& output) {
  const size_t at = output.starts_with("{") ? 0 : output.find("\n{");
  if (at == std::string::npos) return Json();
  Json parsed = Json::parse(output.substr(at), nullptr, false);
  return parsed.is_discarded() ? Json() : parsed;
}

std::vector<DesignNotes::Note> DesignNotes::parseNotes(const Json& list, const fs::path& root) {
  std::vector<Note> notes;
  if (!list.is_object() || !list.contains("notes") || !list["notes"].is_array()) return notes;
  for (const Json& n : list["notes"]) {
    Note note{text(n, "title"), text(n, "path"), text(n, "type")};
    if (note.path.empty()) continue;
    note.group = groupOf(note.path);
    if (n.contains("props") && n["props"].is_object()) note.props = n["props"];
    if (!root.empty()) note.summary = summaryOf(root / note.path);
    notes.push_back(std::move(note));
  }
  // The scaffold's notes in their order, then the person's own; dossiers by folder; decisions last.
  static const std::vector<std::string> kScaffold = {"Design", "Pitch", "Pillars", "Levels", "Systems", "Decisions", "Notes"};
  auto rank = [](const Note& n) {
    const int group = n.group == "Design" ? 0 : n.group == "Decisions" ? 2 : 1;
    const auto at = std::find(kScaffold.begin(), kScaffold.end(), n.title);
    return std::make_tuple(group, n.group, at - kScaffold.begin(), lower(n.title));
  };
  std::stable_sort(notes.begin(), notes.end(), [&](const Note& a, const Note& b) { return rank(a) < rank(b); });
  return notes;
}

DesignNotes::Opened DesignNotes::parseOpened(const Json& show, const Json& world) {
  Opened out;
  if (show.is_object() && show.contains("note")) out.path = text(show["note"], "path");
  out.text = body(text(show, "text"));
  out.obsidian = text(show, "obsidian");
  if (!world.is_object() || !world.contains("edges") || !world["edges"].is_array()) return out;
  for (const Json& e : world["edges"]) {
    Edge edge{text(e, "from"), text(e, "type"), text(e, "to"), text(e, "fromPath"), text(e, "toPath")};
    if (e.contains("attrs") && e["attrs"].is_object()) {
      for (const auto& [key, value] : e["attrs"].items()) {
        edge.details += (edge.details.empty() ? "" : ", ") + key + ": " + (value.is_string() ? value.get<std::string>() : value.dump());
      }
    }
    out.world.push_back(std::move(edge));
  }
  return out;
}

void DesignNotes::setFolder(const fs::path& root) {
  if (root == _root) return;
  _root = root;
  _notes.clear(), _facts.clear(), _hits.clear(), _opened.reset();
  _wantOpen.reset(), _wantSearch.reset();
  if (!_query.empty()) _wantSearch = _query;
  _stamp = -1, _checked = -100;
}

DesignNotes::Ask DesignNotes::ask(std::vector<std::vector<std::string>> jms, bool summaries) {
  Ask out{{}, _root};
  out.answer = std::async(std::launch::async, [root = _root, jms = std::move(jms), summaries]() {
    Json answers = Json::array();
    for (std::vector<std::string> args : jms) {
      args.push_back("--json");
      std::string output;
      CliRunner::run(root, args, output);  // a failure's output isn't JSON: an empty answer
      answers.push_back(parseOutput(output));
    }
    if (summaries) {  // read here, off the UI thread
      for (const Note& n : parseNotes(answers[0], root)) answers[0]["summaries"][n.path] = n.summary;
    }
    return answers;
  });
  return out;
}

std::optional<Json> DesignNotes::take(Ask& ask) {
  if (!ask.valid() || ask.answer.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
  Json answer = ask.answer.get();
  if (ask.root != _root) return std::nullopt;
  return answer;
}

long long DesignNotes::designStamp() const {
  std::error_code ec;
  long long stamp = 0;
  for (auto it = fs::recursive_directory_iterator(_root / "design", fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->path().extension() != ".md") continue;
    stamp += 1 + it->last_write_time(ec).time_since_epoch().count() % 1000000007;  // a file added, removed or changed
  }
  return stamp;
}

void DesignNotes::poll(double now) {
  if (_root.empty()) return;
  if (now - _checked > 2.0) {
    _checked = now;
    if (const long long stamp = designStamp(); stamp != _stamp) {
      _stamp = stamp;
      _wantList = true;
      if (_opened && !_wantOpen) _wantOpen = _opened->path;  // its text or world may have changed
    }
  }
  if (auto answer = take(_listing)) {
    _notes = parseNotes((*answer)[0], {});
    for (Note& n : _notes) n.summary = text((*answer)[0]["summaries"], n.path.c_str());
    _facts.clear();
    if ((*answer)[1].contains("facts")) {
      for (const Json& f : (*answer)[1]["facts"]) _facts.push_back(text(f, "text"));
    }
  }
  const std::string asked = _opening.note;
  if (auto answer = take(_opening)) {
    _opened = parseOpened((*answer)[0], (*answer)[1]);
    _opened->path = asked;  // also when jm couldn't find it: it isn't asked again
  }
  if (auto answer = take(_searching)) {
    _hits.clear();
    if ((*answer)[0].contains("hits")) {
      for (const Json& h : (*answer)[0]["hits"]) _hits.push_back({text(h, "title"), text(h, "path"), text(h, "text")});
    }
  }
  if (_wantList && !_listing.valid()) {
    _wantList = false;
    _listing = ask({{"memory"}, {"memory", "recall"}}, true);
  }
  if (_wantOpen && !_opening.valid()) {
    _opening = ask({{"memory", "show", *_wantOpen}, {"memory", "world", *_wantOpen, "--steps", "1"}});
    _opening.note = *_wantOpen;
    _wantOpen.reset();
  }
  if (_wantSearch && !_searching.valid()) {
    _searching = ask({{"memory", "search", *_wantSearch}});
    _wantSearch.reset();
  }
}

void DesignNotes::open(const std::string& path) {
  if (_opened && _opened->path == path) return;
  _opened = Opened{path};  // shown at once, filled in when jm answers
  _wantOpen = path;
}

void DesignNotes::search(const std::string& query) {
  _query = query;
  if (query.empty()) {
    _hits.clear(), _wantSearch.reset();
    return;
  }
  _wantSearch = query;
}

const DesignNotes::Note* DesignNotes::find(const std::string& path) const {
  for (const Note& n : _notes)
    if (n.path == path) return &n;
  return nullptr;
}

const DesignNotes::Note* DesignNotes::resolve(const std::string& link) const {
  std::string want = lower(link.substr(0, link.find('#')));
  if (want.ends_with(".md")) want.resize(want.size() - 3);
  if (want.starts_with("design/")) want.erase(0, 7);
  for (const Note& n : _notes) {
    std::string inDesign = lower(n.path.substr(n.path.starts_with("design/") ? 7 : 0));
    inDesign.resize(inDesign.size() - (inDesign.ends_with(".md") ? 3 : 0));
    if (lower(n.title) == want || lower(stem(n.path)) == want || inDesign == want) return &n;
  }
  return nullptr;
}

const DesignNotes::Note* DesignNotes::dossierFor(const std::string& name, const std::string& prefab, const std::string& scene) const {
  auto first = [&](auto&& matches) -> const Note* {
    for (const Note& n : _notes)
      if (n.dossier() && matches(n)) return &n;
    return nullptr;
  };
  auto names = [](const Note& n, const char* key, const std::string& file) {
    return !file.empty() && n.props.contains(key) && n.props[key].is_string() && fileNamed(n.props[key].get<std::string>()) == lower(file);
  };
  const Note* found = first([&](const Note& n) { return !name.empty() && lower(n.title) == lower(name); });
  if (!found && !prefab.empty()) found = first([&](const Note& n) { return lower(n.title) == lower(stem(prefab)); });
  if (!found) found = first([&](const Note& n) { return names(n, "prefab", prefab); });
  if (!found) found = first([&](const Note& n) { return names(n, "scene", scene); });
  return found;
}
