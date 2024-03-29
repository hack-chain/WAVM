#pragma once

#include <string>

#include "WAVM/IR/IR.h"
#include "WAVM/IR/Operators.h"

namespace WAVM {
    namespace IR {
        struct FunctionDef;
        struct Module;

        // Since the data section occurs after the code section in binary modules, it's necessary to
        // defer some validation until it is loaded.
        struct DeferredCodeValidationState {
            Uptr requiredNumDataSegments = 0;
        };

        struct ValidationException {
            std::string message;

            ValidationException(std::string &&inMessage) : message(inMessage) {
            }
        };

        struct CodeValidationStreamImpl;

        struct CodeValidationStream {
            IR_API CodeValidationStream(const Module &module, const FunctionDef &function, DeferredCodeValidationState &deferredCodeValidationState);

            IR_API ~CodeValidationStream();

            IR_API void finish();

#define VISIT_OPCODE(_, name, nameString, Imm, ...) IR_API void name(Imm imm = {});

            ENUM_OPERATORS(VISIT_OPCODE)

#undef VISIT_OPCODE

        private:
            CodeValidationStreamImpl *impl;
        };

        template<typename InnerStream> struct CodeValidationProxyStream {
            CodeValidationProxyStream(const Module &module, const FunctionDef &function, InnerStream &inInnerStream, DeferredCodeValidationState &deferredCodeValidationState)
                    : codeValidationStream(module, function, deferredCodeValidationState), innerStream(inInnerStream) {
            }

            void finishValidation() {
                codeValidationStream.finish();
            }

#define VISIT_OPCODE(_, name, nameString, Imm, ...)                                                \
    void name(Imm imm = {})                                                                        \
    {                                                                                              \
        codeValidationStream.name(imm);                                                            \
        innerStream.name(imm);                                                                     \
    }

            ENUM_OPERATORS(VISIT_OPCODE)

#undef VISIT_OPCODE

        private:
            CodeValidationStream codeValidationStream;
            InnerStream &innerStream;
        };

        IR_API void validateTypes(const IR::Module &module);

        IR_API void validateImports(const IR::Module &module);

        IR_API void validateFunctionDeclarations(const IR::Module &module);

        IR_API void validateTableDefs(const IR::Module &module);

        IR_API void validateMemoryDefs(const IR::Module &module);

        IR_API void validateGlobalDefs(const IR::Module &module);

        IR_API void validateExceptionTypeDefs(const IR::Module &module);

        IR_API void validateExports(const IR::Module &module);

        IR_API void validateStartFunction(const IR::Module &module);

        IR_API void validateElemSegments(const IR::Module &module);

        IR_API void validateDataSegments(const IR::Module &module, const DeferredCodeValidationState &deferredCodeValidationState);

        inline void validatePreCodeSections(const IR::Module &module) {
            validateTypes(module);
            validateImports(module);
            validateFunctionDeclarations(module);
            validateTableDefs(module);
            validateMemoryDefs(module);
            validateGlobalDefs(module);
            validateExceptionTypeDefs(module);
            validateExports(module);
            validateStartFunction(module);
            validateElemSegments(module);
        }

        inline void validatePostCodeSections(const IR::Module &module, const DeferredCodeValidationState &deferredCodeValidationState) {
            validateDataSegments(module, deferredCodeValidationState);
        }
    }
}
