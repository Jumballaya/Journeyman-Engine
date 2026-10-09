#include "core/app/Application.hpp"

// journeyman_server: the engine without its frontend (window, renderer, UI,
// audio). It runs the same game files as a dedicated multiplayer server.
int main(int argc, char** argv) {
  Application app(argc, argv, /*server=*/true);
  return app.run();
}
