#pragma once

// The process shell: argv, logging, archive discovery; main() calls run(), and
// Engine does the rest.
class Application {
 public:
  // `server`: journeyman_server's main (EngineOptions::server).
  Application(int argc, char** argv, bool server = false) : _argc(argc), _argv(argv), _server(server) {}

  int run();  // parse argv, init logger, construct Engine, drive lifecycle, return exit code

 private:
  int _argc;
  char** _argv;
  bool _server;
};
