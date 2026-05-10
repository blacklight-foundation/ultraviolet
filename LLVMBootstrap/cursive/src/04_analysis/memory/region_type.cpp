/*
 * =============================================================================
 * region_type.cpp - Built-in Region Modal Type Definition
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 5.4.1 "Built-in Modal Type Region" (Cursive0)
 *   - CursiveSpecification.md, Section 6.9 "Built-ins Runtime Interface"
 *
 * This file constructs the built-in Region modal type declaration used for
 * arena-based memory allocation. Region is a built-in modal with three states:
 *
 * MODAL STATES:
 *   @Active:
 *     - alloc(value: T) -> T_{pi_Region(self)} (special lowering path)
 *     - reset_unchecked() -> @Active (unsafe)
 *     - freeze() -> @Frozen
 *     - free_unchecked() -> @Freed (unsafe)
 *   @Frozen:
 *     - thaw() -> @Active
 *     - free_unchecked() -> @Freed (unsafe)
 *   @Freed:
 *     (no methods)
 *
 * =============================================================================
 */

#include "04_analysis/memory/region_type.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <string_view>

namespace cursive::analysis {

namespace {

// SPEC_DEF: Helper to construct a Type node wrapper
static std::shared_ptr<ast::Type> MakeTypeNode(const ast::TypeNode& node) {
    auto ty = std::make_shared<ast::Type>();
    ty->span = core::Span{};
    ty->node = node;
    return ty;
}

// SPEC_DEF: Helper to construct a primitive type AST node
static std::shared_ptr<ast::Type> MakeTypePrimAst(const char* name) {
    return MakeTypeNode(ast::TypePrim{ast::Identifier{name}});
}

static std::shared_ptr<ast::Type> MakeTypePathAst(const char* name) {
    ast::TypePathType type{};
    type.path.push_back(name);
    return MakeTypeNode(type);
}

static std::shared_ptr<ast::Expr> MakeIdentifierExpr(const char* name) {
    auto expr = std::make_shared<ast::Expr>();
    expr->span = core::Span{};
    expr->node = ast::IdentifierExpr{name};
    return expr;
}

static std::shared_ptr<ast::Block> MakeEmptyBlock() {
    auto block = std::make_shared<ast::Block>();
    block->stmts = {};
    block->tail_opt = nullptr;
    block->span = core::Span{};
    return block;
}

static std::shared_ptr<ast::Block> MakeReturnValueBlock(const char* value_name) {
    auto block = MakeEmptyBlock();
    ast::ReturnStmt ret{};
    ret.value_opt = MakeIdentifierExpr(value_name);
    ret.span = core::Span{};
    block->stmts.push_back(ret);
    return block;
}

// SPEC_DEF: Helper to construct a state field declaration
static ast::StateFieldDecl MakeStateField(const char* name,
                                          std::shared_ptr<ast::Type> type) {
    ast::StateFieldDecl field{};
    field.vis = ast::Visibility::Public;
    field.name = name;
    field.type = type;
    field.span = core::Span{};
    field.doc_opt = std::nullopt;
    return field;
}

static ast::TransitionDecl MakeTransition(const char* name,
                                          const char* target_state) {
    ast::TransitionDecl trans{};
    trans.vis = ast::Visibility::Public;
    trans.name = name;
    trans.params = {};
    trans.target_state = target_state;
    trans.body = MakeEmptyBlock();
    trans.span = core::Span{};
    trans.doc_opt = std::nullopt;
    return trans;
}

static ast::StateMethodDecl MakeRegionAllocMethod() {
    ast::StateMethodDecl method{};
    method.vis = ast::Visibility::Public;
    method.name = "alloc";

    ast::TypeParam t_param{};
    t_param.name = "T";
    t_param.bounds = {};
    t_param.default_type = nullptr;
    t_param.span = core::Span{};

    ast::GenericParams generic_params{};
    generic_params.params.push_back(t_param);
    generic_params.span = core::Span{};
    method.generic_params = generic_params;

    method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Unique};
    ast::Param value_param{};
    value_param.mode = std::nullopt;
    value_param.name = "value";
    value_param.type = MakeTypePathAst("T");
    value_param.span = core::Span{};
    method.params.push_back(value_param);
    method.return_type_opt = MakeTypePathAst("T");
    method.contract = std::nullopt;
    method.body = MakeReturnValueBlock("value");
    method.span = core::Span{};
    method.doc_opt = std::nullopt;
    return method;
}

static bool StateContainsNamedMember(const ast::StateBlock& state,
                                     std::string_view name) {
    for (const auto& member : state.members) {
        if (const auto* method = std::get_if<ast::StateMethodDecl>(&member)) {
            if (method->name == name) {
                return true;
            }
            continue;
        }
        if (const auto* transition = std::get_if<ast::TransitionDecl>(&member)) {
            if (transition->name == name) {
                return true;
            }
            continue;
        }
        if (const auto* field = std::get_if<ast::StateFieldDecl>(&member)) {
            if (field->name == name) {
                return true;
            }
        }
    }
    return false;
}

static bool RegionCanonicalSurfaceMatches(const ast::ModalDecl& decl) {
    const auto active = std::find_if(
        decl.states.begin(), decl.states.end(),
        [](const ast::StateBlock& state) { return state.name == "Active"; });
    const auto frozen = std::find_if(
        decl.states.begin(), decl.states.end(),
        [](const ast::StateBlock& state) { return state.name == "Frozen"; });
    const auto freed = std::find_if(
        decl.states.begin(), decl.states.end(),
        [](const ast::StateBlock& state) { return state.name == "Freed"; });
    if (active == decl.states.end() || frozen == decl.states.end() ||
        freed == decl.states.end()) {
        return false;
    }

    constexpr std::array<std::string_view, 5> kActiveRequired = {
        "handle", "alloc", "reset_unchecked", "freeze", "free_unchecked"};
    constexpr std::array<std::string_view, 3> kFrozenRequired = {
        "handle", "thaw", "free_unchecked"};
    constexpr std::array<std::string_view, 1> kFreedRequired = {"handle"};

    for (const auto name : kActiveRequired) {
        if (!StateContainsNamedMember(*active, name)) {
            return false;
        }
    }
    for (const auto name : kFrozenRequired) {
        if (!StateContainsNamedMember(*frozen, name)) {
            return false;
        }
    }
    for (const auto name : kFreedRequired) {
        if (!StateContainsNamedMember(*freed, name)) {
            return false;
        }
    }
    return true;
}

}  // namespace

// SPEC_RULE: Region Modal Declaration
// Section 5.4.1 specifies Region as a built-in modal type with states
// @Active, @Frozen, and @Freed. Each state has an internal handle field
// representing the underlying memory arena.
ast::ModalDecl BuildRegionModalDecl() {
    ast::ModalDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "Region";
    decl.implements = {};
    decl.span = core::Span{};
    decl.doc = {};

    // Internal handle field - used to track the underlying memory arena
    auto handle_type = MakeTypePrimAst("usize");

    auto make_state = [&](const char* name, std::vector<ast::StateMember> members) {
        ast::StateBlock state{};
        state.name = name;
        state.members = std::move(members);
        state.span = core::Span{};
        state.doc_opt = std::nullopt;
        return state;
    };

    std::vector<ast::StateMember> active_members;
    active_members.push_back(MakeStateField("handle", handle_type));
    // Keep alloc declared structurally so the canonical Region surface is explicit.
    // Typing/codegen still route Region::alloc through dedicated lowering that
    // applies receiver provenance semantics.
    active_members.push_back(MakeRegionAllocMethod());
    active_members.push_back(MakeTransition("reset_unchecked", "Active"));
    active_members.push_back(MakeTransition("freeze", "Frozen"));
    active_members.push_back(MakeTransition("free_unchecked", "Freed"));

    std::vector<ast::StateMember> frozen_members;
    frozen_members.push_back(MakeStateField("handle", handle_type));
    frozen_members.push_back(MakeTransition("thaw", "Active"));
    frozen_members.push_back(MakeTransition("free_unchecked", "Freed"));

    std::vector<ast::StateMember> freed_members;
    freed_members.push_back(MakeStateField("handle", handle_type));

    // SPEC_RULE: Region states
    // @Active - region is active and can allocate
    // @Frozen - region is frozen (read-only)
    // @Freed  - region has been freed
    decl.states = {
        make_state("Active", std::move(active_members)),
        make_state("Frozen", std::move(frozen_members)),
        make_state("Freed", std::move(freed_members)),
    };
    assert(RegionCanonicalSurfaceMatches(decl));

    return decl;
}

}  // namespace cursive::analysis
