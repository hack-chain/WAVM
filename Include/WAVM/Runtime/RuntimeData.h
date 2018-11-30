#pragma once

#include <atomic>
#include <map>

#include "WAVM/IR/Value.h"
#include "WAVM/Inline/BasicTypes.h"

namespace WAVM {
    namespace LLVMJIT {
        struct Module;
    }
}

namespace WAVM {
    namespace Runtime {
        struct Compartment;
        struct Context;
        struct ExceptionType;
        struct Object;
        struct Table;
        struct Memory;

        enum class ObjectKind : U8 {
                    function = 0, table = 1, memory = 2, global = 3, exceptionType = 4,
                    moduleInstance = 5, context = 6, compartment = 7,

            invalid = 0xff,
        };
        static_assert(Uptr(IR::ExternKind::function) ==
                      Uptr(ObjectKind::function), "IR::ExternKind::function != ObjectKind::function");
        static_assert(
                Uptr(IR::ExternKind::table) == Uptr(ObjectKind::table), "IR::ExternKind::table != ObjectKind::table");
        static_assert(Uptr(IR::ExternKind::memory) ==
                      Uptr(ObjectKind::memory), "IR::ExternKind::memory != ObjectKind::memory");
        static_assert(Uptr(IR::ExternKind::global) ==
                      Uptr(ObjectKind::global), "IR::ExternKind::global != ObjectKind::global");
        static_assert(Uptr(IR::ExternKind::exceptionType) ==
                      Uptr(ObjectKind::exceptionType), "IR::ExternKind::exceptionType != ObjectKind::exceptionType");

#define compartmentReservedBytes (4ull * 1024 * 1024 * 1024)

        enum {
            maxThunkArgAndReturnBytes = 256,
            maxGlobalBytes = 4096 - maxThunkArgAndReturnBytes,
            maxMutableGlobals = maxGlobalBytes / sizeof(IR::UntaggedValue),
            maxMemories = 255,
            maxTables = (4096 - maxMemories * sizeof(void *) - sizeof(Compartment *)) / sizeof(void *),
            compartmentRuntimeDataAlignmentLog2 = 32
        };

        static_assert(sizeof(IR::UntaggedValue) * IR::maxReturnValues <=
                      maxThunkArgAndReturnBytes, "maxThunkArgAndReturnBytes must be large enough to hold IR::maxReturnValues * "
                                                 "sizeof(UntaggedValue)");

        struct ContextRuntimeData {
            U8 thunkArgAndReturnData[maxThunkArgAndReturnBytes];
            IR::UntaggedValue mutableGlobals[maxMutableGlobals];
        };

        static_assert(sizeof(ContextRuntimeData) == 4096, "");

        struct CompartmentRuntimeData {
            Compartment *compartment;
            void *memoryBases[maxMemories];
            void *tableBases[maxTables];
            ContextRuntimeData contexts[1];
        };

        enum {
            maxContexts = 1024 * 1024 - offsetof(CompartmentRuntimeData, contexts) / sizeof(ContextRuntimeData)
        };

        static_assert(offsetof(CompartmentRuntimeData, contexts) % 4096 ==
                      0, "CompartmentRuntimeData::contexts isn't page-aligned");
        static_assert(U64(offsetof(CompartmentRuntimeData, contexts)) + U64(maxContexts) * sizeof(ContextRuntimeData) ==
                      compartmentReservedBytes, "CompartmentRuntimeData isn't the expected size");

        struct ExceptionData {
            Uptr typeId;
            ExceptionType *type;
            U8 isUserException;
            IR::UntaggedValue arguments[1];

            static Uptr calcNumBytes(Uptr numArguments) {
                return offsetof(ExceptionData, arguments) + numArguments * sizeof(IR::UntaggedValue);
            }
        };

        struct Object {
            const ObjectKind kind;
        };

        struct FunctionMutableData {
            LLVMJIT::Module *jitModule = nullptr;
            Runtime::Function *function = nullptr;
            Uptr numCodeBytes = 0;
            std::atomic<Uptr> numRootReferences{0};
            std::map<U32, U32> offsetToOpIndexMap;
            std::string debugName;

            FunctionMutableData(std::string &&inDebugName) : debugName(inDebugName) {
            }
        };

        struct Function {
            Object object;
            FunctionMutableData *mutableData;
            const Uptr moduleInstanceId;
            const IR::FunctionType::Encoding encodedType;
            const U8 code[1];

            Function(FunctionMutableData *inMutableData, Uptr inModuleInstanceId, IR::FunctionType::Encoding inEncodedType)
                    : object{ObjectKind::function}, mutableData(inMutableData), moduleInstanceId(inModuleInstanceId),
                      encodedType(inEncodedType), code{0xcc} // int3
            {
            }
        };

        inline CompartmentRuntimeData *getCompartmentRuntimeData(ContextRuntimeData *contextRuntimeData) {
            return reinterpret_cast<CompartmentRuntimeData *>(reinterpret_cast<Uptr>(contextRuntimeData) &
                                                              0xffffffff00000000);
        }
    }
}
