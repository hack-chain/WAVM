#pragma once

#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Platform/Defines.h"

namespace WAVM {
    namespace Platform {
        inline U32 countLeadingZeroes(U32 value) {
            return value == 0 ? 32 : __builtin_clz(value);
        }

        inline U64 countLeadingZeroes(U64 value) {
            return value == 0 ? 64 : __builtin_clzll(value);
        }

        inline U64 countTrailingZeroes(U64 value) {
            return value == 0 ? 64 : __builtin_ctzll(value);
        }

        inline U32 floorLogTwo(U32 value) {
            return value <= 1 ? 0 : 31 - countLeadingZeroes(value);
        }

        inline U64 ceilLogTwo(U64 value) {
            return value <= 1 ? 0 : 63 - countLeadingZeroes(value * 2 - 1);
        }

        inline U64 saturateToBounds(U64 value, U64 maxValue) {
            return U64(value + ((I64(maxValue - value) >> 63) & (maxValue - value)));
        }

        inline void bytewiseMemCopy(U8 *dest, const U8 *source, Uptr numBytes) {
            asm volatile("rep movsb"
            : "=D"(dest), "=S"(source), "=c"(numBytes)
            : "0"(dest), "1"(source), "2"(numBytes)
            : "memory");
        }

        inline void bytewiseMemSet(U8 *dest, U8 value, Uptr numBytes) {
            asm volatile("rep stosb"
            : "=D"(dest), "=a"(value), "=c"(numBytes)
            : "0"(dest), "1"(value), "2"(numBytes)
            : "memory");
        }
        inline void bytewiseMemMove(U8 *dest, U8 *source, Uptr numBytes) {
            const Uptr numNonOverlappingBytes = source < dest && source + numBytes > dest ? dest - source : numBytes;

            if (numNonOverlappingBytes != numBytes) {
                bytewiseMemCopy(
                        dest + numNonOverlappingBytes,
                        source + numNonOverlappingBytes, numBytes - numNonOverlappingBytes);
            }

            bytewiseMemCopy(dest, source, numNonOverlappingBytes);
        }
    }
}
