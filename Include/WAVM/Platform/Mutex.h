#pragma once

#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Platform/Defines.h"

namespace WAVM {
    namespace Platform {
        struct Mutex {
            PLATFORM_API Mutex();

            PLATFORM_API ~Mutex();

            Mutex(const Mutex &) = delete;

            Mutex(Mutex &&) = delete;

            void operator=(const Mutex &) = delete;

            void operator=(Mutex &&) = delete;

            PLATFORM_API void lock();

            PLATFORM_API void unlock();

            private:
                struct PthreadMutex {
                    Uptr data[5];
                }
                pthreadMutex;
        };
    }
}
