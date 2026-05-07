#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "llvm/Support/JSON.h"

#include "01_project/target_profile.h"
#include "06_driver/lsp/json_rpc.h"
#include "06_driver/tooling/analysis.h"
#include "06_driver/tooling/document_store.h"

namespace cursive::driver::lsp {

struct LspServerOptions {
  std::optional<std::filesystem::path> log_file;
  std::optional<project::TargetProfile> target_profile;
};

class LspServer {
 public:
  explicit LspServer(LspServerOptions options = {});
  ~LspServer();
  int Run();

 private:
  struct ProjectSnapshot {
    tooling::ToolingAnalysisOptions options;
    tooling::AnalysisSnapshot snapshot;
  };
  struct PendingAnalysis {
    std::string root_key;
    std::uint64_t generation = 0;
    tooling::ToolingAnalysisOptions options;
    std::vector<tooling::DocumentOverlay> overlays;
  };

  void HandleMessage(const llvm::json::Object& message);
  void HandleRequest(const llvm::json::Object& message,
                     const llvm::json::Value& id,
                     llvm::StringRef method);
  void HandleNotification(const llvm::json::Object& message,
                          llvm::StringRef method);

  llvm::json::Value HandleInitialize(const llvm::json::Object* params);
  llvm::json::Value HandleDocumentSymbol(const llvm::json::Object* params);
  llvm::json::Value HandleHover(const llvm::json::Object* params);
  llvm::json::Value HandleDefinition(const llvm::json::Object* params);
  llvm::json::Value HandleSemanticTokens(const llvm::json::Object* params);
  llvm::json::Value HandleWorkspaceSymbol(const llvm::json::Object* params);
  llvm::json::Value HandleDocumentHighlight(const llvm::json::Object* params);
  llvm::json::Value HandleReferences(const llvm::json::Object* params);
  llvm::json::Value HandleCompletion(const llvm::json::Object* params);
  llvm::json::Value HandleCodeAction(const llvm::json::Object* params);
  llvm::json::Value HandlePrepareRename(const llvm::json::Object* params);
  llvm::json::Value HandleRename(const llvm::json::Object* params);
  llvm::json::Value HandleSignatureHelp(const llvm::json::Object* params);
  llvm::json::Value HandleInlayHint(const llvm::json::Object* params);

  void DidOpen(const llvm::json::Object* params);
  void DidChange(const llvm::json::Object* params);
  void DidSave(const llvm::json::Object* params);
  void DidClose(const llvm::json::Object* params);
  void DidChangeWatchedFiles();

  void AnalyzeAndPublish();
  void ScheduleProjectAnalysis(const std::filesystem::path& project_root);
  void ScheduleProjectAnalysisForPath(const std::filesystem::path& path);
  void AnalysisWorkerLoop();
  void StopAnalysisWorker();
  void PublishDiagnosticsForOverlays(
      const std::vector<tooling::DocumentOverlay>& overlays,
      const tooling::AnalysisSnapshot& snapshot);
  void PublishDiagnosticsForOpenDocuments();
  void PublishDiagnosticsForUri(const std::string& uri);
  void PublishNoManifestDiagnostic(const std::string& uri,
                                   const std::filesystem::path& path);
  void SendResponse(const llvm::json::Value& id, llvm::json::Value result);
  void SendError(const llvm::json::Value& id, int code, std::string message);
  void SendNotification(std::string method, llvm::json::Value params);
  void Log(std::string_view message);

  std::optional<std::filesystem::path> PathFromTextDocument(
      const llvm::json::Object* params) const;
  std::string TextForPath(const std::filesystem::path& path) const;
  std::optional<std::filesystem::path> ManifestRootForPath(
      const std::filesystem::path& path) const;
  std::vector<tooling::DocumentOverlay> OverlaysForRoot(
      const std::filesystem::path& root) const;
  std::shared_ptr<const ProjectSnapshot> SnapshotForPath(
      const std::filesystem::path& path) const;
  std::vector<std::shared_ptr<const ProjectSnapshot>> ProjectSnapshots() const;

  LspServerOptions server_options_;
  std::ofstream log_;
  StdioJsonRpc rpc_;
  tooling::DocumentStore documents_;
  std::vector<std::filesystem::path> workspace_roots_;
  std::unordered_map<std::string, std::shared_ptr<const ProjectSnapshot>>
      projects_by_root_;
  mutable std::mutex state_mutex_;
  std::mutex output_mutex_;
  std::mutex analysis_mutex_;
  std::condition_variable analysis_cv_;
  std::deque<PendingAnalysis> pending_analyses_;
  std::unordered_map<std::string, std::uint64_t> analysis_generations_;
  std::thread analysis_worker_;
  bool analysis_worker_stop_ = false;
  bool shutdown_requested_ = false;
  bool running_ = true;
};

}  // namespace cursive::driver::lsp
