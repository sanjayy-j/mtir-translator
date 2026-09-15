#include "mtir/cir/Module.h"

#include <cstddef>
#include <utility>

namespace mtir::cir {

bool operator==(const Global &a, const Global &b) {
  return a.name == b.name && a.ty == b.ty && a.init == b.init &&
         a.arrayLen == b.arrayLen;
}

Function &Module::addFunction(Function f) {
  index_.emplace(f.name(), functions_.size());
  functions_.push_back(std::move(f));
  return functions_.back();
}

void Module::rebuildIndex() {
  index_.clear();
  index_.reserve(functions_.size());
  for (std::size_t i = 0; i < functions_.size(); ++i)
    index_.emplace(functions_[i].name(), i);
}

Function *Module::function(std::string_view name) {
  const auto it = index_.find(std::string(name));
  if (it == index_.end())
    return nullptr;
  return &functions_[it->second];
}

const Function *Module::function(std::string_view name) const {
  const auto it = index_.find(std::string(name));
  if (it == index_.end())
    return nullptr;
  return &functions_[it->second];
}

bool operator==(const Module &a, const Module &b) {
  return a.globals() == b.globals() && a.functions() == b.functions();
}

} // namespace mtir::cir
