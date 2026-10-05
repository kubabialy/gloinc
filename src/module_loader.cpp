#include "module_loader.h"
#include "parser.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace {
namespace fs = std::filesystem;
class ModuleLoader {
    const std::string &standard_directory;
    const std::shared_ptr<Diagnostics> &diagnostics;
    fs::path root_directory;
    std::unordered_map<std::string, std::shared_ptr<SourceModule>> cache;
    std::unordered_map<std::string, std::string> file_owners;
    std::vector<std::string> active;

    bool error(const ImportStatement &import, const std::string &message) {
        diagnostics->error(DiagnosticStage::Semantic, import.span, message);
        return false;
    }
    bool resolve(ImportStatement &import, const fs::path &parent, fs::path &target,
                 bool &directory, std::string &standard_name) {
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
            target = fs::path(standard_directory) / (standard_name + ".gloin");
        } else if (path.starts_with('#')) {
            if (path.size() < 2 || !lower(path[1]) ||
                !std::all_of(path.begin() + 2, path.end(), [&](char c) {
                    return lower(c) || (c >= '0' && c <= '9') || c == '_';
                }))
                return error(import, "Unsupported package path: " + path);
            import.module_name = path.substr(1);
            target = root_directory / "packages" / import.module_name;
            directory = true;
        } else if (path.starts_with("./") || path.starts_with("../")) {
            if (path.ends_with('/'))
                return error(import, "Local module path must omit the trailing slash: " + path);
            fs::path relative(path);
            if (!relative.extension().empty() && relative.extension() != ".gloin")
                return error(import, "Local module must use the .gloin extension: " + path);
            if (relative.extension() == ".gloin") {
                import.module_name = relative.stem().string();
                target = parent / relative;
            } else {
                auto file = relative;
                file += ".gloin";
                const auto candidate_file = parent / file;
                const auto candidate_directory = parent / relative;
                std::error_code directory_error;
                const bool has_directory = fs::is_directory(candidate_directory, directory_error);
                if (directory_error && directory_error != std::errc::no_such_file_or_directory)
                    return error(import, "Cannot inspect module path: " + path);
                directory = has_directory;
                import.module_name = relative.filename().string();
                target = has_directory ? candidate_directory : candidate_file;
            }
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

    bool load_file(ImportStatement &import, const fs::path &spelled, const fs::path &canonical,
                   const std::string &key) {
        if (auto found = cache.find(key); found != cache.end()) {
            import.module = found->second;
            return true;
        }
        if (active.size() >= 128)
            return error(import, "Module dependency depth exceeds 128 source units");
        if (auto owner = file_owners.find(key); owner != file_owners.end() && owner->second != key)
            return error(import, "Source file is already part of a directory module: " + key);
        auto buffer = llvm::MemoryBuffer::getFile(key);
        if (!buffer)
            return error(import, "Cannot read module '" + import.path +
                                     "': " + buffer.getError().message());
        GloinParser parser(Lexer((*buffer)->getBuffer().str(),
                                 spelled.lexically_normal().string(), diagnostics));
        auto parsed = parser.parse_checked_program();
        if (!parsed.success)
            return false;
        auto module = std::make_shared<SourceModule>();
        module->canonical_path = key;
        module->module_name = import.module_name;
        module->linkage_prefix = "gloin.module.local." + std::to_string(cache.size()) +
                                 "." + import.module_name;
        module->declarations = std::move(parsed.program);
        cache.emplace(key, module);
        file_owners[key] = key;
        active.push_back(key);
        if (!visit(module->declarations, canonical.parent_path()))
            return false;
        active.pop_back();
        import.module = std::move(module);
        return true;
    }

    bool load_directory(ImportStatement &import, const fs::path &canonical,
                        const std::string &key) {
        if (auto found = cache.find(key); found != cache.end()) {
            import.module = found->second;
            return true;
        }
        if (active.size() >= 128)
            return error(import, "Module dependency depth exceeds 128 source units");
        struct Member {
            fs::path path;
            fs::path canonical;
        };
        std::vector<Member> members;
        std::set<std::string> member_keys;
        std::error_code ec;
        fs::directory_iterator entries(canonical, ec), end;
        if (ec)
            return error(import, "Cannot scan module directory '" + import.path +
                                     "': " + ec.message());
        while (entries != end) {
            const auto entry = entries->path();
            if (entry.extension() == ".gloin") {
                auto file = fs::canonical(entry, ec);
                if (ec || !fs::is_regular_file(file, ec))
                    return error(import, "Directory module entry is not a regular file: " +
                                             entry.string());
                const auto file_key = file.string();
                if (!member_keys.insert(file_key).second)
                    return error(import, "Duplicate source file in module directory: " + file_key);
                if (std::find(active.begin(), active.end(), file_key) != active.end())
                    return error(import, "Import cycle through directory member: " + file_key);
                if (auto owner = file_owners.find(file_key);
                    owner != file_owners.end() && owner->second != key)
                    return error(import, "Source file is already imported outside directory module: " +
                                             file_key);
                members.push_back({entry, file});
            }
            entries.increment(ec);
            if (ec)
                return error(import, "Cannot scan module directory '" + import.path +
                                         "': " + ec.message());
        }
        if (members.empty())
            return error(import, "Module directory has no .gloin files: " + canonical.string());
        std::sort(members.begin(), members.end(), [](const Member &left, const Member &right) {
            const auto a = left.path.filename().string();
            const auto b = right.path.filename().string();
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                                [](char x, char y) {
                                                    return static_cast<unsigned char>(x) <
                                                           static_cast<unsigned char>(y);
                                                });
        });
        for (const auto &member : members)
            file_owners[member.canonical.string()] = key;
        auto module = std::make_shared<SourceModule>();
        module->canonical_path = key;
        module->module_name = import.module_name;
        module->linkage_prefix = "gloin.module.local." + std::to_string(cache.size()) +
                                 "." + import.module_name;
        cache.emplace(key, module);
        active.push_back(key);
        std::unordered_map<std::string, std::string> imported_aliases;
        std::set<std::string> imported_files;
        for (const auto &member : members) {
            const auto file_key = member.canonical.string();
            if (active.size() >= 128)
                return error(import, "Module dependency depth exceeds 128 source units");
            auto buffer = llvm::MemoryBuffer::getFile(file_key);
            if (!buffer)
                return error(import, "Cannot read directory module file: " + file_key + ": " +
                                         buffer.getError().message());
            GloinParser parser(Lexer((*buffer)->getBuffer().str(), member.path.string(), diagnostics));
            auto parsed = parser.parse_checked_program();
            if (!parsed.success)
                return false;
            active.push_back(file_key);
            if (!visit(parsed.program, member.canonical.parent_path()))
                return false;
            active.pop_back();
            for (auto &statement : parsed.program) {
                if (const auto *dependency = dynamic_cast<const ImportStatement *>(statement.get())) {
                    const auto &alias = dependency->module_name;
                    const auto &target = dependency->module->canonical_path;
                    if (auto found = imported_aliases.find(alias); found != imported_aliases.end()) {
                        if (found->second == target)
                            continue;
                        return error(*dependency, "Conflicting imports in module directory: " + alias);
                    }
                    if (!imported_files.insert(target).second)
                        return error(*dependency, "Duplicate import of source file: " + target);
                    imported_aliases.emplace(alias, target);
                }
                module->declarations.push_back(std::move(statement));
            }
        }
        active.pop_back();
        import.module = std::move(module);
        return true;
    }

    bool visit(std::vector<std::unique_ptr<Statement>> &program, const fs::path &parent) {
        std::set<std::string> aliases, files;
        for (auto &statement : program) {
            auto *import = dynamic_cast<ImportStatement *>(statement.get());
            if (!import)
                continue;
            fs::path target;
            bool directory = false;
            std::string standard_name;
            if (!resolve(*import, parent, target, directory, standard_name))
                return false;
            if (!aliases.insert(import->module_name).second)
                return error(*import,
                             "Duplicate import or module namespace '" + import->module_name + "'");
            std::error_code ec;
            const auto canonical = fs::canonical(target, ec);
            const bool valid = !ec && (directory ? fs::is_directory(canonical, ec)
                                                 : fs::is_regular_file(canonical, ec));
            if (ec || !valid)
                return error(*import, "Cannot load module '" + import->path + "' from " +
                                          target.string() + ": " +
                                          (ec ? ec.message() :
                                                (directory ? "not a directory" : "not a regular file")));
            const auto key = canonical.string();
            if (!files.insert(key).second)
                return error(*import, "Duplicate import of source file: " + key);
            if (auto cycle = std::find(active.begin(), active.end(), key); cycle != active.end()) {
                std::string chain;
                for (auto edge = cycle; edge != active.end(); ++edge)
                    chain += *edge + " -> ";
                return error(*import, "Import cycle: " + chain + key);
            }
            if (directory ? !load_directory(*import, canonical, key)
                          : !load_file(*import, target, canonical, key))
                return false;
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
        root_directory = root.parent_path();
        return visit(program, root.parent_path());
    }
};
} // namespace

bool load_modules(std::vector<std::unique_ptr<Statement>> &program, const std::string &filename,
                  const std::string &directory, const std::shared_ptr<Diagnostics> &diagnostics) {
    return ModuleLoader(directory, diagnostics).load(program, filename);
}
