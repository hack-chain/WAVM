#pragma once

#include <setjmp.h>
#include <functional>

#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/Errors.h"

struct ExecutionContext {
    U64 rbx;
    U64 rsp;
    U64 rbp;
    U64 r12;
    U64 r13;
    U64 r14;
    U64 r15;
    U64 rip;
};
static_assert(offsetof(ExecutionContext, rbx) == 0, "unexpected offset");
static_assert(offsetof(ExecutionContext, rsp) == 8, "unexpected offset");
static_assert(offsetof(ExecutionContext, rbp) == 16, "unexpected offset");
static_assert(offsetof(ExecutionContext, r12) == 24, "unexpected offset");
static_assert(offsetof(ExecutionContext, r13) == 32, "unexpected offset");
static_assert(offsetof(ExecutionContext, r14) == 40, "unexpected offset");
static_assert(offsetof(ExecutionContext, r15) == 48, "unexpected offset");
static_assert(offsetof(ExecutionContext, rip) == 56, "unexpected offset");
static_assert(sizeof(ExecutionContext) == 64, "unexpected size");

#ifdef __WAVIX__
inline I64 saveExecutionState(ExecutionContext* outContext, I64 returnCode) noexcept(false)
{
    WAVM::Errors::fatal("saveExecutionState is unimplemented on Wavix");
}

[[noreturn]] inline void loadExecutionState(ExecutionContext* context, I64 returnCode)
{
    WAVM::Errors::fatal("loadExecutionState is unimplemented on Wavix");
}

inline I64 switchToForkedStackContext(ExecutionContext* forkedContext,
                                      U8* trampolineFramePointer) noexcept(false)
{
    WAVM::Errors::fatal("switchToForkedStackContext is unimplemented on Wavix");
}

inline U8* getStackPointer() { WAVM::Errors::fatal("getStackPointer is unimplemented on Wavix"); }

// libunwind dynamic frame registration
inline void __register_frame(const void* fde)
{
    WAVM::Errors::fatal("__register_frame is unimplemented on Wavix");
}

inline void __deregister_frame(const void* fde)
{
    WAVM::Errors::fatal("__deregister_frame is unimplemented on Wavix");
}

#else

extern "C" I64 saveExecutionState(ExecutionContext *outContext, I64 returnCode) noexcept(false);

[[noreturn]] extern void loadExecutionState(ExecutionContext *context, I64 returnCode);

extern "C" I64 switchToForkedStackContext(ExecutionContext *forkedContext, U8 *trampolineFramePointer) noexcept(false);
extern "C" U8 *getStackPointer();

extern "C" void __register_frame(const void *fde);
extern "C" void __deregister_frame(const void *fde);
#endif

namespace WAVM {
    namespace Platform {

        struct CallStack;

        void dumpErrorCallStack(Uptr numOmittedFramesFromTop);
    }
}
