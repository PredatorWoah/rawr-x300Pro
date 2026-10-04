#pragma once
#include <mutex>
#include <string>

namespace rawrcam::diagnostics {
class DiagnosticSink {
   public:
    explicit DiagnosticSink(std::string filesDir);
    void initialize();
    void setEnabled(bool enabled);
    bool enabled() const noexcept { return enabled_; }
    void appendValidation(const std::string& line) const;
    void appendDcgSession(const std::string& line) const;
    void appendPipelineAudit(const std::string& line) const;

   private:
    std::string validationPath_;
    std::string dcgPath_;
    std::string pipelinePath_;
    mutable std::mutex validationMutex_;
    bool enabled_ = false;
};
}  // namespace rawrcam::diagnostics
