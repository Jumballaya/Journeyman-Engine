#pragma once

// The process shell: argv, logging, archive discovery; main() calls run(), and
// Engine does the rest.
class Application {
 public:
  Application(int argc, char** argv) : _argc(argc), _argv(argv) {}

  int run();  // parse argv, init logger, construct Engine, drive lifecycle, return exit code

 private:
  int _argc;
  char** _argv;
};
