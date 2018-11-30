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
