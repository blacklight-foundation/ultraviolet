#pragma once

#include <optional>
#include <string>

#include "llvm/Support/JSON.h"

namespace cursive::driver::lsp {

class StdioJsonRpc {
 public:
  std::optional<llvm::json::Value> ReadMessage();
  void WriteMessage(const llvm::json::Value& message);

 private:
  std::string ReadHeaderLine();
};

}  // namespace cursive::driver::lsp
