// =============================================================================
// MIGRATION MAPPING: ir_dump.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.0 (lines 14196-14347) for IR structure
//   - Section 6.4 (lines 15665-15992) for expression lowering
//   - No explicit dump format specified; this is debugging infrastructure
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/ir_dump.cpp
//   - Dumper struct with formatting state (lines 13-18)
//   - NormalizeOpaqueName (lines 19-33)
//   - IsSimpleIdent (lines 35-50)
//   - ShouldCombineAddrOf (lines 52-60)
//   - Indent helper (lines 62-64)
//   - DumpImmediateBytes (lines 66-81)
//   - Dump(IRValue) (lines 83-111)
//   - DumpRange (lines 113-149)
//   - DumpPattern (lines 151-185)
//   - DumpCall (lines 187-198)
//   - Dump(IRPtr) (lines 200-203)
//   - DumpNode overloads for each IR node type (lines 205-773)
//   - DumpIR(IRDecls) (lines 778-813)
//   - DumpIR(IRPtr) (lines 815-819)
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IR node types)
//   - ultraviolet/include/05_codegen/ir/ir_dump.h (dump interface)
//   - analysis::TypeToString for type printing
//   - IRRangeKind for range formatting
//
// REFACTORING NOTES:
//   1. Preserve human-readable dump format for debugging
//   2. Handle all IR node types including:
//      - Basic: IROpaque, IRSeq, IRCall, IRCallVTable
//      - Variables: IRReadVar, IRBindVar, IRStoreVar, IRStoreVarNoDrop
//      - Places: IRReadPlace, IRWritePlace, IRAddrOf
//      - Pointers: IRReadPtr, IRWritePtr
//      - Operations: IRUnaryOp, IRBinaryOp, IRCast, IRTransmute
//      - Checks: IRCheckIndex, IRCheckRange, IRCheckSliceLen, IRCheckOp, IRCheckCast
//      - Control: IRReturn, IRResult, IRBreak, IRContinue, IRDefer
//      - Structured: IRIf, IRBlock, IRLoop, IRIfCase, IRRegion, IRFrame
//      - Low-level: IRBranch, IRPhi
//      - Panic: IRClearPanic, IRPanicCheck, IRInitPanicHandle, IRLowerPanic
//      - Poison: IRCheckPoison
//      - Parallelism: IRParallel, IRSpawn, IRWait, IRCancelCheck,
//        IRCancelSuppress, IRDispatch (Section 19)
//      - Async: IRYield, IRYieldFrom, IRSync, IRRaceReturn, IRRaceYield, IRAll (Section 19.2-19.3)
//   3. Smart formatting for common patterns (addr_of + bind, call + bind)
//   4. Indentation-aware output for nested structures
//   5. Display map for opaque value renaming
//
// OUTPUT FORMAT:
//   - Procedures: proc @symbol { ... }
//   - Global constants: global_const @symbol bytes=N
//   - Global zeros: global_zero @symbol size=N
//   - VTables: vtable @symbol size=N align=N drop=@sym slots=[...]
// =============================================================================

#include "05_codegen/ir/ir_dump.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include "04_analysis/typing/types.h"

namespace ultraviolet::codegen {

namespace {

struct Dumper {
  std::ostringstream oss;
  int indent_level = 0;
  std::unordered_map<std::string, std::string> display_map;
  std::unordered_map<std::string, std::string> addr_place;

  static std::string NormalizeOpaqueName(const std::string& name) {
    if (name.empty()) {
      return name;
    }
    const std::size_t underscore = name.rfind('_');
    if (underscore == std::string::npos || underscore + 1 >= name.size()) {
      return name;
    }
    for (std::size_t i = underscore + 1; i < name.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
        return name;
      }
    }
    return name.substr(0, underscore);
  }

  static bool IsSimpleIdent(const std::string& name) {
    if (name.empty()) {
      return false;
    }
    unsigned char first = static_cast<unsigned char>(name[0]);
    if (!(std::isalpha(first) || name[0] == '_')) {
      return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
      unsigned char c = static_cast<unsigned char>(name[i]);
      if (!(std::isalnum(c) || name[i] == '_')) {
        return false;
      }
    }
    return true;
  }

  static bool ShouldCombineAddrOf(const std::string& place) {
    if (place.empty()) {
      return false;
    }
    if (place[0] == '*') {
      return false;
    }
    return place.find('.') == std::string::npos;
  }

  void Indent() {
    for (int i = 0; i < indent_level; ++i) oss << "  ";
  }

  void DumpImmediateBytes(const IRValue& v) {
    std::ostringstream hex;
    hex << std::uppercase << std::hex;
    for (auto it = v.bytes.rbegin(); it != v.bytes.rend(); ++it) {
      hex << std::setw(2) << std::setfill('0') << static_cast<int>(*it);
    }
    std::string hex_str = hex.str();
    // Strip leading zeros for readability.
    auto it = std::find_if(hex_str.begin(), hex_str.end(), [](char c) { return c != '0'; });
    if (it == hex_str.end()) {
      oss << "0x0";
      return;
    }
    hex_str.erase(hex_str.begin(), it);
    oss << "0x" << hex_str;
  }

  void Dump(const IRValue& v) {
    switch (v.kind) {
      case IRValue::Kind::Opaque:
        if (v.name.empty()) {
          oss << "opaque";
        } else {
          auto it = display_map.find(v.name);
          if (it != display_map.end()) {
            oss << it->second;
          } else {
            oss << NormalizeOpaqueName(v.name);
          }
        }
        break;
      case IRValue::Kind::Local:
        oss << "%" << v.name;
        break;
      case IRValue::Kind::Symbol:
        oss << "@" << v.name;
        break;
      case IRValue::Kind::Immediate:
        if (!v.bytes.empty()) {
          DumpImmediateBytes(v);
        } else {
          oss << v.name;
        }
        break;
    }
  }

  void DumpRange(const IRRange& range) {
    auto dump_opt = [this](const std::optional<IRValue>& v) {
      if (!v.has_value()) {
        oss << "?";
        return;
      }
      Dump(*v);
    };

    switch (range.kind) {
      case IRRangeKind::Full:
        oss << "..";
        return;
      case IRRangeKind::From:
        dump_opt(range.lo);
        oss << "..";
        return;
      case IRRangeKind::To:
        oss << "..";
        dump_opt(range.hi);
        return;
      case IRRangeKind::ToInclusive:
        oss << "..=";
        dump_opt(range.hi);
        return;
      case IRRangeKind::Exclusive:
        dump_opt(range.lo);
        oss << "..";
        dump_opt(range.hi);
        return;
      case IRRangeKind::Inclusive:
        dump_opt(range.lo);
        oss << "..=";
        dump_opt(range.hi);
        return;
    }
  }

  void DumpPattern(const IRPattern& pattern) {
    std::visit(
        [this](const auto& pat) {
          using T = std::decay_t<decltype(pat)>;
        if constexpr (std::is_same_v<T, IRLiteralPattern>) {
          oss << "<literal " << pat.literal.lexeme << ">";
        } else if constexpr (std::is_same_v<T, IRWildcardPattern>) {
          oss << "_";
        } else if constexpr (std::is_same_v<T, IRIdentifierPattern>) {
          oss << pat.name;
        } else if constexpr (std::is_same_v<T, IRTypedPattern>) {
          oss << pat.name;
        } else if constexpr (std::is_same_v<T, IRModalPattern>) {
          oss << "@" << pat.state;
          if (pat.fields) {
            oss << " {";
            for (std::size_t i = 0; i < pat.fields->fields.size(); ++i) {
              const auto& field = pat.fields->fields[i];
              if (i) {
                oss << ", ";
              }
              oss << field.name;
              if (field.pattern) {
                oss << ": ";
                DumpPattern(*field.pattern);
              }
            }
            oss << "}";
          }
        } else {
          oss << "<pattern>";
        }
      },
      pattern.node);
  }

  void DumpCall(const IRCall& call) {
    oss << "call ";
    Dump(call.callee);
    if (!call.args.empty()) {
      oss << " (";
      for (std::size_t i = 0; i < call.args.size(); ++i) {
        if (i) oss << ", ";
        Dump(call.args[i]);
      }
      oss << ")";
    }
  }

  void Dump(const IRPtr& ir) {
    if (!ir) { oss << "null"; return; }
    std::visit([this](const auto& node) { DumpNode(node); }, ir->node);
  }

  void DumpNode(const IROpaque&) { oss << "nop"; }

  void DumpNode(const IRSeq& s) {
    oss << "seq {\n";
    indent_level++;
    for (std::size_t i = 0; i < s.items.size(); ++i) {
      const auto& item = s.items[i];
      if (!item) {
        Indent(); oss << "null\n";
        continue;
      }

      if (auto call = std::get_if<IRCall>(&item->node)) {
        if (i + 1 < s.items.size()) {
          const auto& next = s.items[i + 1];
          if (next) {
            if (auto bind = std::get_if<IRBindVar>(&next->node)) {
              const auto& val = bind->value;
              if (val.kind == IRValue::Kind::Opaque &&
                  (val.name == "call_result" || val.name == "method_call_result" ||
                   val.name == "dyncall_result")) {
                Indent();
                oss << "bind %" << bind->name << " = ";
                DumpCall(*call);
                oss << "\n";
                i++;
                continue;
              }
            }
          }
        }
      }

      if (auto addr = std::get_if<IRAddrOf>(&item->node)) {
        if (i + 1 < s.items.size()) {
          const auto& next = s.items[i + 1];
          if (next) {
            if (auto bind = std::get_if<IRBindVar>(&next->node)) {
              const auto& val = bind->value;
              if (val.kind == IRValue::Kind::Opaque &&
                  NormalizeOpaqueName(val.name) == "addr_of" &&
                  ShouldCombineAddrOf(addr->place.repr)) {
                if (addr->result.kind == IRValue::Kind::Opaque && !addr->result.name.empty()) {
                  display_map[addr->result.name] = "addr_of";
                  if (!addr->place.repr.empty()) {
                    addr_place[addr->result.name] = addr->place.repr;
                  }
                }
                Indent();
                oss << "bind %" << bind->name << " = addr_of";
                if (!addr->place.repr.empty()) {
                  oss << " " << addr->place.repr;
                }
                oss << "\n";
                i++;
                continue;
              }
            }
          }
        }
      }

      if (auto alloc = std::get_if<IRAlloc>(&item->node)) {
        if (i + 1 < s.items.size()) {
          const auto& next = s.items[i + 1];
          if (next) {
            if (auto bind = std::get_if<IRBindVar>(&next->node)) {
              const auto& val = bind->value;
              if (val.kind == IRValue::Kind::Opaque && val.name == "alloc_ptr") {
                Indent();
                oss << "bind %" << bind->name << " = alloc";
                if (alloc->region.has_value()) {
                  oss << " in ";
                  Dump(*alloc->region);
                }
                oss << "\n";
                i++;
                continue;
              }
            }
          }
        }
      }

      Indent();
      Dump(item);
      oss << "\n";
    }
    indent_level--;
    Indent();
    oss << "}";
  }

  void DumpNode(const IRReturn& r) {
    oss << "ret ";
    Dump(r.value);
  }

  void DumpNode(const IRBindVar& b) {
    if (b.value.kind == IRValue::Kind::Opaque && !b.value.name.empty()) {
      const std::string norm = NormalizeOpaqueName(b.value.name);
      if (norm == "addr_of") {
        auto it = addr_place.find(b.value.name);
        if (it != addr_place.end() && ShouldCombineAddrOf(it->second)) {
          oss << "bind %" << b.name << " = addr_of";
          if (!it->second.empty()) {
            oss << " " << it->second;
          }
          return;
        }
      }
    }
    oss << "bind %" << b.name << " = ";
    Dump(b.value);
  }

  void DumpNode(const IRCall& c) { DumpCall(c); }

  void DumpNode(const IRCallVTable& c) {
    oss << "call_vtable ";
    Dump(c.base);
    oss << " [" << c.slot << "]";
    if (!c.args.empty()) {
      oss << " (";
      for (std::size_t i = 0; i < c.args.size(); ++i) {
        if (i) oss << ", ";
        Dump(c.args[i]);
      }
      oss << ")";
    }
  }

  void DumpNode(const IRStoreGlobal& s) {
    oss << "store @" << s.symbol << " = ";
    Dump(s.value);
  }

  void DumpNode(const IRReadVar& r) { oss << "read %" << r.name; }

  void DumpNode(const IRReadPath& r) {
    oss << "read @";
    for (const auto& part : r.path) {
      oss << part << "::";
    }
    oss << r.name;
  }

  void DumpNode(const IRStoreVar& s) {
    oss << "store %" << s.name << " = ";
    Dump(s.value);
  }

  void DumpNode(const IRStoreVarNoDrop& s) {
    oss << "store_nodrop %" << s.name << " = ";
    Dump(s.value);
  }

  void DumpNode(const IRReadPlace& r) { oss << "read_place " << r.place.repr; }

  void DumpNode(const IRWritePlace& w) {
    oss << "rec_update";
    if (!w.place.repr.empty()) {
      oss << " " << w.place.repr;
    }
  }

  void DumpNode(const IRAddrOf& a) {
    if (a.result.kind == IRValue::Kind::Opaque && !a.result.name.empty()) {
      display_map[a.result.name] = "addr_of";
      if (!a.place.repr.empty()) {
        addr_place[a.result.name] = a.place.repr;
      }
    }
    oss << "addr_of";
    if (!a.place.repr.empty()) {
      oss << " " << a.place.repr;
    }
  }

  void DumpNode(const IRReadPtr& r) {
    if (r.ptr.kind == IRValue::Kind::Opaque &&
        r.result.kind == IRValue::Kind::Opaque &&
        !r.ptr.name.empty() && !r.result.name.empty()) {
      auto it = addr_place.find(r.ptr.name);
      if (it != addr_place.end()) {
        if (IsSimpleIdent(it->second)) {
          display_map[r.result.name] = "%" + it->second;
        } else {
          display_map[r.result.name] = it->second;
        }
      }
    }
    oss << "load_ptr ";
    Dump(r.ptr);
  }

  void DumpNode(const IRWritePtr& w) {
    if (w.ptr.kind == IRValue::Kind::Opaque && !w.ptr.name.empty()) {
      auto it = addr_place.find(w.ptr.name);
      if (it != addr_place.end()) {
        oss << "rec_update";
        if (!it->second.empty()) {
          oss << " " << it->second;
        }
        return;
      }
    }
    oss << "store_ptr ";
    Dump(w.ptr);
    oss << " = ";
    Dump(w.value);
  }

  void DumpNode(const IRUnaryOp& op) {
    oss << "unop " << op.op << " ";
    Dump(op.operand);
  }

  void DumpNode(const IRFence& fence) {
    oss << "fence ";
    switch (fence.order) {
      case IRFenceOrder::Acquire:
        oss << "acquire";
        break;
      case IRFenceOrder::Release:
        oss << "release";
        break;
      case IRFenceOrder::SeqCst:
        oss << "seqcst";
        break;
    }
  }

  void DumpNode(const IRBinaryOp& op) {
    oss << "binop " << op.op << " ";
    Dump(op.lhs);
    oss << ", ";
    Dump(op.rhs);
  }

  void DumpNode(const IRCast& c) {
    oss << "cast ";
    Dump(c.value);
    oss << " to " << (c.target ? analysis::TypeToString(c.target) : "<unknown>");
  }

  void DumpNode(const IRTransmute& t) {
    oss << "transmute ";
    Dump(t.value);
    oss << " to " << (t.to ? analysis::TypeToString(t.to) : "<unknown>");
  }

  void DumpNode(const IRCheckIndex& c) {
    oss << "check_index ";
    Dump(c.base);
    oss << ", ";
    Dump(c.index);
  }

  void DumpNode(const IRCheckRange& c) {
    oss << "check_range ";
    Dump(c.base);
    oss << ", ";
    DumpRange(c.range);
  }

  void DumpNode(const IRCheckSliceLen& c) {
    oss << "check_slice_len ";
    Dump(c.base);
    oss << ", ";
    DumpRange(c.range);
    oss << ", ";
    Dump(c.value);
  }

  void DumpNode(const IRCheckOp& c) {
    oss << "check_op " << c.reason << " " << c.op << " ";
    Dump(c.lhs);
    if (c.rhs.has_value()) {
      oss << ", ";
      Dump(*c.rhs);
    }
  }

  void DumpNode(const IRCheckCast& c) {
    oss << "check_cast ";
    Dump(c.value);
    oss << " to " << (c.target ? analysis::TypeToString(c.target) : "<unknown>");
  }

  void DumpNode(const IRAlloc& a) {
    oss << "alloc";
    if (a.region.has_value()) {
      oss << " in ";
      Dump(*a.region);
    }
    oss << " ";
    Dump(a.value);
  }

  void DumpNode(const IRContextBundleBuild& b) {
    oss << "context_bundle_build "
        << (b.target_type ? analysis::TypeToString(b.target_type) : "<unknown>");
  }

  void DumpNode(const IRResult& r) {
    oss << "result ";
    Dump(r.value);
  }

  void DumpNode(const IRBreak& b) {
    oss << "break";
    if (b.value.has_value()) {
      oss << " ";
      Dump(*b.value);
    }
  }

  void DumpNode(const IRContinue&) { oss << "continue"; }

  void DumpNode(const IRDefer&) { oss << "defer"; }

  void DumpNode(const IRMoveState& m) {
    oss << "move_state " << m.place.repr;
  }

  void DumpNode(const IRIf& i) {
    oss << "if ";
    Dump(i.cond);
    oss << " then ";
    Dump(i.then_ir);
    oss << " else ";
    Dump(i.else_ir);
  }

  void DumpNode(const IRBlock& b) {
    oss << "block ";
    Dump(b.setup);
    oss << " ";
    Dump(b.body);
  }

  void DumpNode(const IRLoop& l) {
    oss << "loop";
    switch (l.kind) {
      case IRLoopKind::Infinite:
        oss << " infinite {\n";
        indent_level++;
        Indent();
        oss << "body ";
        Dump(l.body_ir);
        oss << "\n";
        Indent();
        oss << "value ";
        Dump(l.body_value);
        oss << "\n";
        indent_level--;
        Indent();
        oss << "}";
        break;
      case IRLoopKind::Conditional:
        oss << " while {\n";
        indent_level++;
        Indent();
        oss << "cond_ir ";
        Dump(l.cond_ir);
        oss << "\n";
        if (l.cond_value.has_value()) {
          Indent();
          oss << "cond_value ";
          Dump(*l.cond_value);
          oss << "\n";
        }
        Indent();
        oss << "body ";
        Dump(l.body_ir);
        oss << "\n";
        Indent();
        oss << "body_value ";
        Dump(l.body_value);
        oss << "\n";
        indent_level--;
        Indent();
        oss << "}";
        break;
      case IRLoopKind::Iter:
        oss << " iter {\n";
        indent_level++;
        if (l.iter_ir) {
          Indent();
          oss << "iter_ir ";
          Dump(l.iter_ir);
          oss << "\n";
        }
        if (l.iter_value.has_value()) {
          Indent();
          oss << "iter_value ";
          Dump(*l.iter_value);
          oss << "\n";
        }
        Indent();
        oss << "body ";
        Dump(l.body_ir);
        oss << "\n";
        Indent();
        oss << "body_value ";
        Dump(l.body_value);
        oss << "\n";
        indent_level--;
        Indent();
        oss << "}";
        break;
    }
  }

  void DumpNode(const IRIfCase& m) {
    oss << "if_case ";
    Dump(m.scrutinee);
    oss << " {\n";
    indent_level++;
    for (const auto& arm : m.arms) {
      Indent();
      oss << "case_clause {\n";
      indent_level++;
      if (arm.pattern) {
        Indent();
        oss << "pat ";
        DumpPattern(*arm.pattern);
        oss << "\n";
      }
      Indent();
      oss << "body {\n";
      indent_level++;
      Dump(arm.body);
      indent_level--;
      Indent();
      oss << "}\n";
      Indent();
      oss << "value ";
      Dump(arm.value);
      oss << "\n";
      if (arm.cleanup_ir && !std::holds_alternative<IROpaque>(arm.cleanup_ir->node)) {
        Indent();
        oss << "cleanup {\n";
        indent_level++;
        Dump(arm.cleanup_ir);
        indent_level--;
        Indent();
        oss << "}\n";
      }
      indent_level--;
      Indent();
      oss << "}\n";
    }
    indent_level--;
    Indent();
    oss << "}";
  }

  void DumpNode(const IRRegion& r) {
    oss << "region ";
    Dump(r.owner);
  }

  void DumpNode(const IRFrame& f) {
    oss << "frame";
    if (f.region.has_value()) {
      oss << " in ";
      Dump(*f.region);
    }
  }

  void DumpNode(const IRBranch& b) {
    if (b.cond.has_value()) {
      oss << "br_if ";
      Dump(*b.cond);
      oss << " " << b.true_label << " " << b.false_label;
    } else {
      oss << "br " << b.true_label;
    }
  }

  void DumpNode(const IRPhi& p) {
    oss << "phi " << (p.type ? analysis::TypeToString(p.type) : "<unknown>");
  }

  void DumpNode(const IRClearPanic&) { oss << "clear_panic"; }

  void DumpNode(const IRPanicCheck&) { oss << "panic_check"; }

  void DumpNode(const IRCleanupPanicCheck& h) {
    oss << "cleanup_panic_check";
    if (h.cleanup_ir) {
      oss << " {\n";
      indent_level++;
      Dump(h.cleanup_ir);
      indent_level--;
      Indent();
      oss << "}";
    }
  }

  void DumpNode(const IRInitPanicHandle& h) {
    oss << "init_panic_handle " << h.module;
    if (!h.poison_modules.empty()) {
      oss << " [";
      for (std::size_t i = 0; i < h.poison_modules.size(); ++i) {
        if (i) oss << ", ";
        oss << h.poison_modules[i];
      }
      oss << "]";
    }
    if (h.cleanup_ir) {
      oss << " cleanup";
    }
  }

  void DumpNode(const IRInitPanicRaise& h) {
    oss << "init_panic_raise " << h.module;
    if (!h.poison_modules.empty()) {
      oss << " [";
      for (std::size_t i = 0; i < h.poison_modules.size(); ++i) {
        if (i) oss << ", ";
        oss << h.poison_modules[i];
      }
      oss << "]";
    }
    if (h.cleanup_ir) {
      oss << " cleanup";
    }
  }

  void DumpNode(const IRCheckPoison& c) {
    oss << "check_poison " << c.module;
  }

  void DumpNode(const IRLowerPanic& p) { oss << "panic " << p.reason; }

  void DumpNode(const IRParallel& par) {
    oss << "parallel {\n";
    indent_level++;
    Indent(); oss << "domain: "; Dump(par.domain); oss << "\n";
    Indent(); oss << "body:\n";
    indent_level++;
    Indent(); Dump(par.body); oss << "\n";
    indent_level--;
    indent_level--;
    Indent(); oss << "}";
  }

  void DumpNode(const IRSpawn& spawn) {
    oss << "spawn {\n";
    indent_level++;
    if (spawn.captured_env) {
      Indent(); oss << "captured_env:\n";
      indent_level++;
      Indent(); Dump(spawn.captured_env); oss << "\n";
      indent_level--;
    }
    Indent(); oss << "body:\n";
    indent_level++;
    Indent(); Dump(spawn.body); oss << "\n";
    indent_level--;
    Indent(); oss << "body_result: "; Dump(spawn.body_result); oss << "\n";
    Indent(); oss << "result: "; Dump(spawn.result); oss << "\n";
    if (spawn.affinity_mask.has_value()) {
      Indent(); oss << "affinity: "; Dump(*spawn.affinity_mask); oss << "\n";
    }
    if (spawn.priority.has_value()) {
      Indent(); oss << "priority: "; Dump(*spawn.priority); oss << "\n";
    }
    if (!spawn.name.empty()) {
      Indent(); oss << "name: " << spawn.name << "\n";
    }
    indent_level--;
    Indent(); oss << "}";
  }

  void DumpNode(const IRWait& wait) {
    oss << "wait ";
    Dump(wait.handle);
    oss << " -> ";
    Dump(wait.result);
  }

  void DumpNode(const IRCancelCheck& check) {
    oss << "cancel_check ";
    Dump(check.token);
    oss << " -> ";
    Dump(check.result);
  }

  void DumpNode(const IRCancelSuppress&) { oss << "cancel_suppress"; }

  void DumpNode(const IRDispatch& dispatch) {
    oss << "dispatch {\n";
    indent_level++;
    Indent(); oss << "range: "; Dump(dispatch.range); oss << "\n";
    if (dispatch.ordered) {
      Indent(); oss << "ordered: true\n";
    }
    if (dispatch.chunk_size.has_value()) {
      Indent(); oss << "chunk_size: "; Dump(*dispatch.chunk_size); oss << "\n";
    }
    if (dispatch.reduce_op.has_value()) {
      Indent(); oss << "reduce_op: " << *dispatch.reduce_op << "\n";
    }
    Indent(); oss << "body:\n";
    indent_level++;
    Indent(); Dump(dispatch.body); oss << "\n";
    indent_level--;
    Indent(); oss << "body_result: "; Dump(dispatch.body_result); oss << "\n";
    Indent(); oss << "result: "; Dump(dispatch.result); oss << "\n";
    indent_level--;
    Indent(); oss << "}";
  }

  // Section 19.2.2 Yield expression
  void DumpNode(const IRYield& yield) {
    oss << "yield";
    if (yield.release) oss << " release";
    oss << " ";
    Dump(yield.value);
    oss << " [state=" << yield.state_index << "]";
    oss << " -> ";
    Dump(yield.result);
  }

  // Section 19.2.3 Yield-from expression
  void DumpNode(const IRYieldFrom& yf) {
    oss << "yield";
    if (yf.release) oss << " release";
    oss << " from ";
    Dump(yf.source);
    oss << " [state=" << yf.state_index << "]";
    oss << " -> ";
    Dump(yf.result);
  }

  void DumpNode(const IRSpecSnapshot& spec) {
    oss << "spec_snapshot paths=[";
    for (std::size_t i = 0; i < spec.paths.size(); ++i) {
      if (i) oss << ", ";
      oss << spec.paths[i];
    }
    oss << "] -> ";
    Dump(spec.result);
  }

  void DumpNode(const IRSpecValidate& spec) {
    oss << "spec_validate paths=[";
    for (std::size_t i = 0; i < spec.paths.size(); ++i) {
      if (i) oss << ", ";
      oss << spec.paths[i];
    }
    oss << "] -> ";
    Dump(spec.result);
  }

  void DumpNode(const IRSpecCommit& spec) {
    oss << "spec_commit paths=[";
    for (std::size_t i = 0; i < spec.paths.size(); ++i) {
      if (i) oss << ", ";
      oss << spec.paths[i];
    }
    oss << "] value=";
    Dump(spec.value);
    oss << " -> ";
    Dump(spec.result);
  }

  void DumpNode(const IRSpecRetry& spec) {
    oss << "spec_retry -> ";
    Dump(spec.result);
  }

  void DumpNode(const IRSpecFallback& spec) {
    oss << "spec_fallback ";
    Dump(spec.body);
    oss << " -> ";
    Dump(spec.result);
  }

  void DumpNode(const IRSpecLoop& spec) {
    oss << "spec_loop {\n";
    indent_level++;
    Indent(); oss << "snapshot: "; Dump(spec.snapshot_ir); oss << "\n";
    Indent(); oss << "body: "; Dump(spec.body_ir); oss << "\n";
    Indent(); oss << "validate: "; Dump(spec.validate_ir); oss << "\n";
    Indent(); oss << "commit: "; Dump(spec.commit_ir); oss << "\n";
    Indent(); oss << "retry: "; Dump(spec.retry_ir); oss << "\n";
    Indent(); oss << "fallback: "; Dump(spec.fallback_ir); oss << "\n";
    Indent(); oss << "result: "; Dump(spec.result); oss << "\n";
    indent_level--;
    Indent(); oss << "}";
  }

  // Section 19.3.3 Sync expression
  void DumpNode(const IRSync& sync) {
    oss << "sync ";
    Dump(sync.async_value);
    oss << " -> ";
    Dump(sync.result);
  }

  // Section 19.3.4 Race expression (first-completion)
  void DumpNode(const IRRaceReturn& race) {
    oss << "race {\n";
    indent_level++;
    for (std::size_t i = 0; i < race.arms.size(); ++i) {
      const auto& arm = race.arms[i];
      Indent(); oss << "arm[" << i << "]:\n";
      indent_level++;
      Indent(); oss << "async_ir: "; Dump(arm.async_ir); oss << "\n";
      Indent(); oss << "async_value: "; Dump(arm.async_value); oss << "\n";
      Indent(); oss << "match_value: "; Dump(arm.match_value); oss << "\n";
      Indent(); oss << "handler_ir: "; Dump(arm.handler_ir); oss << "\n";
      Indent(); oss << "handler_result: "; Dump(arm.handler_result); oss << "\n";
      indent_level--;
    }
    Indent(); oss << "result: "; Dump(race.result); oss << "\n";
    indent_level--;
    Indent(); oss << "}";
  }

  // Section 19.3.4 Race expression (streaming)
  void DumpNode(const IRRaceYield& race) {
    oss << "race_yield {\n";
    indent_level++;
    for (std::size_t i = 0; i < race.arms.size(); ++i) {
      const auto& arm = race.arms[i];
      Indent(); oss << "arm[" << i << "]:\n";
      indent_level++;
      Indent(); oss << "async_ir: "; Dump(arm.async_ir); oss << "\n";
      Indent(); oss << "async_value: "; Dump(arm.async_value); oss << "\n";
      Indent(); oss << "handler_ir: "; Dump(arm.handler_ir); oss << "\n";
      Indent(); oss << "handler_result: "; Dump(arm.handler_result); oss << "\n";
      indent_level--;
    }
    Indent(); oss << "result: "; Dump(race.result); oss << "\n";
    indent_level--;
    Indent(); oss << "}";
  }

  // Section 19.3.5 All expression
  void DumpNode(const IRAll& all) {
    oss << "all {\n";
    indent_level++;
    for (std::size_t i = 0; i < all.async_irs.size(); ++i) {
      Indent(); oss << "async[" << i << "]:\n";
      indent_level++;
      Indent(); oss << "ir: "; Dump(all.async_irs[i]); oss << "\n";
      if (i < all.async_values.size()) {
        Indent(); oss << "value: "; Dump(all.async_values[i]); oss << "\n";
      }
      indent_level--;
    }
    Indent(); oss << "result: "; Dump(all.result); oss << "\n";
    indent_level--;
    Indent(); oss << "}";
  }

  void DumpNode(const IRAsyncComplete& async_complete) {
    oss << "async_complete value: ";
    Dump(async_complete.value);
    oss << " -> ";
    Dump(async_complete.result);
  }

  void DumpNode(const IRAsyncFail& async_fail) {
    oss << "async_fail value: ";
    Dump(async_fail.value);
    oss << " -> ";
    Dump(async_fail.result);
  }
};

}  // namespace

std::string DumpIR(const IRDecls& decls) {
  Dumper d;
  for (const auto& decl : decls) {
    std::visit([&d](const auto& item) {
      using T = std::decay_t<decltype(item)>;
      if constexpr (std::is_same_v<T, ProcIR>) {
        d.oss << "proc @" << item.symbol << " {\n";
        d.indent_level++;
        d.Indent(); d.Dump(item.body); d.oss << "\n";
        d.indent_level--;
        d.oss << "}\n\n";
      } else if constexpr (std::is_same_v<T, GlobalConst>) {
        d.oss << "global_const @" << item.symbol << " bytes=" << item.bytes.size() << "\n\n";
      } else if constexpr (std::is_same_v<T, GlobalZero>) {
        d.oss << "global_zero @" << item.symbol << " size=" << item.size << "\n\n";
      } else if constexpr (std::is_same_v<T, GlobalVTable>) {
        d.oss << "vtable @" << item.symbol
               << " size=" << item.header.size
               << " align=" << item.header.align
               << " drop=@" << item.header.drop_sym;
        if (!item.slots.empty()) {
          d.oss << " slots=[";
          for (std::size_t i = 0; i < item.slots.size(); ++i) {
            if (i) d.oss << ", ";
            d.oss << "@" << item.slots[i];
          }
          d.oss << "]";
        }
        d.oss << "\n\n";
      } else if constexpr (std::is_same_v<T, ExternProcIR>) {
        d.oss << "extern_proc @" << item.symbol;
        if (item.abi.has_value()) {
          d.oss << " \"" << *item.abi << "\"";
        }
        if (item.raw_dylib_library_name.has_value() &&
            item.raw_dylib_foreign_symbol.has_value()) {
          d.oss << " raw_dylib=\"" << *item.raw_dylib_library_name
                << "\" foreign=\"" << *item.raw_dylib_foreign_symbol << "\"";
          if (item.raw_dylib_catch_unwind) {
            d.oss << " unwind=catch";
          }
        }
        d.oss << "\n\n";
      } else {
        d.oss << "decl ...\n\n";
      }
    }, decl);
  }
  return d.oss.str();
}

std::string DumpIR(const IRPtr& ir) {
  Dumper d;
  d.Dump(ir);
  return d.oss.str();
}

}  // namespace ultraviolet::codegen
