#include "module_loader.h"
#include "parser.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <set>

bool load_standard_modules(std::vector<std::unique_ptr<Statement>> &program,
                           const std::string &directory,
                           const std::shared_ptr<Diagnostics> &diagnostics) {
    std::set<std::string> names;
    for (auto &statement : program) {
        auto *import = dynamic_cast<ImportStatement *>(statement.get());
        if (!import)
            continue;
        const auto &path = import->path;
        auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
        if (path.size() < 2 || path[0] != '@' || !lower(path[1]) ||
            !std::all_of(path.begin() + 2, path.end(),
                         [&](char c) { return lower(c) || (c >= '0' && c <= '9') || c == '_'; })) {
            diagnostics->error(DiagnosticStage::Semantic, import->span,
                               "Unsupported module path: " + path);
            return false;
        }
        import->module_name = path.substr(1);
        if (!names.insert(import->module_name).second) {
            diagnostics->error(DiagnosticStage::Semantic, import->span,
                               "Duplicate import '" + path + "'");
            return false;
        }
        std::string filename = import->module_name;
        filename += ".gloin";
        llvm::SmallString<256> full_path(directory);
        llvm::sys::path::append(full_path, filename);
        llvm::sys::fs::file_status status;
        const auto error = llvm::sys::fs::status(full_path, status);
        if (error || !llvm::sys::fs::is_regular_file(status)) {
            diagnostics->error(DiagnosticStage::Semantic, import->span,
                               "Cannot load module '" + path + "' from " + full_path.str().str() +
                                   ": " + (error ? error.message() : "not a regular file"));
            return false;
        }
        auto buffer = llvm::MemoryBuffer::getFile(full_path);
        if (!buffer) {
            diagnostics->error(DiagnosticStage::Semantic, import->span,
                               "Cannot read module '" + path + "': " + buffer.getError().message());
            return false;
        }
        GloinParser parser(Lexer((*buffer)->getBuffer().str(), full_path.str().str(), diagnostics));
        auto parsed = parser.parse_checked_program();
        if (!parsed.success)
            return false;
        for (const auto &declaration : parsed.program) {
            // Dependencies between standard files require a module graph (SPEC-029).
            if (dynamic_cast<const ImportStatement *>(declaration.get())) {
                diagnostics->error(DiagnosticStage::Semantic, declaration->span,
                                   "Imports inside standard modules are not supported yet");
                return false;
            }
        }
        import->declarations = std::move(parsed.program);
        import->loaded = true;
    }
    return true;
}
