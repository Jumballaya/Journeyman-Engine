#include "NoteEdit.hpp"

#include <utility>

#include "../ui/Text.hpp"

void NoteEdit::begin(std::string note) {
  _editing = true;
  _before = note;
  _text.clear();
  _length = 0;
  for (size_t i = 0; i < note.size();) type(nextCodepoint(note, i));
}

void NoteEdit::type(char32_t c) {
  const bool control = c < 0x20 || (c >= 0x7F && c < 0xA0);
  const bool bad = c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF);
  if (!_editing || control || bad || _length >= kLength) return;
  appendUtf8(_text, c);
  ++_length;
}

void NoteEdit::erase() {
  if (_text.empty()) return;
  size_t end = _text.size() - 1;
  while (end > 0 && (static_cast<unsigned char>(_text[end]) & 0xC0) == 0x80) --end;  // a continuation byte
  _text.resize(end);
  --_length;
}

std::string NoteEdit::commit() {
  _editing = false;
  _before.clear();
  _length = 0;
  return std::exchange(_text, {});
}

std::string NoteEdit::cancel() {
  std::string before = std::exchange(_before, {});
  commit();
  return before;
}
