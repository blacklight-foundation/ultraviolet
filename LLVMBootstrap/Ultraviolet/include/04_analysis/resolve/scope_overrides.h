#pragma once

#include <utility>

#include "04_analysis/typing/context.h"

namespace ultraviolet::analysis {

class ScopedScopesOverride final {
 public:
  ScopedScopesOverride(ScopeContext& ctx, ScopeList replacement)
      : ctx_(&ctx), saved_(std::move(ctx.scopes)) {
    ctx_->scopes = std::move(replacement);
  }

  ~ScopedScopesOverride() { Restore(); }

  ScopedScopesOverride(const ScopedScopesOverride&) = delete;
  ScopedScopesOverride& operator=(const ScopedScopesOverride&) = delete;

  ScopedScopesOverride(ScopedScopesOverride&& other) noexcept
      : ctx_(other.ctx_), saved_(std::move(other.saved_)) {
    other.ctx_ = nullptr;
  }

  ScopedScopesOverride& operator=(ScopedScopesOverride&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Restore();
    ctx_ = other.ctx_;
    saved_ = std::move(other.saved_);
    other.ctx_ = nullptr;
    return *this;
  }

 private:
  void Restore() {
    if (!ctx_) {
      return;
    }
    ctx_->scopes = std::move(saved_);
    ctx_ = nullptr;
  }

  ScopeContext* ctx_ = nullptr;
  ScopeList saved_;
};

class ScopedCurrentModuleOverride final {
 public:
  ScopedCurrentModuleOverride(ScopeContext& ctx, ast::ModulePath replacement)
      : ctx_(&ctx), saved_(std::move(ctx.current_module)) {
    ctx_->current_module = std::move(replacement);
  }

  ~ScopedCurrentModuleOverride() { Restore(); }

  ScopedCurrentModuleOverride(const ScopedCurrentModuleOverride&) = delete;
  ScopedCurrentModuleOverride& operator=(const ScopedCurrentModuleOverride&) =
      delete;

  ScopedCurrentModuleOverride(ScopedCurrentModuleOverride&& other) noexcept
      : ctx_(other.ctx_), saved_(std::move(other.saved_)) {
    other.ctx_ = nullptr;
  }

  ScopedCurrentModuleOverride& operator=(
      ScopedCurrentModuleOverride&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Restore();
    ctx_ = other.ctx_;
    saved_ = std::move(other.saved_);
    other.ctx_ = nullptr;
    return *this;
  }

 private:
  void Restore() {
    if (!ctx_) {
      return;
    }
    ctx_->current_module = std::move(saved_);
    ctx_ = nullptr;
  }

  ScopeContext* ctx_ = nullptr;
  ast::ModulePath saved_;
};

class ScopedLeadingScope final {
 public:
  explicit ScopedLeadingScope(ScopeContext& ctx) : ctx_(&ctx) {
    ctx_->scopes.insert(ctx_->scopes.begin(), Scope{});
  }

  ~ScopedLeadingScope() {
    if (!ctx_ || ctx_->scopes.empty()) {
      return;
    }
    ctx_->scopes.erase(ctx_->scopes.begin());
  }

  ScopedLeadingScope(const ScopedLeadingScope&) = delete;
  ScopedLeadingScope& operator=(const ScopedLeadingScope&) = delete;

  Scope& scope() { return ctx_->scopes.front(); }

 private:
  ScopeContext* ctx_ = nullptr;
};

inline ScopeList MakeProcLikeScopes(const ScopeContext& base, Scope proc_scope) {
  ScopeList scopes;
  const auto local_scopes = LocalScopes(base.scopes);
  scopes.reserve(local_scopes.size() + 3);
  for (const auto& local_scope : local_scopes) {
    scopes.push_back(local_scope);
  }
  scopes.push_back(std::move(proc_scope));
  scopes.push_back(ModuleScope(base.scopes));
  scopes.push_back(UniverseScope(base.scopes));
  return scopes;
}

}  // namespace ultraviolet::analysis
