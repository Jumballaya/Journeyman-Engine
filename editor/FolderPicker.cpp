#include "FolderPicker.hpp"

#include <nfd.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#endif

FolderPick pickFolder(const std::filesystem::path& start) {
#ifdef _WIN32
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
#endif
  // NFD's paths are UTF-8 on every platform; std::filesystem only reads char
  // strings as UTF-8 on macOS and Linux, so convert through char8_t.
  const std::u8string startText = start.u8string();
  NFD::UniquePath folder;
  const nfdresult_t result =
      NFD::PickFolder(folder, startText.empty() ? nullptr : reinterpret_cast<const char*>(startText.c_str()));
#ifdef _WIN32
  if (SUCCEEDED(com)) CoUninitialize();
#endif

  FolderPick pick;
  if (result == NFD_OKAY) {
    pick.path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(folder.get())));
  } else if (result == NFD_ERROR) {
    const char* message = NFD::GetError();
    pick.error = message && *message ? message : "The folder dialog couldn't open.";
  }
  return pick;
}
