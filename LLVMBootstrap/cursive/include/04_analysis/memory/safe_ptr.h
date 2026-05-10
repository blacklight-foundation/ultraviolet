#pragma once

#include <optional>

#include "04_analysis/typing/types.h"

namespace cursive::analysis {

void SpecDefsSafePtr();

const TypePtr* AsSafePtr(const TypeRef& type);
bool IsSafePtrType(const TypeRef& type);
std::optional<PtrState> PtrStateOf(const TypeRef& type);
TypeRef PtrElementType(const TypeRef& type);

}  // namespace cursive::analysis
