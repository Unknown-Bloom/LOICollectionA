#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "LOICollectionA/frontend/Callback.h"

using namespace LOICollection::frontend;

namespace {
    using Overloads = std::set<std::string>;

    struct FunctionGroup {
        std::map<std::string, Overloads> functions;
    };

    struct ClassGroup {
        std::set<std::string> fields;
        std::set<std::string> staticFields;
        Overloads constructors;
        std::map<std::string, Overloads> methods;
        std::map<std::string, Overloads> staticMethods;
        std::map<std::string, Overloads> valueMethods;
        std::set<std::string> operators;
    };

    const std::set<std::string> functionNamespaces = {
        "math", "std", "storage", "playerdata"
    };

    const std::set<std::string> classNames = {
        "Array", "CtxValue", "GlobalValue", "Map",
        "ObservableBoolean", "ObservableNumber", "ObservableString",
        "Proc", "String"
    };

    std::string typeName(int type) {
        switch (static_cast<ParamType>(type)) {
            case ParamType::INT: return "int";
            case ParamType::FLOAT: return "float";
            case ParamType::STRING: return "string";
            case ParamType::BOOL: return "bool";
            case ParamType::OBJECT: return "object";
            case ParamType::FUNCTION: return "function";
            case ParamType::ARRAY: return "array";
        }

        return "?";
    }

    std::string parameters(const std::string& encoded) {
        const std::size_t name = encoded.find('/');
        const std::size_t marker = name == std::string::npos
            ? std::string::npos
            : encoded.find('/', name + 1);

        std::string out;
        if (marker == std::string::npos)
            return out;

        const std::string tail = encoded.substr(marker + 1);
        const std::size_t colon = tail.find(':');
        if (colon == std::string::npos)
            return out;

        std::istringstream types(tail.substr(colon + 1));
        std::string part;
        while (std::getline(types, part, ':')) {
            if (part.empty())
                continue;

            if (!out.empty())
                out += ", ";
            out += typeName(std::stoi(part));
        }

        return out;
    }

    std::string signatureName(const std::string& encoded) {
        const std::size_t slash = encoded.find('/');
        return slash == std::string::npos ? encoded : encoded.substr(0, slash);
    }

    std::string cell(const std::string& text) {
        return text.empty() ? "-" : "`" + text + "`";
    }

    std::string row(const std::string& kind, const std::string& member, const std::string& params) {
        return "| " + kind + " | `" + member + "` | " + cell(params) + " |\n";
    }

    std::string renderFunctions(bool chinese) {
        std::map<std::string, FunctionGroup> groups;
        std::istringstream lines(FunctionCall::getInstance().exportShape());
        std::string line;

        while (std::getline(lines, line)) {
            const std::size_t sep = line.find("::");
            if (sep == std::string::npos)
                continue;

            const std::string namespaces = line.substr(0, sep);
            if (!functionNamespaces.contains(namespaces))
                continue;

            const std::string rest = line.substr(sep + 2);
            groups[namespaces].functions[signatureName(rest)].insert(parameters(rest));
        }

        if (groups.empty())
            return {};

        std::string out = chinese ? "## 函数\n\n" : "## Functions\n\n";
        for (const auto& [namespaces, group] : groups) {
            out += "### `" + namespaces + "`\n\n";
            out += chinese ? "| 函数 | 参数 |\n| --- | --- |\n" : "| Function | Parameters |\n| --- | --- |\n";

            for (const auto& [name, overloads] : group.functions)
                for (const auto& params : overloads)
                    out += "| `" + name + "` | " + cell(params) + " |\n";

            out += '\n';
        }

        return out;
    }

    std::string renderClasses(bool chinese) {
        std::map<std::string, ClassGroup> groups;
        std::istringstream lines(ClassCall::getInstance().exportShape());
        std::string line;

        while (std::getline(lines, line)) {
            std::istringstream parts(line);
            std::string kind;
            parts >> kind;

            if (kind == "field" || kind == "staticField") {
                std::string qualified;
                parts >> qualified;

                const std::size_t dot = qualified.find('.');
                if (dot == std::string::npos)
                    continue;

                const std::string className = qualified.substr(0, dot);
                if (!classNames.contains(className))
                    continue;

                const std::string field = qualified.substr(dot + 1);
                if (kind == "field")
                    groups[className].fields.insert(field);
                else
                    groups[className].staticFields.insert(field);

                continue;
            }

            if (kind == "operator") {
                std::string className;
                std::string op;
                parts >> className >> op;

                if (classNames.contains(className))
                    groups[className].operators.insert(op);

                continue;
            }

            std::string className;
            std::string encoded;
            parts >> className >> encoded;

            if (!classNames.contains(className))
                continue;

            if (kind == "constructor")
                groups[className].constructors.insert(parameters(encoded));
            else if (kind == "method")
                groups[className].methods[signatureName(encoded)].insert(parameters(encoded));
            else if (kind == "staticMethod")
                groups[className].staticMethods[signatureName(encoded)].insert(parameters(encoded));
            else if (kind == "valueMethod")
                groups[className].valueMethods[signatureName(encoded)].insert(parameters(encoded));
        }

        if (groups.empty())
            return {};

        std::string out = chinese ? "## 原生类\n\n" : "## Native Classes\n\n";
        for (const auto& [className, group] : groups) {
            out += "### `" + className + "`\n\n";

            if (!group.fields.empty() || !group.staticFields.empty()) {
                if (!group.fields.empty()) {
                    out += chinese ? "字段：" : "Fields: ";
                    bool first = true;
                    for (const auto& field : group.fields) {
                        if (!first)
                            out += ", ";
                        out += "`" + field + "`";
                        first = false;
                    }
                    out += "\n\n";
                }

                if (!group.staticFields.empty()) {
                    out += chinese ? "静态字段：" : "Static fields: ";
                    bool first = true;
                    for (const auto& field : group.staticFields) {
                        if (!first)
                            out += ", ";
                        out += "`" + field + "`";
                        first = false;
                    }
                    out += "\n\n";
                }
            }

            if (!group.operators.empty()) {
                out += chinese ? "运算符：" : "Operators: ";
                bool first = true;
                for (const auto& op : group.operators) {
                    if (!first)
                        out += ", ";
                    out += "`" + op + "`";
                    first = false;
                }
                out += "\n\n";
            }

            out += chinese ? "| 类型 | 成员 | 参数 |\n| --- | --- | --- |\n"
                           : "| Kind | Member | Parameters |\n| --- | --- | --- |\n";

            const std::string constructor = chinese ? "构造函数" : "Constructor";
            for (const auto& params : group.constructors)
                out += row(constructor, "new", params);

            const std::string method = chinese ? "方法" : "Method";
            for (const auto& [name, overloads] : group.methods)
                for (const auto& params : overloads)
                    out += row(method, name, params);

            const std::string staticMethod = chinese ? "静态方法" : "Static method";
            for (const auto& [name, overloads] : group.staticMethods)
                for (const auto& params : overloads)
                    out += row(staticMethod, name, params);

            const std::string valueMethod = chinese ? "值方法" : "Value method";
            for (const auto& [name, overloads] : group.valueMethods)
                for (const auto& params : overloads)
                    out += row(valueMethod, name, params);

            out += '\n';
        }

        return out;
    }

    std::string renderDocument(bool chinese) {
        std::string out = chinese ? "# 核心原生 ABI\n\n" : "# Core Native ABI\n\n";

        out += chinese
            ? "> 由 `FunctionCall` / `ClassCall` 的 `exportShape()` 生成，请勿手工编辑。\n"
              "> 范围是跨平台的运行时核心（`math`、`std`、`storage`、`playerdata` 与核心值类）；"
              "平台命名空间与 Bedrock UI 类见各自的文档。\n\n"
            : "> Generated from `exportShape()` of `FunctionCall` / `ClassCall`; do not edit by hand.\n"
              "> Scope is the platform-neutral runtime core (`math`, `std`, `storage`, `playerdata`, "
              "and core value classes); platform namespaces and Bedrock UI classes are documented separately.\n\n";

        out += renderFunctions(chinese);
        out += renderClasses(chinese);

        return out;
    }

    std::filesystem::path documentPath(const char* name) {
        const std::filesystem::path source = __FILE__;
        if (source.is_absolute())
            return (source.parent_path() / "../../../docs" / name).lexically_normal();

        return std::filesystem::path("docs") / name;
    }

    std::string readDocument(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::string content{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
        return content;
    }
}

TEST(NativeAbiDocTest, RendersCoreFunctionSignatures) {
    const std::string document = renderDocument(false);

    EXPECT_NE(document.find("### `math`"), std::string::npos);
    EXPECT_NE(document.find("| `sqrt` | `int` |"), std::string::npos);
    EXPECT_NE(document.find("### `storage`"), std::string::npos);
    EXPECT_NE(document.find("| `get` | `string` |"), std::string::npos);
    EXPECT_EQ(document.find("### `mc`"), std::string::npos);
}

TEST(NativeAbiDocTest, RendersCoreClassMembers) {
    const std::string document = renderDocument(false);

    EXPECT_NE(document.find("### `String`"), std::string::npos);
    EXPECT_NE(document.find("| Value method | `charAt` | `int` |"), std::string::npos);
    EXPECT_NE(document.find("### `Map`"), std::string::npos);
    EXPECT_NE(document.find("Fields: `keys`"), std::string::npos);
    EXPECT_EQ(document.find("### `CustomForm`"), std::string::npos);
}

TEST(NativeAbiDocTest, MatchesGeneratedDocuments) {
    const std::string chinese = renderDocument(true);
    const std::string english = renderDocument(false);

    if (std::getenv("LOICC_WRITE_ABI_DOC") != nullptr) {
        const auto chinesePath = documentPath("dev/native-abi.md");
        const auto englishPath = documentPath("en/dev/native-abi.md");
        std::filesystem::create_directories(chinesePath.parent_path());
        std::filesystem::create_directories(englishPath.parent_path());
        std::ofstream(chinesePath, std::ios::binary) << chinese;
        std::ofstream(englishPath, std::ios::binary) << english;
        return;
    }

    EXPECT_EQ(readDocument(documentPath("dev/native-abi.md")), chinese);
    EXPECT_EQ(readDocument(documentPath("en/dev/native-abi.md")), english);
}
