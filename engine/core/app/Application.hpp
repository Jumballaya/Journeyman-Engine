#pragma once

#include <memory>

class Engine;

// Application is the process shell: argv parsing, logger bringup, and
// lifecycle orchestration of the Engine. main() constructs one and calls
// run(). Everything that makes run() work lives in Engine.
class Application {
 public:
  Application(int argc, char** argv);
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  int run();  // parse argv, init logger, construct Engine, drive lifecycle, return exit code

 private:
  int _argc;
  char** _argv;
  std::unique_ptr<Engine> _engine;
};
