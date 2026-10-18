#pragma once

#include <cstddef>
#include <string>

// A note being typed while F8 holds the run: UTF-8 text, a code point at a
// time, at most kLength of them. Commit keeps what's typed, cancel gives
// back the note as it was before.
class NoteEdit {
 public:
  static constexpr size_t kLength = 200;  // code points

  bool editing() const { return _editing; }
  const std::string& text() const { return _text; }
  // Editing `note` from its end.
  void begin(std::string note);
  // A typed character; controls, bad code points and what's past kLength aren't kept.
  void type(char32_t c);
  void erase();  // the last code point
  std::string commit();
  std::string cancel();

 private:
  bool _editing = false;
  std::string _text, _before;
  size_t _length = 0;  // code points in _text
};
