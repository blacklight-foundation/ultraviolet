#include "04_analysis/caps/cap_io.h"

#include <utility>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/builtin_paths.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/outcome.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsCapIO() {
  SPEC_DEF("CapClass", "5.9.1");
  SPEC_DEF("CapType", "5.9.1");
  SPEC_DEF("CapMethodSig", "5.9.1");
  SPEC_DEF("CapRecv", "5.9.1");
  SPEC_DEF("IOInterface", "5.9.2");
  SPEC_DEF("BuiltinTypes_IO", "5.9.2");
  SPEC_DEF("DirIterStateMembers", "5.9.2");
  SPEC_DEF("FileStateMembers", "5.9.2");
}

static std::shared_ptr<ast::Type> MakeTypeNode(const ast::TypeNode& node) {
  auto ty = std::make_shared<ast::Type>();
  ty->span = core::Span{};
  ty->node = node;
  return ty;
}

static std::shared_ptr<ast::Type> MakeTypePrimAst(std::string_view name) {
  return MakeTypeNode(ast::TypePrim{ast::Identifier{name}});
}

static std::shared_ptr<ast::Type> MakeTypePathAst(
    std::initializer_list<std::string_view> comps) {
  ast::TypePathType path_type;
  for (const auto comp : comps) {
    path_type.path.emplace_back(comp);
  }
  return MakeTypeNode(path_type);
}

static std::shared_ptr<ast::Type> MakeTypeStringAst(
    std::optional<ast::StringState> state) {
  ast::TypeString node;
  node.state = state;
  return MakeTypeNode(node);
}

static std::shared_ptr<ast::Type> MakeTypeBytesAst(
    std::optional<ast::BytesState> state) {
  ast::TypeBytes node;
  node.state = state;
  return MakeTypeNode(node);
}

static std::shared_ptr<ast::Type> MakeTypePermAst(
    ast::TypePerm perm,
    std::shared_ptr<ast::Type> base) {
  ast::TypePermType node;
  node.perm = perm;
  node.base = std::move(base);
  return MakeTypeNode(std::move(node));
}

static std::shared_ptr<ast::Type> MakeTypeModalStateAst(
    std::initializer_list<std::string_view> comps,
    std::string_view state) {
  ast::TypeModalState node;
  for (const auto comp : comps) {
    node.path.emplace_back(comp);
  }
  node.generic_args = {};
  ast::SyncTypeModalStateFromFields(node);
  node.state = ast::Identifier{state};
  return MakeTypeNode(node);
}

static std::shared_ptr<ast::Type> MakeTypeUnionAst(
    std::vector<std::shared_ptr<ast::Type>> members) {
  ast::TypeUnion node;
  node.types = std::move(members);
  return MakeTypeNode(node);
}

static std::shared_ptr<ast::Type> MakeOutcomeAst(
    std::shared_ptr<ast::Type> value_type,
    std::shared_ptr<ast::Type> error_type) {
  ast::TypePathType node;
  node.path = {"Outcome"};
  node.generic_args = {std::move(value_type), std::move(error_type)};
  return MakeTypeNode(std::move(node));
}

static ast::Param MakeParam(std::string_view name,
                            std::shared_ptr<ast::Type> type) {
  ast::Param param{};
  param.mode = std::nullopt;
  param.name = std::string(name);
  param.type = std::move(type);
  param.span = core::Span{};
  return param;
}

static std::shared_ptr<ast::Block> MakeEmptyBlock() {
  auto block = std::make_shared<ast::Block>();
  block->span = core::Span{};
  return block;
}

static ast::StateFieldDecl MakeStateField(std::string_view name,
                                          std::shared_ptr<ast::Type> type) {
  ast::StateFieldDecl field{};
  field.vis = ast::Visibility::Public;
  field.name = std::string(name);
  field.type = std::move(type);
  field.span = core::Span{};
  field.doc_opt = std::nullopt;
  return field;
}

static ast::StateMethodDecl MakeStateMethod(std::string_view name,
                                            std::vector<ast::Param> params,
                                            std::shared_ptr<ast::Type> ret) {
  ast::StateMethodDecl method{};
  method.vis = ast::Visibility::Public;
  method.name = std::string(name);
  method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Const};
  method.params = std::move(params);
  method.return_type_opt = std::move(ret);
  method.body = MakeEmptyBlock();
  method.span = core::Span{};
  method.doc_opt = std::nullopt;
  return method;
}

static ast::TransitionDecl MakeTransition(std::string_view name,
                                          std::vector<ast::Param> params,
                                          std::string_view target_state) {
  ast::TransitionDecl trans{};
  trans.vis = ast::Visibility::Public;
  trans.name = std::string(name);
  trans.params = std::move(params);
  trans.target_state = std::string(target_state);
  trans.body = MakeEmptyBlock();
  trans.span = core::Span{};
  trans.doc_opt = std::nullopt;
  return trans;
}

static TypeRef TypeStringView() {
  return MakeTypeString(StringState::View);
}

static TypeRef TypeBytesView() {
  return MakeTypeBytes(BytesState::View);
}

static TypeRef TypeBytesManaged() {
  return MakeTypeBytes(BytesState::Managed);
}

static TypeRef TypeStringManaged() {
  return MakeTypeString(StringState::Managed);
}

static TypeRef TypeIoError() {
  return MakeTypePath({"IoError"});
}

static TypeRef TypeFileKind() {
  return MakeTypePath({"FileKind"});
}

static TypeRef TypeUnique(TypeRef base) {
  return MakeTypePerm(Permission::Unique, std::move(base));
}

static TypeRef TypeFileState(std::string_view state) {
  return TypeUnique(MakeTypeModalState({"File"}, std::string(state)));
}

static TypeRef TypeDirIterState(std::string_view state) {
  return TypeUnique(MakeTypeModalState({"DirIter"}, std::string(state)));
}

static TypeRef TypeDirEntry() {
  return MakeTypePath({"DirEntry"});
}

static TypeRef TypeIODynamic() {
  return MakeTypeDynamic({"IO"});
}

static TypeRef TypeUnit() {
  return MakeTypePrim("()");
}

static TypeRef TypeBool() {
  return MakeTypePrim("bool");
}

static TypeRef Outcome(TypeRef value, TypeRef error) {
  return MakeOutcomeType(std::move(value), std::move(error));
}

}  // namespace

bool IsIOBuiltinTypePath(const ast::TypePath& path) {
  SpecDefsCapIO();
  return PathMatchesBuiltinName(path, "File") ||
         PathMatchesBuiltinName(path, "DirIter") ||
         PathMatchesBuiltinName(path, "DirEntry") ||
         PathMatchesBuiltinName(path, "FileKind") ||
         PathMatchesBuiltinName(path, "IoError");
}

bool IsIOClassPath(const ast::ClassPath& path) {
  SpecDefsCapIO();
  return PathMatchesBuiltinName(path, "IO");
}

std::optional<IOMethodSig> LookupIOMethodSig(
    std::string_view name) {
  SpecDefsCapIO();
  IOMethodSig sig{};
  sig.recv_perm = Permission::Const;

  if (IdEq(name, "open_read")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeFileState("Read"), TypeIoError());
    return sig;
  }
  if (IdEq(name, "open_write")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeFileState("Write"), TypeIoError());
    return sig;
  }
  if (IdEq(name, "open_append")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeFileState("Append"), TypeIoError());
    return sig;
  }
  if (IdEq(name, "create_write")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeFileState("Write"), TypeIoError());
    return sig;
  }
  if (IdEq(name, "read_file")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeUnique(TypeStringManaged()), TypeIoError());
    return sig;
  }
  if (IdEq(name, "read_bytes")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeUnique(TypeBytesManaged()), TypeIoError());
    return sig;
  }
  if (IdEq(name, "write_file")) {
    sig.params = {
        MakeParam("path", MakeTypeStringAst(ast::StringState::View)),
        MakeParam("data", MakeTypeBytesAst(ast::BytesState::View)),
    };
    sig.ret = Outcome(TypeUnit(), TypeIoError());
    return sig;
  }
  if (IdEq(name, "write_stdout")) {
    sig.params = {MakeParam("data", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeUnit(), TypeIoError());
    return sig;
  }
  if (IdEq(name, "write_stderr")) {
    sig.params = {MakeParam("data", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeUnit(), TypeIoError());
    return sig;
  }
  if (IdEq(name, "exists")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = TypeBool();
    return sig;
  }
  if (IdEq(name, "remove")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeUnit(), TypeIoError());
    return sig;
  }
  if (IdEq(name, "open_dir")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeDirIterState("Open"), TypeIoError());
    return sig;
  }
  if (IdEq(name, "create_dir")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeUnit(), TypeIoError());
    return sig;
  }
  if (IdEq(name, "ensure_dir")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeUnit(), TypeIoError());
    return sig;
  }
  if (IdEq(name, "kind")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = Outcome(TypeFileKind(), TypeIoError());
    return sig;
  }
  if (IdEq(name, "restrict")) {
    sig.params = {MakeParam("path", MakeTypeStringAst(ast::StringState::View))};
    sig.ret = TypeIODynamic();
    return sig;
  }

  return std::nullopt;
}

ast::ModalDecl BuildFileModalDecl() {
  SpecDefsCapIO();
  ast::ModalDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "File";
  decl.implements = {};
  decl.doc = {};
  decl.span = core::Span{};

  auto make_state = [](std::string_view name,
                       std::vector<ast::StateMember> members) {
    ast::StateBlock state{};
    state.name = std::string(name);
    state.members = std::move(members);
    state.span = core::Span{};
    state.doc_opt = std::nullopt;
    return state;
  };

  auto handle_field = MakeStateField("handle", MakeTypePrimAst("usize"));

  std::vector<ast::StateMember> read_members;
  read_members.push_back(handle_field);
  read_members.push_back(MakeStateMethod(
      "read_all", {},
      MakeOutcomeAst(
          MakeTypePermAst(
              ast::TypePerm::Unique,
              MakeTypeStringAst(ast::StringState::Managed)),
          MakeTypePathAst({"IoError"}))));
  read_members.push_back(MakeStateMethod(
      "read_all_bytes", {},
      MakeOutcomeAst(
          MakeTypePermAst(
              ast::TypePerm::Unique,
              MakeTypeBytesAst(ast::BytesState::Managed)),
          MakeTypePathAst({"IoError"}))));
  read_members.push_back(MakeTransition("close", {}, "Closed"));

  std::vector<ast::StateMember> write_members;
  write_members.push_back(handle_field);
  write_members.push_back(MakeStateMethod(
      "write", {MakeParam("data", MakeTypeBytesAst(ast::BytesState::View))},
      MakeOutcomeAst(MakeTypePrimAst("()"), MakeTypePathAst({"IoError"}))));
  write_members.push_back(MakeStateMethod(
      "flush", {},
      MakeOutcomeAst(MakeTypePrimAst("()"), MakeTypePathAst({"IoError"}))));
  write_members.push_back(MakeTransition("close", {}, "Closed"));

  std::vector<ast::StateMember> append_members;
  append_members.push_back(handle_field);
  append_members.push_back(MakeStateMethod(
      "write", {MakeParam("data", MakeTypeBytesAst(ast::BytesState::View))},
      MakeOutcomeAst(MakeTypePrimAst("()"), MakeTypePathAst({"IoError"}))));
  append_members.push_back(MakeStateMethod(
      "flush", {},
      MakeOutcomeAst(MakeTypePrimAst("()"), MakeTypePathAst({"IoError"}))));
  append_members.push_back(MakeTransition("close", {}, "Closed"));

  std::vector<ast::StateMember> closed_members;

  decl.states = {
      make_state("Read", std::move(read_members)),
      make_state("Write", std::move(write_members)),
      make_state("Append", std::move(append_members)),
      make_state("Closed", std::move(closed_members)),
  };

  return decl;
}

ast::ModalDecl BuildDirIterModalDecl() {
  SpecDefsCapIO();
  ast::ModalDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "DirIter";
  decl.implements = {};
  decl.doc = {};
  decl.span = core::Span{};

  auto make_state = [](std::string_view name,
                       std::vector<ast::StateMember> members) {
    ast::StateBlock state{};
    state.name = std::string(name);
    state.members = std::move(members);
    state.span = core::Span{};
    state.doc_opt = std::nullopt;
    return state;
  };

  auto handle_field = MakeStateField("handle", MakeTypePrimAst("usize"));

  std::vector<ast::StateMember> open_members;
  open_members.push_back(handle_field);
  open_members.push_back(MakeStateMethod(
      "next", {},
      MakeOutcomeAst(
          MakeTypeUnionAst({MakeTypePathAst({"DirEntry"}), MakeTypePrimAst("()")}),
          MakeTypePathAst({"IoError"}))));
  open_members.push_back(MakeTransition("close", {}, "Closed"));

  std::vector<ast::StateMember> closed_members;

  decl.states = {
      make_state("Open", std::move(open_members)),
      make_state("Closed", std::move(closed_members)),
  };

  return decl;
}

ast::RecordDecl BuildDirEntryRecordDecl() {
  SpecDefsCapIO();
  ast::RecordDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "DirEntry";
  decl.implements = {};
  decl.span = core::Span{};
  decl.doc = {};

  ast::FieldDecl name_field{};
  name_field.vis = ast::Visibility::Public;
  name_field.name = "name";
  name_field.type = MakeTypeStringAst(ast::StringState::Managed);
  name_field.init_opt = nullptr;
  name_field.span = core::Span{};
  name_field.doc_opt = std::nullopt;

  ast::FieldDecl path_field{};
  path_field.vis = ast::Visibility::Public;
  path_field.name = "path";
  path_field.type = MakeTypeStringAst(ast::StringState::Managed);
  path_field.init_opt = nullptr;
  path_field.span = core::Span{};
  path_field.doc_opt = std::nullopt;

  ast::FieldDecl kind_field{};
  kind_field.vis = ast::Visibility::Public;
  kind_field.name = "kind";
  kind_field.type = MakeTypePathAst({"FileKind"});
  kind_field.init_opt = nullptr;
  kind_field.span = core::Span{};
  kind_field.doc_opt = std::nullopt;

  decl.members = {name_field, path_field, kind_field};
  return decl;
}

ast::EnumDecl BuildFileKindEnumDecl() {
  SpecDefsCapIO();
  ast::EnumDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "FileKind";
  decl.implements = {{"Bitcopy"}};
  decl.span = core::Span{};
  decl.doc = {};

  auto make_variant = [](std::string_view name) {
    ast::VariantDecl variant{};
    variant.name = std::string(name);
    variant.payload_opt = std::nullopt;
    variant.discriminant_opt = std::nullopt;
    variant.span = core::Span{};
    variant.doc_opt = std::nullopt;
    return variant;
  };

  decl.variants = {make_variant("File"), make_variant("Dir"),
                   make_variant("Other")};
  return decl;
}

ast::EnumDecl BuildIoErrorEnumDecl() {
  SpecDefsCapIO();
  ast::EnumDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "IoError";
  decl.implements = {{"Bitcopy"}};
  decl.span = core::Span{};
  decl.doc = {};

  auto make_variant = [](std::string_view name) {
    ast::VariantDecl variant{};
    variant.name = std::string(name);
    variant.payload_opt = std::nullopt;
    variant.discriminant_opt = std::nullopt;
    variant.span = core::Span{};
    variant.doc_opt = std::nullopt;
    return variant;
  };

  decl.variants = {make_variant("NotFound"), make_variant("PermissionDenied"),
                   make_variant("AlreadyExists"), make_variant("InvalidPath"),
                   make_variant("Busy"), make_variant("IoFailure")};
  return decl;
}

}  // namespace ultraviolet::analysis
