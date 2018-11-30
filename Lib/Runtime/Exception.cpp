#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <map>
#include <vector>
#include <WAVM/LLVMJIT/LLVMJIT.h>

#include "RuntimePrivate.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Platform/Exception.h"

using namespace WAVM;
using namespace WAVM::Runtime;

#define DEFINE_INTRINSIC_EXCEPTION_TYPE(name, ...)                                                 \
    ExceptionType* Runtime::Exception::name##Type = new ExceptionType(                             \
        nullptr, IR::ExceptionType{IR::TypeTuple({__VA_ARGS__})}, "wavm." #name);
ENUM_INTRINSIC_EXCEPTION_TYPES(DEFINE_INTRINSIC_EXCEPTION_TYPE)
#undef DEFINE_INTRINSIC_EXCEPTION_TYPE

bool Runtime::describeInstructionPointer(Uptr ip, std::string &outDescription) {
    Runtime::Function *function = LLVMJIT::getFunctionByAddress(ip);
    if (!function) { return Platform::describeInstructionPointer(ip, outDescription); }
    else {
        outDescription = function->mutableData->debugName;
        outDescription += '+';

        // Find the highest entry in the offsetToOpIndexMap whose offset is <= the
        // symbol-relative IP.
        U32 ipOffset = (U32) (ip - reinterpret_cast<Uptr>(function->code));
        Iptr opIndex = -1;
        for (auto offsetMapIt : function->mutableData->offsetToOpIndexMap) {
            if (offsetMapIt.first <= ipOffset) { opIndex = offsetMapIt.second; }
            else {
                break;
            }
        }
        outDescription += std::to_string(opIndex >= 0 ? opIndex : 0);
        return true;
    }
}

ExceptionType *Runtime::createExceptionType(Compartment *compartment, IR::ExceptionType sig, std::string &&debugName) {
    auto exceptionType = new ExceptionType(compartment, sig, std::move(debugName));

    Lock<Platform::Mutex> compartmentLock(compartment->mutex);
    exceptionType->id = compartment->exceptionTypes.add(UINTPTR_MAX, exceptionType);
    if (exceptionType->id == UINTPTR_MAX) {
        delete exceptionType;
        return nullptr;
    }

    return exceptionType;
}

ExceptionType *Runtime::cloneExceptionType(ExceptionType *exceptionType,
                                           Compartment *newCompartment) {
    auto newExceptionType = new ExceptionType(
            newCompartment, exceptionType->sig, std::string(exceptionType->debugName));
    newExceptionType->id = exceptionType->id;

    Lock<Platform::Mutex> compartmentLock(newCompartment->mutex);
    newCompartment->exceptionTypes.insertOrFail(exceptionType->id, newExceptionType);
    return newExceptionType;
}

Runtime::ExceptionType::~ExceptionType() {
    if (id != UINTPTR_MAX) {
        wavmAssertMutexIsLockedByCurrentThread(compartment->mutex);
        compartment->exceptionTypes.removeOrFail(id);
    }
}

[[noreturn]] void Runtime::throwException(ExceptionType *type,
                                          std::vector<IR::UntaggedValue> &&arguments) {
    wavmAssert(arguments.size() == type->sig.params.size());
    ExceptionData *exceptionData
            = (ExceptionData *) malloc(ExceptionData::calcNumBytes(type->sig.params.size()));
    exceptionData->typeId = type->id;
    exceptionData->type = type;
    exceptionData->isUserException = 0;
    if (arguments.size()) {
        memcpy(exceptionData->arguments,
               arguments.data(),
               sizeof(IR::UntaggedValue) * arguments.size());
    }
    Platform::raisePlatformException(exceptionData);
}

DEFINE_INTRINSIC_FUNCTION(wavmIntrinsics, "throwException", void, intrinsicThrowException, Uptr exceptionTypeId, Uptr argsBits, U32 isUserException) {
    ExceptionType *exceptionType;
    {
        Compartment *compartment = getCompartmentRuntimeData(contextRuntimeData)->compartment;
        Lock<Platform::Mutex> compartmentLock(compartment->mutex);
        exceptionType = compartment->exceptionTypes[exceptionTypeId];
    }
    auto args = reinterpret_cast<const IR::UntaggedValue *>(Uptr(argsBits));

    ExceptionData *exceptionData
            = (ExceptionData *) malloc(ExceptionData::calcNumBytes(exceptionType->sig.params.size()));
    exceptionData->typeId = exceptionTypeId;
    exceptionData->type = exceptionType;
    exceptionData->isUserException = isUserException ? 1 : 0;
    memcpy(exceptionData->arguments,
           args,
           sizeof(IR::UntaggedValue) * exceptionType->sig.params.size());
    Platform::raisePlatformException(exceptionData);
}

DEFINE_INTRINSIC_FUNCTION(wavmIntrinsics,
                          "rethrowException",
                          void,
                          rethrowException,
                          Uptr exceptionBits) {
    ExceptionData *exception = reinterpret_cast<ExceptionData *>(exceptionBits);
    Platform::raisePlatformException(exception);
}

