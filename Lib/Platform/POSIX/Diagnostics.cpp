#include <cxxabi.h>
#include <dlfcn.h>
#include <cstdio>
#include <string>

#include "POSIXPrivate.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Platform/Mutex.h"

using namespace WAVM;
using namespace WAVM::Platform;

static Mutex &getErrorReportingMutex() {
    static Platform::Mutex mutex;
    return mutex;
}

void Platform::handleFatalError(const char *messageFormat, bool printCallStack, va_list varArgs) {
    Lock<Platform::Mutex> lock(getErrorReportingMutex());
    std::vfprintf(stderr, messageFormat, varArgs);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    std::abort();
}

void Platform::handleAssertionFailure(const AssertMetadata &metadata) {
    Lock<Platform::Mutex> lock(getErrorReportingMutex());
    std::fprintf(stderr, "Assertion failed at %s(%u): %s\n", metadata.file, metadata.line, metadata.condition);
    std::fflush(stderr);
}