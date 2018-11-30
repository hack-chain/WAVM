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

