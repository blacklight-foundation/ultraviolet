#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/type_layout.h"
#include "04_analysis/typing/type_wf.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

// §5.2.12 Expression Typing (Cursive0)

// Core expression typing - infers type of expression
ExprTypeResult TypeExpr(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::ExprPtr& expr,
                        const TypeEnv& env);

// Check expression against expected type
CheckResult CheckExprAgainst(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::ExprPtr& expr,
                             const TypeRef& expected,
                             const TypeEnv& env);

// Check place expression against expected type
CheckResult CheckPlaceAgainst(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::ExprPtr& expr,
                              const TypeRef& expected,
                              const TypeEnv& env);

// Place expression typing - for lvalues
PlaceTypeResult TypePlace(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::ExprPtr& expr,
                          const TypeEnv& env);

// Individual expression form handlers - Identifiers and Paths
ExprTypeResult TypeIdentifierExpr(const ScopeContext& ctx,
                                  const ast::IdentifierExpr& expr,
                                  const TypeEnv& env);

ExprTypeResult TypePathExpr(const ScopeContext& ctx,
                            const ast::PathExpr& expr,
                            const TypeEnv& env);

// Field Access
ExprTypeResult TypeFieldAccessExpr(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::FieldAccessExpr& expr,
                                   const TypeEnv& env);

PlaceTypeResult TypeFieldAccessPlace(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::FieldAccessExpr& expr,
                                     const TypeEnv& env);

// Tuple Access
ExprTypeResult TypeTupleAccessExpr(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::TupleAccessExpr& expr,
                                   const TypeEnv& env);

PlaceTypeResult TypeTupleAccessPlace(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::TupleAccessExpr& expr,
                                     const TypeEnv& env);

// Binary Operators
ExprTypeResult TypeBinaryExpr(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::BinaryExpr& expr,
                              const TypeEnv& env);

// Unary Operators
ExprTypeResult TypeUnaryExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::UnaryExpr& expr,
                             const TypeEnv& env,
                             const core::Span& span = core::Span{});

// Cast Expression
ExprTypeResult TypeCastExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::CastExpr& expr,
                            const TypeEnv& env);

// If Expression
ExprTypeResult TypeIfExpr(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::IfExpr& expr,
                          const TypeEnv& env);

CheckResult CheckIfExpr(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::IfExpr& expr,
                        const TypeRef& expected,
                        const TypeEnv& env);

// Range Expression
ExprTypeResult TypeRangeExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::RangeExpr& expr,
                             const TypeEnv& env);

// Address-Of Expression
ExprTypeResult TypeAddressOfExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::AddressOfExpr& expr,
                                 const TypeEnv& env);

// Dereference Expression
ExprTypeResult TypeDerefExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::DerefExpr& expr,
                             const TypeEnv& env);

PlaceTypeResult TypeDerefPlace(const ScopeContext& ctx,
                               const StmtTypeContext& type_ctx,
                               const ast::DerefExpr& expr,
                               const TypeEnv& env);

// Move Expression
ExprTypeResult TypeMoveExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::MoveExpr& expr,
                            const TypeEnv& env);

// Alloc Expression
ExprTypeResult TypeAllocExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::AllocExpr& expr,
                             const TypeEnv& env);

// Transmute Expression
ExprTypeResult TypeTransmuteExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::TransmuteExpr& expr,
                                 const TypeEnv& env);

// Propagate Expression (Sigma)
ExprTypeResult TypePropagateExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::PropagateExpr& expr,
                                 const TypeEnv& env);

// Record Literal
ExprTypeResult TypeRecordExpr(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::RecordExpr& expr,
                              const TypeEnv& env);

// Enum Literal
ExprTypeResult TypeEnumLiteralExpr(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::EnumLiteralExpr& expr,
                                   const TypeEnv& env);

// Error Expression
ExprTypeResult TypeErrorExpr(const ScopeContext& ctx,
                             const ast::ErrorExpr& expr);

// Method Call Expression
ExprTypeResult TypeMethodCallExpr(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::MethodCallExpr& expr,
                                  const TypeEnv& env,
                                  const core::Span& span = core::Span{});

// Helper predicates
bool IsPlaceExpr(const ast::ExprPtr& expr);
bool BitcopyType(const ScopeContext& ctx, const TypeRef& type);
bool CloneType(const ScopeContext& ctx, const TypeRef& type);
bool EqType(const TypeRef& type);
bool OrdType(const TypeRef& type);
bool CastValid(const TypeRef& source, const TypeRef& target);
bool IsInUnsafeSpan(const ScopeContext& ctx, const core::Span& span);
TypeRef StripPerm(const TypeRef& type);
std::optional<std::string_view> ComptimeTypeAvailabilityDiag(
    const ScopeContext& ctx,
    const TypeRef& type,
    std::string_view unavailable_diag_id);

// Internal helpers exposed for split expression files
bool IsCapabilityType(const TypeRef& type);
bool IsImpureType(const TypeRef& type);
bool ParamsPure(const ScopeContext& ctx,
                const std::vector<TypeFuncParam>& params);
bool ParamsPure(const ScopeContext& ctx,
                const std::vector<ast::Param>& params,
                const std::function<LowerTypeResult(
                    const std::shared_ptr<ast::Type>&)>& lower_type);
TypeRef SubstSelfType(const TypeRef& self,
                      const TypeRef& type);
TypeRef SubstSelfType(const TypeRef& self,
                      const TypeRef& type,
                      const TypeSubst* assoc_subst);
std::optional<Permission> PermOfTypeOpt(const TypeRef& type);
Permission PermOfType(const TypeRef& type);
bool IsPrimType(const TypeRef& type, std::string_view name);

// NOTE: SizeOf and AlignOf are declared in type_layout.h

}  // namespace cursive::analysis
