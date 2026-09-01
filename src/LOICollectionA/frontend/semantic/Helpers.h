#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Callback.h"

namespace LOICollection::frontend {
    TypeInfo typeFromParam(ParamType param);
    bool typeMatchesParam(const TypeInfo& type, ParamType param);
    bool matchesNativeSignature(const CallbackTypeArgs& signature, const std::vector<TypeInfo>& argTypes);
    TypeInfo arithmeticResult(const std::string& op, const TypeInfo& left, const TypeInfo& right);
    bool isNativeClass(const std::string& name);
    const std::unordered_map<std::string, TypeKind>& basicTypes();
    bool isReservedTypeName(const std::string& name);
    bool isWhitelistedFormClass(const std::string& name);
}
