#pragma once

#include <memory>

class Engine;

// The process shell: argv, logging, archive discovery; main() calls run(), and
// Engine does the rest.
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
