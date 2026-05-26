#pragma once

#include <string>
#include "05_codegen/ir/ir_model.h"

namespace ultraviolet::codegen {

// DumpIR - Return a string representation of the IR Declarations
std::string DumpIR(const IRDecls& decls);

// DumpIR - Return a string representation of a single IR node
std::string DumpIR(const IRPtr& ir);

}  // namespace ultraviolet::codegen
