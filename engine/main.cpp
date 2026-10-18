#include <iostream>
#include <string_view>

#include "core/app/Application.hpp"
#include "core/app/Platform.hpp"

int main(int argc, char** argv) {
  platform::attachParentConsole();
  // The release this is (jm doctor compares it with jm's own).
  if (argc > 1 && std::string_view(argv[1]) == "--version") {
    std::cout << JM_PROGRAM " " << JM_VERSION << "\n";
    return 0;
  }
  Application app(argc, argv);
  return app.run();
}
