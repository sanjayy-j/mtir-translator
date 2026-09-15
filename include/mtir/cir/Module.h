// Module.h -- a whole CIR translation unit.
#ifndef MTIR_CIR_MODULE_H
#define MTIR_CIR_MODULE_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mtir/cir/Function.h"

namespace mtir::cir {

struct Global {
  std::string name;
  Ty ty = Ty::I32;
  std::optional<Value> init;
  /// Set for `global @a : i32[8]`; empty for a scalar global.
  std::optional<int> arrayLen;
};

bool operator==(const Global &a, const Global &b);
inline bool operator!=(const Global &a, const Global &b) { return !(a == b); }

class Module {
public:
  Module() = default;
  explicit Module(std::string name) : name_(std::move(name)) {}

  const std::string &name() const { return name_; }
  void setName(std::string name) { name_ = std::move(name); }

  const std::vector<Global> &globals() const { return globals_; }
  std::vector<Global> &globals() { return globals_; }
  void addGlobal(Global g) { globals_.push_back(std::move(g)); }

  const std::vector<Function> &functions() const { return functions_; }
  std::vector<Function> &functions() { return functions_; }

  Function &addFunction(Function f);

  Function *function(std::string_view name);
  const Function *function(std::string_view name) const;

  void rebuildIndex();

private:
  std::string name_ = "module";
  std::vector<Global> globals_;
  std::vector<Function> functions_;
  std::unordered_map<std::string, std::size_t> index_;
};

bool operator==(const Module &a, const Module &b);
inline bool operator!=(const Module &a, const Module &b) { return !(a == b); }

} // namespace mtir::cir

#endif // MTIR_CIR_MODULE_H
