#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

namespace LOICollection::frontend {
    struct SourceLocation {
        size_t line;
        size_t column;
        size_t offset;

        SourceLocation() : line(0), column(0), offset(0) {}
        SourceLocation(size_t l, size_t c, size_t o) : line(l), column(c), offset(o) {}
    };

    enum class DiagnosticLevel {
        Error,
        Warning,
        Note,
        Remark
    };

    struct Diagnostic {
        DiagnosticLevel level;
        SourceLocation loc;
        std::string message;
    };

    class DiagnosticEngine {
        std::vector<Diagnostic> diagnostics;

    public:
        void addError(const SourceLocation& loc, const std::string& msg) {
            diagnostics.push_back({ DiagnosticLevel::Error, loc, msg });
        }

        void addWarning(const SourceLocation& loc, const std::string& msg) {
            diagnostics.push_back({ DiagnosticLevel::Warning, loc, msg });
        }

        void addNote(const SourceLocation& loc, const std::string& msg) {
            diagnostics.push_back({ DiagnosticLevel::Note, loc, msg });
        }

        [[nodiscard]] const std::vector<Diagnostic>& all() const {
            return this->diagnostics;
        }

        [[nodiscard]] bool hasErrors() const {
            return std::ranges::any_of(diagnostics, [](const Diagnostic& d) -> bool {
                return d.level == DiagnosticLevel::Error;
            });
        }

        [[nodiscard]] bool hasWarnings() const {
            return std::ranges::any_of(diagnostics, [](const Diagnostic& d) -> bool {
                return d.level == DiagnosticLevel::Warning;
            });
        }

        [[nodiscard]] std::string getErrorMessage() const {
            std::ostringstream oss;
            for (const auto& d : diagnostics) {
                if (d.level == DiagnosticLevel::Error) {
                    oss << d.message;
                    if (d.loc.line > 0 || d.loc.column > 0)
                        oss << " (at line " << d.loc.line << ", col " << d.loc.column << ")";
                    oss << "; ";
                }
            }
            auto result = oss.str();
            if (!result.empty()) {
                result.pop_back();
                result.pop_back();
            }
            return result;
        }

        [[nodiscard]] std::string getWarningMessage() const {
            std::ostringstream oss;
            for (const auto& d : diagnostics) {
                if (d.level == DiagnosticLevel::Warning) {
                    oss << "Warning: " << d.message;
                    if (d.loc.line > 0 || d.loc.column > 0)
                        oss << " (at line " << d.loc.line << ", col " << d.loc.column << ")";
                    oss << "; ";
                }
            }
            auto result = oss.str();
            if (!result.empty()) {
                result.pop_back();
                result.pop_back();
            }
            return result;
        }

        void clear() {
            diagnostics.clear();
        }
    };
}
