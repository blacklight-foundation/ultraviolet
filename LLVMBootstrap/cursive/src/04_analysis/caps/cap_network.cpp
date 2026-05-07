#include "04_analysis/caps/cap_network.h"

#include "00_core/assert_spec.h"
#include "04_analysis/caps/builtin_paths.h"
#include "04_analysis/resolve/scopes.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsCapNetwork() {
  SPEC_DEF("CapClass", "5.9.1");
  SPEC_DEF("CapMethodSig", "5.9.1");
  SPEC_DEF("CapRecv", "5.9.1");
  SPEC_DEF("NetworkInterface", "5.9.2");
}

static ast::TypePtr MakeTypeNode(const ast::TypeNode& node) {
  auto ty = std::make_shared<ast::Type>();
  ty->span = core::Span{};
  ty->node = node;
  return ty;
}

static ast::TypePtr MakeTypeStringAst(std::optional<ast::StringState> state) {
  ast::TypeString node;
  node.state = state;
  return MakeTypeNode(node);
}

static ast::Param MakeParam(std::string_view name, ast::TypePtr type) {
  ast::Param param{};
  param.mode = std::nullopt;
  param.name = std::string(name);
  param.type = std::move(type);
  param.span = core::Span{};
  return param;
}

static TypeRef TypeNetworkDynamic() {
  return MakeTypeDynamic({"Network"});
}

}  // namespace

bool IsNetworkClassPath(const ast::ClassPath& path) {
  SpecDefsCapNetwork();
  return PathMatchesBuiltinName(path, "Network");
}

std::optional<NetworkMethodSig> LookupNetworkMethodSig(std::string_view name) {
  SpecDefsCapNetwork();
  NetworkMethodSig sig{};
  sig.recv_perm = Permission::Const;

  if (IdEq(name, "restrict_to_host")) {
    sig.params = {
        MakeParam("host", MakeTypeStringAst(ast::StringState::View)),
    };
    sig.ret = TypeNetworkDynamic();
    return sig;
  }

  return std::nullopt;
}

}  // namespace cursive::analysis
