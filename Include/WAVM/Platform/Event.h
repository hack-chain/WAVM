#pragma once

#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Platform/Defines.h"

namespace WAVM {
    namespace Platform {
        struct Event {
            PLATFORM_API Event();

            PLATFORM_API ~Event();

            Event(const Event &) = delete;

            Event(Event &&) = delete;

            void operator=(const Event &) = delete;

            void operator=(Event &&) = delete;

        private:
            struct PthreadMutex {
                Uptr data[5];
            } pthreadMutex;
            struct PthreadCond {
                Uptr data[6];
            } pthreadCond;
        };
    }
}
