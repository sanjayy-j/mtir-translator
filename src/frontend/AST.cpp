#include "mtir/ast/AST.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "mtir/cir/Printer.h" // printDouble, so the AST dump and the CIR text
                              // agree on how a float literal is spelled

namespace mtir::ast {
namespace {

void dumpNode(const Node &node, int indent, std::string &out);

void line(std::string &out, int indent, const std::string &text) {
  out.append(static_cast<std::size_t>(indent) * 2, ' ');
  out += text;
  out += '\n';
}

void dumpChild(const Node *child, int indent, std::string &out) {
  if (child != nullptr)
    dumpNode(*child, indent, out);
}

void dumpNode(const Node &node, int indent, std::string &out) {
  switch (node.kind()) {
  case NodeKind::IntLit:
    line(out, indent,
         "IntLit " + std::to_string(static_cast<const IntLit &>(node).value()));
    return;
  case NodeKind::FloatLit:
    line(out, indent,
         "FloatLit " + cir::printDouble(static_cast<const FloatLit &>(node).value()));
    return;
  case NodeKind::BoolLit:
    line(out, indent,
         std::string("BoolLit ") +
             (static_cast<const BoolLit &>(node).value() ? "true" : "false"));
    return;
  case NodeKind::VarRef:
    line(out, indent, "VarRef " + static_cast<const VarRef &>(node).name());
    return;
  case NodeKind::Unary: {
    const auto &n = static_cast<const Unary &>(node);
    line(out, indent, "Unary " + n.op());
    dumpChild(&n.operand(), indent + 1, out);
    return;
  }
  case NodeKind::Binary: {
    const auto &n = static_cast<const Binary &>(node);
    line(out, indent, "Binary " + n.op());
    dumpChild(&n.lhs(), indent + 1, out);
    dumpChild(&n.rhs(), indent + 1, out);
    return;
  }
  case NodeKind::Call: {
    const auto &n = static_cast<const Call &>(node);
    line(out, indent, "Call " + n.callee());
    for (const ExprPtr &a : n.args())
      dumpChild(a.get(), indent + 1, out);
    return;
  }
  case NodeKind::Index: {
    const auto &n = static_cast<const Index &>(node);
    line(out, indent, "Index");
    dumpChild(&n.base(), indent + 1, out);
    dumpChild(&n.index(), indent + 1, out);
    return;
  }
  case NodeKind::Conv: {
    const auto &n = static_cast<const Conv &>(node);
    line(out, indent, "Conv");
    dumpChild(&n.operand(), indent + 1, out);
    return;
  }
  case NodeKind::Block: {
    const auto &n = static_cast<const Block &>(node);
    line(out, indent, "Block");
    for (const StmtPtr &s : n.statements())
      dumpChild(s.get(), indent + 1, out);
    return;
  }
  case NodeKind::Let: {
    const auto &n = static_cast<const Let &>(node);
    line(out, indent, "Let " + n.name() + ": " + n.type().toString());
    dumpChild(n.init(), indent + 1, out);
    return;
  }
  case NodeKind::Assign: {
    const auto &n = static_cast<const Assign &>(node);
    line(out, indent, "Assign");
    dumpChild(&n.target(), indent + 1, out);
    dumpChild(&n.value(), indent + 1, out);
    return;
  }
  case NodeKind::ExprStmt: {
    const auto &n = static_cast<const ExprStmt &>(node);
    line(out, indent, "ExprStmt");
    dumpChild(&n.expr(), indent + 1, out);
    return;
  }
  case NodeKind::If: {
    const auto &n = static_cast<const If &>(node);
    line(out, indent, "If");
    dumpChild(&n.cond(), indent + 1, out);
    dumpChild(&n.thenBlock(), indent + 1, out);
    dumpChild(n.elseBlock(), indent + 1, out);
    return;
  }
  case NodeKind::While: {
    const auto &n = static_cast<const While &>(node);
    line(out, indent, "While");
    dumpChild(&n.cond(), indent + 1, out);
    dumpChild(&n.body(), indent + 1, out);
    return;
  }
  case NodeKind::For: {
    const auto &n = static_cast<const For &>(node);
    line(out, indent, "For");
    dumpChild(n.init(), indent + 1, out);
    dumpChild(n.cond(), indent + 1, out);
    dumpChild(n.step(), indent + 1, out);
    dumpChild(&n.body(), indent + 1, out);
    return;
  }
  case NodeKind::Break:
    line(out, indent, "Break");
    return;
  case NodeKind::Continue:
    line(out, indent, "Continue");
    return;
  case NodeKind::Return: {
    const auto &n = static_cast<const Return &>(node);
    line(out, indent, "Return");
    dumpChild(n.value(), indent + 1, out);
    return;
  }
  case NodeKind::FnDecl: {
    const auto &n = static_cast<const FnDecl &>(node);
    std::string params;
    for (std::size_t i = 0; i < n.params().size(); ++i) {
      if (i != 0)
        params += ", ";
      params += n.params()[i].name + ": " + n.params()[i].type.toString();
    }
    line(out, indent,
         "FnDecl " + n.name() + "(" + params + ") -> " + n.returnType().toString());
    dumpChild(&n.body(), indent + 1, out);
    return;
  }
  case NodeKind::GlobalDecl: {
    const auto &n = static_cast<const GlobalDecl &>(node);
    line(out, indent, "GlobalDecl " + n.name() + ": " + n.type().toString());
    dumpChild(n.init(), indent + 1, out);
    return;
  }
  }
}

} // namespace

std::string TypeNode::toString() const {
  return arrayLength.has_value() ? name + "[" + std::to_string(*arrayLength) + "]"
                                 : name;
}

std::string dump(const Program &program) {
  std::string out;
  line(out, 0, "Program");
  for (const DeclPtr &d : program.decls())
    dumpNode(*d, 1, out);
  return out;
}

} // namespace mtir::ast
