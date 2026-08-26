#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include "LOICollectionA/frontend/ComponentExpander.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"

#include "LOICollectionA/utils/core/Sha256.h"

#include "LOICollectionA/frontend/ScriptLoader.h"

namespace LOICollection::frontend {
    namespace {
        bool isTopLevelDefinition(ASTNode& node) {
            switch (node.getType()) {
                case ASTNode::Type::Class:
                case ASTNode::Type::FunctionDef:
                case ASTNode::Type::Using:
                case ASTNode::Type::Import:
                case ASTNode::Type::Component:
                    return true;
                default:
                    return false;
            }
        }

        const std::string* definitionName(ASTNode& node) {
            switch (node.getType()) {
                case ASTNode::Type::Class:
                    return &static_cast<ClassNode&>(node).name;
                case ASTNode::Type::FunctionDef:
                    return &static_cast<FunctionDefNode&>(node).name;
                case ASTNode::Type::Using:
                    return &static_cast<UsingNode&>(node).name;
                case ASTNode::Type::Component:
                    return &static_cast<ComponentNode&>(node).name;
                default:
                    return nullptr;
            }
        }

        SourceLocation definitionLoc(ASTNode& node) {
            switch (node.getType()) {
                case ASTNode::Type::Class:
                    return static_cast<ClassNode&>(node).loc;
                case ASTNode::Type::FunctionDef:
                    return static_cast<FunctionDefNode&>(node).loc;
                case ASTNode::Type::Using:
                    return static_cast<UsingNode&>(node).loc;
                case ASTNode::Type::Component:
                    return static_cast<ComponentNode&>(node).loc;
                default:
                    return {};
            }
        }

        std::string displayName(const std::string& path, const std::string& rootDir) {
            std::error_code ec;
            auto relative = std::filesystem::relative(path, rootDir, ec);
            if (!ec && !relative.empty() && relative.native()[0] != '.')
                return relative.generic_string();

            return std::filesystem::path(path).generic_string();
        }
    }

    std::optional<ScriptLoader::Result> ScriptLoader::load(
        const std::string& entryPath,
        const std::string& rootDir,
        const FileReader& readFile,
        DiagnosticEngine& diagnostics
    ) {
        std::unordered_map<std::string, std::unique_ptr<ProgramNode>> modules;
        std::vector<std::string> order;
        std::vector<std::string> hashes;
        std::vector<std::string> stack;

        std::function<bool(const std::string&, const SourceLocation&)> visit =
            [&](const std::string& path, const SourceLocation& importLoc) -> bool {
                if (std::find(order.begin(), order.end(), path) != order.end())
                    return true;

                if (auto cycleAt = std::find(stack.begin(), stack.end(), path); cycleAt != stack.end()) {
                    std::string cycle;
                    for (auto it = cycleAt; it != stack.end(); ++it)
                        cycle += displayName(*it, rootDir) + " -> ";
                    cycle += displayName(path, rootDir);

                    diagnostics.addError(importLoc, "Circular import detected: " + cycle);
                    return false;
                }

                auto content = readFile(path);
                if (!content) {
                    diagnostics.addError(importLoc, "Cannot read imported file: " + path);
                    return false;
                }

                Lexer lexer(*content, diagnostics);
                Parser parser(lexer, diagnostics);
                auto ast = parser.parse();
                if (!ast || ast->getType() != ASTNode::Type::Program || diagnostics.hasErrors())
                    return false;

                auto program = std::unique_ptr<ProgramNode>(static_cast<ProgramNode*>(ast.release()));

                stack.push_back(path);
                bool ok = true;
                for (auto& part : program->parts) {
                    if (part->getType() != ASTNode::Type::Import)
                        continue;

                    auto& import = static_cast<ImportNode&>(*part);

                    std::error_code ec;
                    auto resolved = (std::filesystem::path(rootDir) / import.path).lexically_normal();

                    ok = visit(resolved.string(), import.loc) && ok;
                }
                stack.pop_back();

                if (!ok)
                    return false;

                modules.emplace(path, std::move(program));
                order.push_back(path);
                hashes.push_back(utils::Sha256::compute(*content));
                return true;
            };

        if (!visit(entryPath, {}))
            return std::nullopt;

        for (auto& [path, program] : modules) {
            if (path == entryPath)
                continue;

            for (auto& part : program->parts) {
                if (!isTopLevelDefinition(*part)) {
                    diagnostics.addError({},
                        "Imported file '" + displayName(path, rootDir) +
                        "' must only contain top-level definitions (class/func/using/component)");
                    return std::nullopt;
                }
            }
        }

        std::unordered_map<std::string, std::vector<std::unique_ptr<ASTNode>>> expanded;
        std::unordered_set<std::string> merged;
        std::unordered_map<std::string, std::pair<std::string, size_t>> definedAt;

        for (auto& path : order) {
            auto& program = modules.at(path);
            std::vector<std::unique_ptr<ASTNode>> nodes;

            for (auto& part : program->parts) {
                if (part->getType() == ASTNode::Type::Import) {
                    auto& import = static_cast<ImportNode&>(*part);

                    std::error_code ec;
                    auto resolved = (std::filesystem::path(rootDir) / import.path).lexically_normal();

                    if (merged.insert(resolved.string()).second) {
                        auto& imported = expanded.at(resolved.string());
                        for (auto& node : imported)
                            nodes.push_back(std::move(node));
                        imported.clear();
                    }

                    continue;
                }

                if (const std::string* name = definitionName(*part)) {
                    SourceLocation loc = definitionLoc(*part);

                    auto existing = definedAt.find(*name);
                    if (existing != definedAt.end()) {
                        diagnostics.addError(loc,
                            "Definition '" + *name + "' from '" + displayName(path, rootDir) +
                            "' (line " + std::to_string(loc.line) +
                            ") conflicts with the one in '" + displayName(existing->second.first, rootDir) +
                            "' (line " + std::to_string(existing->second.second) + ")");
                        return std::nullopt;
                    }

                    definedAt.emplace(*name, std::make_pair(path, loc.line));
                }

                nodes.push_back(std::move(part));
            }

            expanded.emplace(path, std::move(nodes));
        }

        Result result;
        result.program = std::make_unique<ProgramNode>();
        result.program->parts = std::move(expanded.at(entryPath));
        result.files = std::move(order);
        result.hashes = std::move(hashes);

        if (!ComponentExpander::expand(*result.program, diagnostics))
            return std::nullopt;

        return result;
    }
}
