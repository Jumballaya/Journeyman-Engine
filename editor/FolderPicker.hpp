#pragma once

#include <filesystem>
#include <string>

// The result of the native "choose a folder" dialog. Both fields are empty when
// the user cancelled; `error` is set when the dialog couldn't be shown.
struct FolderPick {
  std::filesystem::path path;
  std::string error;
};

// Shows the native folder dialog, starting in `start` when it's given.
//
// On Windows the shell's dialog needs COM on the calling thread as a
// single-threaded apartment. NFD::Guard sets that up once at startup, but
// opening an audio device undoes it: miniaudio's WASAPI device lookup pairs a
// CoInitializeEx that fails on such a thread with a CoUninitialize that runs
// anyway, and the editor opens one on the main thread as soon as it starts (the
// schema probe engine). So this initializes COM itself for the dialog's
// duration, whatever happened on the thread before.
FolderPick pickFolder(const std::filesystem::path& start = {});
