// mtirc -- the command-line driver for the Multi-Target IR Translator.
//
// Everything the tool does lives in mtir::tool::runDriver, so that the
// end-to-end tests can call it directly; this file only translates a process
// entry point into that call.
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "mtir/tool/Driver.h"

int main(int argc, char **argv) {
#if defined(_WIN32)
  // Without this, the CRT translates '\n' to "\r\n" on the way out and a
  // golden-file diff fails on Windows for a reason that has nothing to do
  // with the compiler.
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
  for (int i = 1; i < argc; ++i)
    args.emplace_back(argv[i]);

  const int status = mtir::tool::runDriver(args, std::cout, std::cerr);
  std::cout.flush();
  return status;
}
