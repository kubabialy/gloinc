#ifndef GLOINC_DIAGNOSTICS_H
#define GLOINC_DIAGNOSTICS_H

#include <algorithm>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

struct SourceFile {
    std::string name;
    std::string text;
    std::vector<size_t> line_starts{0};

    SourceFile(std::string name, std::string text) : name(std::move(name)), text(std::move(text)) {
        for (size_t i = 0; i < this->text.size(); ++i)
            if (this->text[i] == '\n')
                line_starts.push_back(i + 1);
    }

    std::pair<size_t, size_t> line_column(size_t offset) const {
        offset = std::min(offset, text.size());
        auto it = std::upper_bound(line_starts.begin(), line_starts.end(), offset);
        size_t line = static_cast<size_t>(it - line_starts.begin());
        return {line, offset - line_starts[line - 1] + 1};
    }
};

// Half-open byte offsets. Shared ownership keeps diagnostics and ASTs valid after parsing.
struct SourceSpan {
    std::shared_ptr<const SourceFile> source;
    size_t begin = 0;
    size_t end = 0;
};

enum class DiagnosticStage { Lexing, Parsing, Semantic, Codegen, Execution };

struct Diagnostic {
    DiagnosticStage stage;
    SourceSpan span;
    std::string message;
};

class Diagnostics {
  public:
    void error(DiagnosticStage stage, SourceSpan span, std::string message) {
        if (message.starts_with("Error: "))
            message.erase(0, 7);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
            message.pop_back();
        entries.push_back({stage, std::move(span), std::move(message)});
    }

    bool has_errors() const { return !entries.empty(); }
    const std::vector<Diagnostic> &all() const { return entries; }
    void render(std::ostream &out) const {
        for (const auto &entry : entries) {
            auto [line, column] = entry.span.source
                                      ? entry.span.source->line_column(entry.span.begin)
                                      : std::pair<size_t, size_t>{1, 1};
            out << (entry.span.source ? entry.span.source->name : "<unknown>") << ':' << line << ':'
                << column << ": error: " << entry.message << '\n';
        }
    }

  private:
    std::vector<Diagnostic> entries;
};

template <typename... Args> std::string diagnostic_text(const Args &...args) {
    std::ostringstream out;
    (out << ... << args);
    return out.str();
}

// Nested visitors restore their parent's diagnostic location after visiting a child.
class DiagnosticScope {
  public:
    DiagnosticScope(SourceSpan &current, const SourceSpan &next)
        : current(current), saved(current) {
        if (next.source)
            current = next;
    }
    ~DiagnosticScope() { current = std::move(saved); }

  private:
    SourceSpan &current;
    SourceSpan saved;
};

#endif
