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
        struct TextFileLocus {
            U32 newlines;
            U32 tabs;
            U32 characters;

            TextFileLocus() : newlines(0), tabs(0), characters(0) {
            }

            U32 lineNumber() const {
                return newlines + 1;
            }

            U32 column(U32 spacesPerTab = 4) const {
                return tabs * spacesPerTab + characters + 1;
            }

            std::string describe(U32 spacesPerTab = 4) const {
                return std::to_string(lineNumber()) + ":" + std::to_string(column(spacesPerTab));
            }
        };

        WASTPARSE_API bool parseModule(const char *string, Uptr stringLength, IR::Module &outModule);
    }
}
