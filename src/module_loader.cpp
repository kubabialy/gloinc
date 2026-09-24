#include "module_loader.h"
#include "parser.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <filesystem>
#include <set>
#include <unordered_map>

namespace {
namespace fs = std::filesystem;
class ModuleLoader {
    const std::string &standard_directory;
    const std::shared_ptr<Diagnostics> &diagnostics;
    std::unordered_map<std::string, std::shared_ptr<SourceModule>> cache;
    std::vector<std::string> active;

    bool error(const ImportStatement &import, const std::string &message) {
        diagnostics->error(DiagnosticStage::Semantic, import.span, message);
        return false;
    }
    bool resolve(ImportStatement &import, const fs::path &parent, fs::path &file,
                 std::string &standard_name) {
        const auto &path = import.path;
        if (path.find('\0') != std::string::npos)
            return error(import, "Module path cannot contain NUL");
        auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
        if (path.starts_with('@')) {
            if (path.size() < 2 || !lower(path[1]) ||
                !std::all_of(path.begin() + 2, path.end(), [&](char c) {
                    return lower(c) || (c >= '0' && c <= '9') || c == '_';
                }))
                return error(import, "Unsupported module path: " + path);
            standard_name = path.substr(1);
            import.module_name = standard_name;
            file = fs::path(standard_directory) / (standard_name + ".gloin");
        } else if (path.starts_with("./") || path.starts_with("../")) {
            fs::path relative(path);
            if (relative.extension().empty())
                relative += ".gloin";
            else if (relative.extension() != ".gloin")
                return error(import, "Local module must use the .gloin extension: " + path);
            import.module_name = relative.stem().string();
            file = parent / relative;
        } else {
            return error(import, "Unsupported module path: " + path);
        }
        Lexer alias(import.module_name);
        if (alias.next_token().type != GLOIN_TOKEN_IDENTIFIER ||
            alias.next_token().type != GLOIN_TOKEN_EOF)
            return error(import, "Module filename must provide a non-reserved identifier: " + path);
        return true;
    }

  public:
    ModuleLoader(const std::string &directory, const std::shared_ptr<Diagnostics> &diagnostics)
        : standard_directory(directory), diagnostics(diagnostics) {}

    bool visit(std::vector<std::unique_ptr<Statement>> &program, const fs::path &parent) {
        std::set<std::string> aliases, files;
        for (auto &statement : program) {
            auto *import = dynamic_cast<ImportStatement *>(statement.get());
            if (!import)
                continue;
            fs::path file;
            std::string standard_name;
            if (!resolve(*import, parent, file, standard_name))
                return false;
            if (!aliases.insert(import->module_name).second)
                return error(*import,
                             "Duplicate import or module namespace '" + import->module_name + "'");
            std::error_code ec;
            const auto canonical = fs::canonical(file, ec);
            if (ec || !fs::is_regular_file(canonical, ec))
                return error(*import, "Cannot load module '" + import->path + "' from " +
                                          file.string() + ": " +
                                          (ec ? ec.message() : "not a regular file"));
            const auto key = canonical.string();
            if (!files.insert(key).second)
                return error(*import, "Duplicate import of source file: " + key);
            if (auto cycle = std::find(active.begin(), active.end(), key); cycle != active.end()) {
                std::string chain;
                for (auto edge = cycle; edge != active.end(); ++edge)
                    chain += *edge + " -> ";
                return error(*import, "Import cycle: " + chain + key);
            }
            auto found = cache.find(key);
            if (found == cache.end()) {
                if (active.size() >= 128)
                    return error(*import, "Module dependency depth exceeds 128 files");
                auto buffer = llvm::MemoryBuffer::getFile(key);
                if (!buffer)
                    return error(*import, "Cannot read module '" + import->path +
                                              "': " + buffer.getError().message());
                GloinParser parser(Lexer((*buffer)->getBuffer().str(),
                                         file.lexically_normal().string(), diagnostics));
                auto parsed = parser.parse_checked_program();
                if (!parsed.success)
                    return false;
                auto module = std::make_shared<SourceModule>();
                module->canonical_path = key;
                module->module_name = import->module_name;
                module->linkage_prefix = "gloin.module.local." + std::to_string(cache.size()) +
                                         "." + import->module_name;
                module->declarations = std::move(parsed.program);
                cache.emplace(key, module);
                active.push_back(key);
                if (!visit(module->declarations, canonical.parent_path()))
                    return false;
                active.pop_back();
                import->module = std::move(module);
            } else {
                import->module = found->second;
            }
            if (!standard_name.empty()) {
                if (!import->module->standard_name.empty() &&
                    import->module->standard_name != standard_name)
                    return error(*import,
                                 "Distinct standard module names resolve to the same file");
                import->module->standard_name = standard_name;
                import->module->linkage_prefix = "gloin.module." + standard_name;
            }
        }
        return true;
    }

    bool load(std::vector<std::unique_ptr<Statement>> &program, const std::string &filename) {
        std::error_code ec;
        auto root = fs::absolute(filename, ec);
        if (!ec)
            root = fs::weakly_canonical(root, ec);
        if (ec) {
            diagnostics->error(DiagnosticStage::Semantic, {},
                               "Cannot resolve source path: " + ec.message());
            return false;
        }
        active.push_back(root.string());
        return visit(program, root.parent_path());
    }
};
} // namespace

bool load_modules(std::vector<std::unique_ptr<Statement>> &program, const std::string &filename,
                  const std::string &directory, const std::shared_ptr<Diagnostics> &diagnostics) {
    return ModuleLoader(directory, diagnostics).load(program, filename);
}
