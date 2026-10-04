#include "DiagnosticSink.h"

#include <fstream>

namespace rawrcam::diagnostics {
namespace {
std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (char c : value) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}
void appendJson(const std::string& path, const char* type, const std::string& line) {
    std::ofstream out(path, std::ios::app);
    if (!out) return;
    out << "{\"type\":\"" << type << "\",\"message\":\"" << escapeJson(line) << "\"}\n";
}
}  // namespace
DiagnosticSink::DiagnosticSink(std::string filesDir)
    : validationPath_(filesDir + "/rawrcam_validation_current.txt"),
      dcgPath_(filesDir + "/rawrcam_dcg_session_audit.jsonl"),
      pipelinePath_(filesDir + "/rawrcam_post_session_pipeline_audit.jsonl") {}
void DiagnosticSink::initialize() {
    if (!enabled_) return;
    {
        std::ofstream out(dcgPath_, std::ios::trunc);
        if (out) out << "{\"type\":\"dcgAuditStart\",\"version\":1}\n";
    }
    {
        std::ofstream out(pipelinePath_, std::ios::trunc);
        if (out) out << "{\"type\":\"postSessionPipelineAuditStart\",\"version\":1}\n";
    }
    {
        std::ofstream out(validationPath_, std::ios::trunc);
        if (out) {
            out << "RAWRCAM_DIAGNOSTICS_VERSION=8\n";
            out << "camera_owner=NDK_CPP\n";
            out << "raw_ingress=vkCmdCopyImage_non_zero_copy\n";
            out << "purpose=event_triggered_transient_highlight_validation_before_stability\n";
        }
    }
}
void DiagnosticSink::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(validationMutex_);
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled_) return;
    {
        std::ofstream out(dcgPath_, std::ios::trunc);
        if (out) out << "{\"type\":\"dcgAuditStart\",\"version\":1}\n";
    }
    {
        std::ofstream out(pipelinePath_, std::ios::trunc);
        if (out) out << "{\"type\":\"postSessionPipelineAuditStart\",\"version\":1}\n";
    }
    {
        std::ofstream out(validationPath_, std::ios::trunc);
        if (out) out << "RAWRCAM_DIAGNOSTICS_VERSION=9\npersistent_diagnostics=enabled\n";
    }
}
void DiagnosticSink::appendValidation(const std::string& line) const {
    if (!enabled_) return;
    std::lock_guard<std::mutex> lock(validationMutex_);
    std::ofstream out(validationPath_, std::ios::app);
    if (out) out << line << "\n";
}
void DiagnosticSink::appendDcgSession(const std::string& line) const {
    if (enabled_) appendJson(dcgPath_, "cameraSessionDiagnostic", line);
}
void DiagnosticSink::appendPipelineAudit(const std::string& line) const {
    if (enabled_) appendJson(pipelinePath_, "postSessionPipelineAudit", line);
}
}  // namespace rawrcam::diagnostics
