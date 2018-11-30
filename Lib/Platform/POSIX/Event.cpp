#include <errno.h>
#include <pthread.h>

#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Platform/Event.h"

using namespace WAVM;
using namespace WAVM::Platform;

Platform::Event::Event() {
    static_assert(sizeof(pthreadMutex) == sizeof(pthread_mutex_t), "");
    static_assert(alignof(PthreadMutex) >= alignof(pthread_mutex_t), "");

    static_assert(sizeof(pthreadCond) == sizeof(pthread_cond_t), "");
    static_assert(alignof(PthreadCond) >= alignof(pthread_cond_t), "");

    pthread_condattr_t conditionVariableAttr;
    errorUnless(!pthread_condattr_init(&conditionVariableAttr));

// Set the condition variable to use the monotonic clock for wait timeouts.
#ifndef __APPLE__
    errorUnless(!pthread_condattr_setclock(&conditionVariableAttr, CLOCK_MONOTONIC));
#endif

    errorUnless(!pthread_cond_init((pthread_cond_t *) &pthreadCond, nullptr));
    errorUnless(!pthread_mutex_init((pthread_mutex_t *) &pthreadMutex, nullptr));

    errorUnless(!pthread_condattr_destroy(&conditionVariableAttr));
}

Platform::Event::~Event() {
    pthread_cond_destroy((pthread_cond_t *) &pthreadCond);
    errorUnless(!pthread_mutex_destroy((pthread_mutex_t *) &pthreadMutex));
}