#include <iostream>
#include <string_view>

#include "core/app/Application.hpp"

int main(int argc, char** argv) {
  // The release this is (jm doctor compares it with jm's own).
  if (argc > 1 && std::string_view(argv[1]) == "--version") {
    std::cout << "journeyman_engine " << JM_VERSION << "\n";
    return 0;
  }
  Application app(argc, argv);
  return app.run();
}
