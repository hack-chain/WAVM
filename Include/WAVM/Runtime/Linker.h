#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "WAVM/IR/Module.h"
#include "WAVM/IR/Types.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Runtime/Runtime.h"

namespace WAVM {
    namespace Runtime {
        // An abstract resolver: maps module+export name pairs to a Runtime::Object.
        struct Resolver {
            virtual bool resolve(const std::string &moduleName, const std::string &exportName, IR::ExternType type, Object *&outObject) = 0;
        };

        // Links a module using the given resolver, returning an array mapping import indices to
        // objects. If the resolver fails to resolve any imports, throws a LinkException.
        struct LinkResult {
            struct MissingImport {
                std::string moduleName;
                std::string exportName;
                IR::ExternType type;
            };

            std::vector<MissingImport> missingImports;
            ImportBindings resolvedImports;
            bool success;
        };

        RUNTIME_API LinkResult linkModule(const IR::Module &module, Resolver &resolver);
    }
}