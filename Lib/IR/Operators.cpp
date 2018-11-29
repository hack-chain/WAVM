#include "WAVM/IR/Operators.h"

using namespace WAVM;
using namespace WAVM::IR;

const char *IR::getOpcodeName(Opcode opcode) {
    switch (opcode) {
#define VISIT_OPCODE(encoding, name, nameString, Imm, ...)                                         \
    case Opcode::name: return nameString;
        ENUM_OPERATORS(VISIT_OPCODE)
#undef VISIT_OPCODE
        default:
            return "unknown";
    };
}

const IR::NonParametricOpSignatures &IR::getNonParametricOpSigs() {
    static const IR::NonParametricOpSignatures nonParametricOpSignatures = {
#define VISIT_OP(_1, _2, _3, _4, signature, ...) signature,
            ENUM_NONCONTROL_NONPARAMETRIC_OPERATORS(VISIT_OP)
#undef VISIT_OP
    };
    return nonParametricOpSignatures;
}
