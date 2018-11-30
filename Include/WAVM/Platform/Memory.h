#pragma once

#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Platform/Defines.h"

namespace WAVM {
    namespace Platform {
        enum class MemoryAccess {
            none, readOnly, readWrite, execute, readWriteExecute
        };

        PLATFORM_API Uptr getPageSizeLog2();

        PLATFORM_API U8 *allocateVirtualPages(Uptr numPages);

        PLATFORM_API U8 *allocateAlignedVirtualPages(Uptr numPages, Uptr alignmentLog2, U8 *&outUnalignedBaseAddress);

        PLATFORM_API bool commitVirtualPages(U8 *baseVirtualAddress, Uptr numPages, MemoryAccess access = MemoryAccess::readWrite);

        PLATFORM_API bool setVirtualPageAccess(U8 *baseVirtualAddress, Uptr numPages, MemoryAccess access);

        PLATFORM_API void decommitVirtualPages(U8 *baseVirtualAddress, Uptr numPages);

        PLATFORM_API void freeVirtualPages(U8 *baseVirtualAddress, Uptr numPages);

        PLATFORM_API void freeAlignedVirtualPages(U8 *unalignedBaseAddress, Uptr numPages, Uptr alignmentLog2);
    }
}
