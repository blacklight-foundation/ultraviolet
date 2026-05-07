#pragma once

#include <iosfwd>
#include <string>

#include "02_source/ast/ast.h"

namespace cursive::ast {

struct DumpOptions {
  bool include_spans = true;
  bool include_docs = false;
  int indent_size = 2;
  int max_depth = -1;
};

void dump(std::ostream& out, const Type& type, const DumpOptions& opts = {});
std::string to_string(const Type& type, const DumpOptions& opts = {});

void dump(std::ostream& out, const Expr& expr, const DumpOptions& opts = {});
std::string to_string(const Expr& expr, const DumpOptions& opts = {});

void dump(std::ostream& out, const Pattern& pattern,
          const DumpOptions& opts = {});
std::string to_string(const Pattern& pattern, const DumpOptions& opts = {});

void dump(std::ostream& out, const Stmt& stmt, const DumpOptions& opts = {});
std::string to_string(const Stmt& stmt, const DumpOptions& opts = {});

void dump(std::ostream& out, const Block& block, const DumpOptions& opts = {});
std::string to_string(const Block& block, const DumpOptions& opts = {});

void dump(std::ostream& out, const ASTItem& item, const DumpOptions& opts = {});
std::string to_string(const ASTItem& item, const DumpOptions& opts = {});

}  // namespace cursive::ast
