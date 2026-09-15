#include "mtir_test.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <utility>

namespace mtir::test {
namespace {

int gFailuresInCase = 0;

} // namespace

std::vector<TestCase> &registry() {
  static std::vector<TestCase> cases;
  return cases;
}

Registrar::Registrar(std::string suite, std::string name, std::function<void()> body) {
  registry().push_back(TestCase{std::move(suite), std::move(name), std::move(body)});
}

void reportFailure(const char *file, int line, const std::string &message) {
  ++gFailuresInCase;
  std::cout << "    " << file << ":" << line << ": " << message << "\n";
}

int runAll(const std::string &filter) {
  std::size_t passed = 0;
  std::size_t failed = 0;
  std::size_t skipped = 0;
  std::string currentSuite;

  for (const TestCase &tc : registry()) {
    const std::string full = tc.suite + "/" + tc.name;
    if (!filter.empty() && full.find(filter) == std::string::npos) {
      ++skipped;
      continue;
    }
    if (tc.suite != currentSuite) {
      currentSuite = tc.suite;
      std::cout << "\n[" << currentSuite << "]\n";
    }

    gFailuresInCase = 0;
    bool threw = false;
    try {
      tc.body();
    } catch (const std::exception &e) {
      std::cout << "    unexpected exception: " << e.what() << "\n";
      threw = true;
    } catch (...) {
      std::cout << "    unexpected exception (unknown type)\n";
      threw = true;
    }

    if (gFailuresInCase == 0 && !threw) {
      ++passed;
      std::cout << "  ok    " << tc.name << "\n";
    } else {
      ++failed;
      std::cout << "  FAIL  " << tc.name << "\n";
    }
  }

  std::cout << "\n" << passed << " passed, " << failed << " failed";
  if (skipped != 0)
    std::cout << ", " << skipped << " not selected";
  std::cout << "\n";
  return failed == 0 ? 0 : 1;
}

} // namespace mtir::test

int main(int argc, char **argv) {
  std::string filter;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc)
      filter = argv[++i];
    else if (std::strncmp(argv[i], "--filter=", 9) == 0)
      filter = argv[i] + 9;
    else if (std::strcmp(argv[i], "--help") == 0) {
      std::cout << "usage: mtir_tests [--filter=<substring>]\n";
      return 0;
    }
  }
  return mtir::test::runAll(filter);
}
