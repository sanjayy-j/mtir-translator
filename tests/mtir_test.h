// mtir_test.h -- a very small test harness.
//
// Why not doctest/Catch2/GoogleTest: the project has had zero third-party
// dependencies since the scaffold (requirements.txt: "no dependencies, which
// keeps the artefact readable and the setup trivial"), and pulling a 400 KB
// vendored header, or a FetchContent download that needs network at configure
// time, to get TEST_CASE and CHECK would be a poor trade for a student build
// on a lab machine.  What is needed is registration, an assertion that keeps
// going after a failure, and a readable report -- which is what this is.
//
// Swapping in Catch2 later is a mechanical change: MTIR_TEST becomes
// TEST_CASE and CHECK_EQ becomes CHECK.
#ifndef MTIR_TESTS_MTIR_TEST_H
#define MTIR_TESTS_MTIR_TEST_H

#include <cstddef>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace mtir::test {

struct TestCase {
  std::string suite;
  std::string name;
  std::function<void()> body;
};

std::vector<TestCase> &registry();

/// Registration happens through a namespace-scope object, so a test file only
/// has to be linked in for its cases to run.
struct Registrar {
  Registrar(std::string suite, std::string name, std::function<void()> body);
};

void reportFailure(const char *file, int line, const std::string &message);

/// Runs every registered case.  Returns a process exit code.
int runAll(const std::string &filter);

// -- value printing for failure messages -----------------------------------
template <typename T> std::string show(const T &value) {
  // enum class has no operator<<, and adding one for every project enum would
  // make the harness depend on the code under test.  The expression text in
  // the failure message already names the enum, so the numeric value is
  // enough to tell two cases apart.
  if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<long long>(value));
  } else {
    std::ostringstream out;
    out << value;
    return out.str();
  }
}
inline std::string show(bool value) { return value ? "true" : "false"; }
inline std::string show(const std::string &value) { return "\"" + value + "\""; }

} // namespace mtir::test

// clang-format off
#define MTIR_CONCAT_(a, b) a##b
#define MTIR_CONCAT(a, b) MTIR_CONCAT_(a, b)

/// Declare a test case.  `suite` groups the output; `name` identifies it.
#define MTIR_TEST(suite, name)                                                 \
  static void MTIR_CONCAT(mtir_test_body_, __LINE__)();                        \
  static const ::mtir::test::Registrar MTIR_CONCAT(mtir_test_reg_, __LINE__)(  \
      suite, name, &MTIR_CONCAT(mtir_test_body_, __LINE__));                   \
  static void MTIR_CONCAT(mtir_test_body_, __LINE__)()

// Variadic so that a braced initialiser inside the expression -- whose commas
// the preprocessor would otherwise read as argument separators -- still works.
// CHECK_EQ and CHECK_NE take exactly two arguments, so a top-level braced
// initialiser there must be hoisted into a local first.
#define CHECK(...)                                                             \
  do {                                                                         \
    if (!(__VA_ARGS__))                                                        \
      ::mtir::test::reportFailure(__FILE__, __LINE__,                          \
                                  "expected: " #__VA_ARGS__);                  \
  } while (false)

#define CHECK_EQ(actual, expected)                                             \
  do {                                                                         \
    const auto &mtir_a_ = (actual);                                            \
    const auto &mtir_b_ = (expected);                                          \
    if (!(mtir_a_ == mtir_b_))                                                 \
      ::mtir::test::reportFailure(                                             \
          __FILE__, __LINE__,                                                  \
          std::string(#actual " == " #expected "\n      actual:   ") +         \
              ::mtir::test::show(mtir_a_) + "\n      expected: " +             \
              ::mtir::test::show(mtir_b_));                                    \
  } while (false)

#define CHECK_NE(actual, expected)                                             \
  do {                                                                         \
    if ((actual) == (expected))                                                \
      ::mtir::test::reportFailure(__FILE__, __LINE__,                          \
                                  "expected " #actual " != " #expected);       \
  } while (false)
// clang-format on

#endif // MTIR_TESTS_MTIR_TEST_H
