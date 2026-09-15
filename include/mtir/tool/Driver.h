// Driver.h -- the mtirc command line, as a function.
//
// Module M9.  The driver is a library entry point rather than a main() so
// that the end-to-end tests can drive it the way the Python prototype's tests
// drove theirs -- by calling it with an argument list and reading back what
// it wrote -- instead of spawning a process and plumbing a path to the
// executable through the build.
//
// Exit codes
//   0  success
//   1  the input was rejected (bad option, parse error, verifier error,
//      emitter error)
//   2  the input file could not be read
//   4  the program was run with --run and trapped
#ifndef MTIR_TOOL_DRIVER_H
#define MTIR_TOOL_DRIVER_H

#include <ostream>
#include <string>
#include <vector>

namespace mtir::tool {

/// Run the driver.  `args` excludes the program name, as in the Python
/// prototype.  Everything the tool would print goes to `out` and `err`; it
/// writes to a file only when -o asks it to.
int runDriver(const std::vector<std::string> &args, std::ostream &out,
              std::ostream &err);

/// The usage text, exposed so a test can check the options it advertises
/// against the ones it accepts.
const char *usage();

} // namespace mtir::tool

#endif // MTIR_TOOL_DRIVER_H
