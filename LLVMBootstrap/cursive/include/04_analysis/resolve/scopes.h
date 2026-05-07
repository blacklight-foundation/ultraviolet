#pragma once

#include <string_view>
#include <vector>

#include "04_analysis/typing/context.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

IdKey IdKeyOf(std::string_view s);
bool IdEq(std::string_view s1, std::string_view s2);

PathKey PathKeyOf(const ast::Path& path);
bool PathEq(const ast::Path& p, const ast::Path& q);

bool ScopeKey(const Scope& scope);

bool BytePrefix(std::string_view p, std::string_view s);
bool Prefix(std::string_view s, std::string_view p);

bool ReservedGen(std::string_view x);
bool ReservedModulePath(const ast::ModulePath& path);

const std::vector<std::string_view>& PrimTypeNames();
const std::vector<std::string_view>& SpecialTypeNames();
const std::vector<std::string_view>& AsyncTypeNames();

const std::vector<IdKey>& PrimTypeKeys();
const std::vector<IdKey>& SpecialTypeKeys();
const std::vector<IdKey>& AsyncTypeKeys();

bool KeywordKey(std::string_view idkey);

Scope UniverseBindings();

}  // namespace cursive::analysis
