#include <iostream>

#include "core/app/Application.hpp"

int main(int argc, char** argv) {
  std::cout << "Journeyman Engine Starting up...\n";

  std::filesystem::path manifestPath = ".jm.json";
  if (argc > 1) {
    manifestPath = argv[1];
  }
  std::cout << "Loading Game Manifest: " << manifestPath << std::endl;

  Application app;
  app.initialize(manifestPath);
  app.run();

  std::cout << "Journeyman Engine Shutting down...\n";
  return 0;
}