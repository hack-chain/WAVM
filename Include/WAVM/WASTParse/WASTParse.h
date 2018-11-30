#pragma once

#include <string>
#include <vector>

#include "WAVM/Inline/BasicTypes.h"

namespace WAVM {
    namespace IR {
        struct Module;
    }
}

namespace WAVM {
    namespace WAST {
        // A location in a text file.
        struct TextFileLocus {
            std::string sourceLine;
            U32 newlines;
            U32 tabs;
            U32 characters;

            TextFileLocus() : newlines(0), tabs(0), characters(0) {}

            U32 lineNumber() const { return newlines + 1; }

            U32 column(U32 spacesPerTab = 4) const { return tabs * spacesPerTab + characters + 1; }

            std::string describe(U32 spacesPerTab = 4) const {
                return std::to_string(lineNumber()) + ":" + std::to_string(column(spacesPerTab));
            }
        };

        // A WAST parse error.
        struct Error {
            TextFileLocus locus;
            std::string message;
        };

        // Parse a module from a string. Returns true if it succeeds, and writes the module to
        // outModule. If it fails, returns false and appends a list of errors to outErrors.
        WASTPARSE_API bool parseModule(const char *string,
                                       Uptr stringLength,
                                       IR::Module &outModule);
    }
}
