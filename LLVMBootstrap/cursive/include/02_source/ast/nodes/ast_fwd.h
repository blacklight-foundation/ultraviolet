// ===========================================================================
// ast_fwd.h - Forward declarations for all AST node types
// ===========================================================================
//
// This header provides forward declarations for all AST node types, enabling
// files to reference AST types without including full definitions. This
// reduces compilation dependencies and improves build times.
//
// Include hierarchy for AST files:
//   ast_fwd.h -> ast_enums.h -> ast_common.h -> (category headers)
//
// ===========================================================================

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace cursive::ast {

// ===========================================================================
// Common Type Aliases
// ===========================================================================

using Identifier = std::string;
using Path = std::vector<Identifier>;
using ModulePath = Path;
using TypePath = Path;
using ClassPath = Path;

// ===========================================================================
// Core Wrapper Types (forward-declared)
// ===========================================================================

struct Expr;
struct Type;
struct Pattern;
struct Block;
struct LoopInvariant;

// ===========================================================================
// Smart Pointer Aliases
// ===========================================================================

using ExprPtr = std::shared_ptr<Expr>;
using TypePtr = std::shared_ptr<Type>;
using PatternPtr = std::shared_ptr<Pattern>;
using BlockPtr = std::shared_ptr<Block>;

// ===========================================================================
// Documentation (from lexer)
// ===========================================================================

// DocComment is used from cursive::lexer namespace via ast_common.h

// ===========================================================================
// Attribute System
// ===========================================================================

struct AttributeArg;
struct AttributeItem;

// ===========================================================================
// Type Node Variants
// ===========================================================================

struct TypePrim;
struct TypePermType;
struct TypeUnion;
struct TypeFunc;
struct TypeFuncParam;
struct SharedDep;
struct TypeClosure;
struct TypeTuple;
struct TypeArray;
struct TypeSlice;
struct TypeSafePtr;
struct TypeRawPtr;
struct TypeString;
struct TypeBytes;
struct TypeDynamic;
struct TypeModalState;
struct TypePathType;
struct TypeApply;
struct TypeOpaque;
struct TypeRefine;
struct TypeRange;
struct TypeRangeInclusive;
struct TypeRangeFrom;
struct TypeRangeTo;
struct TypeRangeToInclusive;
struct TypeRangeFull;

// ===========================================================================
// Expression Node Variants
// ===========================================================================

struct ErrorExpr;
struct LiteralExpr;
struct IdentifierExpr;
struct QualifiedNameExpr;
struct QualifiedApplyExpr;
struct PathExpr;
struct RangeExpr;
struct BinaryExpr;
struct CastExpr;
struct UnaryExpr;
struct DerefExpr;
struct AddressOfExpr;
struct MoveExpr;
struct AllocExpr;
struct PtrNullExpr;
struct TupleExpr;
struct ArrayElemSegment;
struct ArrayRepeatSegment;
struct ArrayExpr;
struct ArrayRepeatExpr;
struct SizeofExpr;
struct AlignofExpr;
struct RecordExpr;
struct EnumLiteralExpr;
struct IfExpr;
struct IfIsExpr;
struct IfCaseExpr;
struct ComptimeExpr;
struct CtIfExpr;
struct CtLoopIterExpr;
struct TypeLiteralExpr;
struct QuoteExpr;
struct SpliceExprNode;
struct SpliceIdentNode;
struct LoopInfiniteExpr;
struct LoopConditionalExpr;
struct LoopIterExpr;
struct BlockExpr;
struct UnsafeBlockExpr;
struct AttributedExpr;
struct TransmuteExpr;
struct FieldAccessExpr;
struct TupleAccessExpr;
struct IndexAccessExpr;
struct CallExpr;
struct CallTypeArgsExpr;
struct MethodCallExpr;
struct PropagateExpr;
struct ResultExpr;
struct EntryExpr;
struct YieldExpr;
struct YieldFromExpr;
struct SyncExpr;
struct RaceExpr;
struct AllExpr;
struct ParallelExpr;
struct SpawnExpr;
struct WaitExpr;
struct FenceExpr;
struct DispatchExpr;

// Expression helper types
struct Arg;
struct ParenArgs;
struct FieldInit;
struct BraceArgs;
struct EnumPayloadParen;
struct EnumPayloadBrace;
struct GenericTypeRef;
struct ModalStateRef;
struct IfCaseClause;
struct RaceHandler;
struct RaceArm;
struct ParallelOption;
struct SpawnOption;
struct DispatchOption;
struct DispatchKeyClause;
struct KeyPathExpr;
struct KeySegField;
struct KeySegIndex;

// ===========================================================================
// Pattern Node Variants
// ===========================================================================

struct LiteralPattern;
struct WildcardPattern;
struct IdentifierPattern;
struct TypedPattern;
struct TuplePattern;
struct FieldPattern;
struct RecordPattern;
struct EnumPattern;
struct ModalPattern;
struct RangePattern;

// Pattern helper types
struct TuplePayloadPattern;
struct RecordPayloadPattern;
struct ModalRecordPayload;

// ===========================================================================
// Statement Node Variants
// ===========================================================================

struct Binding;
struct LetStmt;
struct VarStmt;
struct UsingLocalStmt;
struct AssignStmt;
struct CompoundAssignStmt;
struct ExprStmt;
struct DeferStmt;
struct RegionStmt;
struct FrameStmt;
struct ReturnStmt;
struct BreakStmt;
struct ContinueStmt;
struct UnsafeBlockStmt;
struct KeyBlockStmt;
struct CtStmt;
using ComptimeStmt = CtStmt;
struct ErrorStmt;

// ===========================================================================
// Item/Declaration Node Variants
// ===========================================================================

struct UsingDecl;
struct ImportDecl;
struct ExternBlock;
struct ExternProcDecl;
struct StaticDecl;
struct ProcedureDecl;
struct ComptimeProcedureDecl;
struct RecordDecl;
struct FieldDecl;
struct MethodDecl;
struct EnumDecl;
struct VariantDecl;
struct ModalDecl;
struct StateBlock;
struct ClassDecl;
struct TypeAliasDecl;
struct DeriveTargetDecl;
struct ErrorItem;

// Declaration helper types
struct UsingSpec;
struct UsingItem;
struct UsingList;
struct UsingWildcard;
struct Param;
struct ReceiverShorthand;
struct ReceiverExplicit;
struct ExternAbiString;
struct ExternAbiIdent;
struct StateFieldDecl;
struct StateMethodDecl;
struct TransitionDecl;
struct ClassFieldDecl;
struct ClassMethodDecl;
struct AssociatedTypeDecl;
struct AbstractFieldDecl;
struct AbstractStateDecl;
struct VariantPayloadTuple;
struct VariantPayloadRecord;

// ===========================================================================
// Generics System
// ===========================================================================

struct TypeBound;
struct TypeParam;
struct GenericParams;
struct GenericArgs;
struct PredicateReq;
using PredicateClause = std::vector<PredicateReq>;

// ===========================================================================
// Contract System
// ===========================================================================

struct ContractClause;
struct ForeignContractClause;
struct ContractIntrinsicExpr;
struct TypeInvariant;

// ===========================================================================
// Module-Level Structures
// ===========================================================================

struct ASTModule;
struct ASTFile;

}  // namespace cursive::ast
