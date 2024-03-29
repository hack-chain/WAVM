#include "EmitContext.h"
#include "EmitFunctionContext.h"

PUSH_DISABLE_WARNINGS_FOR_LLVM_HEADERS

POP_DISABLE_WARNINGS_FOR_LLVM_HEADERS

using namespace WAVM::IR;
using namespace WAVM::LLVMJIT;

// Bounds checks a sandboxed memory address + offset, and returns an offset relative to the memory
// base address that is guaranteed to be within the virtual address space allocated for the linear
// memory object.
static llvm::Value *getOffsetAndBoundedAddress(EmitContext &emitContext, llvm::Value *address, U32 offset) {
    // zext the 32-bit address to 64-bits.
    // This is crucial for security, as LLVM will otherwise implicitly sign extend it to 64-bits in
    // the GEP below, interpreting it as a signed offset and allowing access to memory outside the
    // sandboxed memory range. There are no 'far addresses' in a 32 bit runtime.
    address = emitContext.irBuilder.CreateZExt(address, emitContext.llvmContext.i64Type);

    // Add the offset to the byte index.
    if (offset) {
        address = emitContext.irBuilder.CreateAdd(address, emitContext.irBuilder.CreateZExt(emitLiteral(emitContext.llvmContext, offset), emitContext.llvmContext.i64Type));
    }

    // If HAS_64BIT_ADDRESS_SPACE, the memory has enough virtual address space allocated to ensure
    // that any 32-bit byte index + 32-bit offset will fall within the virtual address sandbox, so
    // no explicit bounds check is necessary.

    return address;
}

llvm::Value *EmitFunctionContext::coerceAddressToPointer(llvm::Value *boundedAddress, llvm::Type *memoryType) {
    llvm::Value *memoryBasePointer = irBuilder.CreateLoad(memoryBasePointerVariable);
    llvm::Value *bytePointer = irBuilder.CreateInBoundsGEP(memoryBasePointer, boundedAddress);

    // Cast the pointer to the appropriate type.
    return irBuilder.CreatePointerCast(bytePointer, memoryType->getPointerTo());
}

//
// Memory size operators
// These just call out to wavmIntrinsics.growMemory/currentMemory, passing a pointer to the default
// memory for the module.
//

void EmitFunctionContext::memory_grow(MemoryImm imm) {
    errorUnless(imm.memoryIndex == 0);
    llvm::Value *deltaNumPages = pop();
    ValueVector previousNumPages = emitRuntimeIntrinsic("memory.grow", FunctionType(TypeTuple(ValueType::i32), TypeTuple({ValueType::i32, inferValueType<Uptr>()})), {deltaNumPages, getMemoryIdFromOffset(llvmContext, moduleContext.memoryOffsets[imm.memoryIndex])});
    wavmAssert(previousNumPages.size() == 1);
    push(previousNumPages[0]);
}

void EmitFunctionContext::memory_size(MemoryImm imm) {
    errorUnless(imm.memoryIndex == 0);
    ValueVector currentNumPages = emitRuntimeIntrinsic("memory.size", FunctionType(TypeTuple(ValueType::i32), TypeTuple(inferValueType<Uptr>())), {getMemoryIdFromOffset(llvmContext, moduleContext.memoryOffsets[imm.memoryIndex])});
    wavmAssert(currentNumPages.size() == 1);
    push(currentNumPages[0]);
}

//
// Memory bulk operators.
//

void EmitFunctionContext::memory_init(DataSegmentAndMemImm imm) {
    auto numBytes = pop();
    auto sourceOffset = pop();
    auto destAddress = pop();
    emitRuntimeIntrinsic("memory.init", FunctionType({}, TypeTuple({ValueType::i32, ValueType::i32, ValueType::i32, inferValueType<Uptr>(), inferValueType<Uptr>(), inferValueType<Uptr>()})), {destAddress, sourceOffset, numBytes, moduleContext.moduleInstanceId, getMemoryIdFromOffset(llvmContext, moduleContext.memoryOffsets[imm.memoryIndex]), emitLiteral(llvmContext, imm.dataSegmentIndex)});
}

void EmitFunctionContext::memory_drop(DataSegmentImm imm) {
    emitRuntimeIntrinsic("memory.drop", FunctionType({}, TypeTuple({inferValueType<Uptr>(), inferValueType<Uptr>()})), {moduleContext.moduleInstanceId, emitLiteral(llvmContext, imm.dataSegmentIndex)});
}

void EmitFunctionContext::memory_copy(MemoryImm imm) {
    auto numBytes = pop();
    auto sourceAddress = pop();
    auto destAddress = pop();

    emitRuntimeIntrinsic("memory.copy", FunctionType({}, TypeTuple({ValueType::i32, ValueType::i32, ValueType::i32, inferValueType<Uptr>()})), {destAddress, sourceAddress, numBytes, getMemoryIdFromOffset(llvmContext, moduleContext.memoryOffsets[imm.memoryIndex])});
}

void EmitFunctionContext::memory_fill(MemoryImm imm) {
    auto numBytes = pop();
    auto value = pop();
    auto destAddress = pop();

    emitRuntimeIntrinsic("memory.fill", FunctionType({}, TypeTuple({ValueType::i32, ValueType::i32, ValueType::i32, inferValueType<Uptr>()})), {destAddress, value, numBytes, getMemoryIdFromOffset(llvmContext, moduleContext.memoryOffsets[imm.memoryIndex])});
}

//
// Load/store operators
//

#define EMIT_LOAD_OP(valueTypeId, name, llvmMemoryType, naturalAlignmentLog2, conversionOp)        \
    void EmitFunctionContext::valueTypeId##_##name(LoadOrStoreImm<naturalAlignmentLog2> imm)       \
    {                                                                                              \
        auto address = pop();                                                                      \
        auto boundedAddress = getOffsetAndBoundedAddress(*this, address, imm.offset);              \
        auto pointer = coerceAddressToPointer(boundedAddress, llvmMemoryType);                     \
        auto load = irBuilder.CreateLoad(pointer);                                                 \
        /* Don't trust the alignment hint provided by the WebAssembly code, since the load can't   \
		 * trap if it's wrong. */                                                                  \
        load->setAlignment(1);                                                                     \
        load->setVolatile(true);                                                                   \
        push(conversionOp(load, asLLVMType(llvmContext, ValueType::valueTypeId)));                 \
    }
#define EMIT_STORE_OP(valueTypeId, name, llvmMemoryType, naturalAlignmentLog2, conversionOp)       \
    void EmitFunctionContext::valueTypeId##_##name(LoadOrStoreImm<naturalAlignmentLog2> imm)       \
    {                                                                                              \
        auto value = pop();                                                                        \
        auto address = pop();                                                                      \
        auto boundedAddress = getOffsetAndBoundedAddress(*this, address, imm.offset);              \
        auto pointer = coerceAddressToPointer(boundedAddress, llvmMemoryType);                     \
        auto memoryValue = conversionOp(value, llvmMemoryType);                                    \
        auto store = irBuilder.CreateStore(memoryValue, pointer);                                  \
        store->setVolatile(true);                                                                  \
        /* Don't trust the alignment hint provided by the WebAssembly code, since the store can't  \
		 * trap if it's wrong. */                                                                  \
        store->setAlignment(1);                                                                    \
    }

EMIT_LOAD_OP(i32, load8_s, llvmContext.i8Type, 0, sext)

EMIT_LOAD_OP(i32, load8_u, llvmContext.i8Type, 0, zext)

EMIT_LOAD_OP(i32, load16_s, llvmContext.i16Type, 1, sext)

EMIT_LOAD_OP(i32, load16_u, llvmContext.i16Type, 1, zext)

EMIT_LOAD_OP(i64, load8_s, llvmContext.i8Type, 0, sext)

EMIT_LOAD_OP(i64, load8_u, llvmContext.i8Type, 0, zext)

EMIT_LOAD_OP(i64, load16_s, llvmContext.i16Type, 1, sext)

EMIT_LOAD_OP(i64, load16_u, llvmContext.i16Type, 1, zext)

EMIT_LOAD_OP(i64, load32_s, llvmContext.i32Type, 2, sext)

EMIT_LOAD_OP(i64, load32_u, llvmContext.i32Type, 2, zext)

EMIT_LOAD_OP(i32, load, llvmContext.i32Type, 2, identity)

EMIT_LOAD_OP(i64, load, llvmContext.i64Type, 3, identity)

EMIT_LOAD_OP(f32, load, llvmContext.f32Type, 2, identity)

EMIT_LOAD_OP(f64, load, llvmContext.f64Type, 3, identity)

EMIT_STORE_OP(i32, store8, llvmContext.i8Type, 0, trunc)

EMIT_STORE_OP(i64, store8, llvmContext.i8Type, 0, trunc)

EMIT_STORE_OP(i32, store16, llvmContext.i16Type, 1, trunc)

EMIT_STORE_OP(i64, store16, llvmContext.i16Type, 1, trunc)

EMIT_STORE_OP(i32, store, llvmContext.i32Type, 2, trunc)

EMIT_STORE_OP(i64, store32, llvmContext.i32Type, 2, trunc)

EMIT_STORE_OP(i64, store, llvmContext.i64Type, 3, identity)

EMIT_STORE_OP(f32, store, llvmContext.f32Type, 2, identity)

EMIT_STORE_OP(f64, store, llvmContext.f64Type, 3, identity)

EMIT_STORE_OP(v128, store, value->getType(), 4, identity)

EMIT_LOAD_OP(v128, load, llvmContext.i64x2Type, 4, identity)

void EmitFunctionContext::trapIfMisalignedAtomic(llvm::Value *address, U32 alignmentLog2) {
    if (alignmentLog2 > 0) {
        emitConditionalTrapIntrinsic(irBuilder.CreateICmpNE(llvmContext.typedZeroConstants[(Uptr) ValueType::i64], irBuilder.CreateAnd(address, emitLiteral(llvmContext,
                                                                                                                                                            (U64(1)
                                                                                                                                                                    << alignmentLog2) -
                                                                                                                                                            1))), "misalignedAtomicTrap", FunctionType(TypeTuple{}, TypeTuple{ValueType::i64}), {address});
    }
}

void EmitFunctionContext::atomic_wake(AtomicLoadOrStoreImm<2> imm) {
    llvm::Value *numWaiters = pop();
    llvm::Value *address = pop();
    llvm::Value *boundedAddress = getOffsetAndBoundedAddress(*this, address, imm.offset);
    trapIfMisalignedAtomic(boundedAddress, imm.alignmentLog2);
    push(emitRuntimeIntrinsic("atomic_wake", FunctionType(TypeTuple{ValueType::i32}, TypeTuple{ValueType::i32, ValueType::i32, ValueType::i64}), {address, numWaiters, getMemoryIdFromOffset(llvmContext, moduleContext.defaultMemoryOffset)})[0]);
}

void EmitFunctionContext::i32_atomic_wait(AtomicLoadOrStoreImm<2> imm) {
    llvm::Value *timeout = pop();
    llvm::Value *expectedValue = pop();
    llvm::Value *address = pop();
    llvm::Value *boundedAddress = getOffsetAndBoundedAddress(*this, address, imm.offset);
    trapIfMisalignedAtomic(boundedAddress, imm.alignmentLog2);
    push(emitRuntimeIntrinsic("atomic_wait_i32", FunctionType(TypeTuple{ValueType::i32}, TypeTuple{ValueType::i32, ValueType::i32, ValueType::f64, inferValueType<Uptr>()}), {address, expectedValue, timeout, getMemoryIdFromOffset(llvmContext, moduleContext.defaultMemoryOffset)})[0]);
}

void EmitFunctionContext::i64_atomic_wait(AtomicLoadOrStoreImm<3> imm) {
    llvm::Value *timeout = pop();
    llvm::Value *expectedValue = pop();
    llvm::Value *address = pop();
    llvm::Value *boundedAddress = getOffsetAndBoundedAddress(*this, address, imm.offset);
    trapIfMisalignedAtomic(boundedAddress, imm.alignmentLog2);
    push(emitRuntimeIntrinsic("atomic_wait_i64", FunctionType(TypeTuple{ValueType::i32}, TypeTuple{ValueType::i32, ValueType::i64, ValueType::f64, inferValueType<Uptr>()}), {address, expectedValue, timeout, getMemoryIdFromOffset(llvmContext, moduleContext.defaultMemoryOffset)})[0]);
}

#define EMIT_ATOMIC_LOAD_OP(valueTypeId, name, llvmMemoryType, naturalAlignmentLog2, memToValue)   \
    void EmitFunctionContext::valueTypeId##_##name(AtomicLoadOrStoreImm<naturalAlignmentLog2> imm) \
    {                                                                                              \
        auto address = pop();                                                                      \
        auto boundedAddress = getOffsetAndBoundedAddress(*this, address, imm.offset);              \
        trapIfMisalignedAtomic(boundedAddress, naturalAlignmentLog2);                              \
        auto pointer = coerceAddressToPointer(boundedAddress, llvmMemoryType);                     \
        auto load = irBuilder.CreateLoad(pointer);                                                 \
        load->setAlignment(1 << imm.alignmentLog2);                                                \
        load->setVolatile(true);                                                                   \
        load->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);                             \
        push(memToValue(load, asLLVMType(llvmContext, ValueType::valueTypeId)));                   \
    }
#define EMIT_ATOMIC_STORE_OP(valueTypeId, name, llvmMemoryType, naturalAlignmentLog2, valueToMem)  \
    void EmitFunctionContext::valueTypeId##_##name(AtomicLoadOrStoreImm<naturalAlignmentLog2> imm) \
    {                                                                                              \
        auto value = pop();                                                                        \
        auto address = pop();                                                                      \
        auto boundedAddress = getOffsetAndBoundedAddress(*this, address, imm.offset);              \
        trapIfMisalignedAtomic(boundedAddress, naturalAlignmentLog2);                              \
        auto pointer = coerceAddressToPointer(boundedAddress, llvmMemoryType);                     \
        auto memoryValue = valueToMem(value, llvmMemoryType);                                      \
        auto store = irBuilder.CreateStore(memoryValue, pointer);                                  \
        store->setVolatile(true);                                                                  \
        store->setAlignment(1 << imm.alignmentLog2);                                               \
        store->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);                            \
    }

EMIT_ATOMIC_LOAD_OP(i32, atomic_load, llvmContext.i32Type, 2, identity)

EMIT_ATOMIC_LOAD_OP(i64, atomic_load, llvmContext.i64Type, 3, identity)

EMIT_ATOMIC_LOAD_OP(i32, atomic_load8_u, llvmContext.i8Type, 0, zext)

EMIT_ATOMIC_LOAD_OP(i32, atomic_load16_u, llvmContext.i16Type, 1, zext)

EMIT_ATOMIC_LOAD_OP(i64, atomic_load8_u, llvmContext.i8Type, 0, zext)

EMIT_ATOMIC_LOAD_OP(i64, atomic_load16_u, llvmContext.i16Type, 1, zext)

EMIT_ATOMIC_LOAD_OP(i64, atomic_load32_u, llvmContext.i32Type, 2, zext)

EMIT_ATOMIC_STORE_OP(i32, atomic_store, llvmContext.i32Type, 2, identity)

EMIT_ATOMIC_STORE_OP(i64, atomic_store, llvmContext.i64Type, 3, identity)

EMIT_ATOMIC_STORE_OP(i32, atomic_store8, llvmContext.i8Type, 0, trunc)

EMIT_ATOMIC_STORE_OP(i32, atomic_store16, llvmContext.i16Type, 1, trunc)

EMIT_ATOMIC_STORE_OP(i64, atomic_store8, llvmContext.i8Type, 0, trunc)

EMIT_ATOMIC_STORE_OP(i64, atomic_store16, llvmContext.i16Type, 1, trunc)

EMIT_ATOMIC_STORE_OP(i64, atomic_store32, llvmContext.i32Type, 2, trunc)

#define EMIT_ATOMIC_CMPXCHG(\
    valueTypeId, name, llvmMemoryType, alignmentLog2, memToValue, valueToMem)                      \
    void EmitFunctionContext::valueTypeId##_##name(AtomicLoadOrStoreImm<alignmentLog2> imm)        \
    {                                                                                              \
        auto replacementValue = valueToMem(pop(), llvmMemoryType);                                 \
        auto expectedValue = valueToMem(pop(), llvmMemoryType);                                    \
        auto address = pop();                                                                      \
        auto boundedAddress = getOffsetAndBoundedAddress(*this, address, imm.offset);              \
        trapIfMisalignedAtomic(boundedAddress, alignmentLog2);                                     \
        auto pointer = coerceAddressToPointer(boundedAddress, llvmMemoryType);                     \
        auto atomicCmpXchg                                                                         \
            = irBuilder.CreateAtomicCmpXchg(pointer,                                               \
                                            expectedValue,                                         \
                                            replacementValue,                                      \
                                            llvm::AtomicOrdering::SequentiallyConsistent,          \
                                            llvm::AtomicOrdering::SequentiallyConsistent);         \
        atomicCmpXchg->setVolatile(true);                                                          \
        auto previousValue = irBuilder.CreateExtractValue(atomicCmpXchg, {0});                     \
        push(memToValue(previousValue, asLLVMType(llvmContext, ValueType::valueTypeId)));          \
    }

EMIT_ATOMIC_CMPXCHG(i32, atomic_rmw8_u_cmpxchg, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_CMPXCHG(i32, atomic_rmw16_u_cmpxchg, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_CMPXCHG(i32, atomic_rmw_cmpxchg, llvmContext.i32Type, 2, identity, identity)

EMIT_ATOMIC_CMPXCHG(i64, atomic_rmw8_u_cmpxchg, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_CMPXCHG(i64, atomic_rmw16_u_cmpxchg, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_CMPXCHG(i64, atomic_rmw32_u_cmpxchg, llvmContext.i32Type, 2, zext, trunc)

EMIT_ATOMIC_CMPXCHG(i64, atomic_rmw_cmpxchg, llvmContext.i64Type, 3, identity, identity)

#define EMIT_ATOMIC_RMW(\
    valueTypeId, name, rmwOpId, llvmMemoryType, alignmentLog2, memToValue, valueToMem)             \
    void EmitFunctionContext::valueTypeId##_##name(AtomicLoadOrStoreImm<alignmentLog2> imm)        \
    {                                                                                              \
        auto value = valueToMem(pop(), llvmMemoryType);                                            \
        auto address = pop();                                                                      \
        auto boundedAddress = getOffsetAndBoundedAddress(*this, address, imm.offset);              \
        trapIfMisalignedAtomic(boundedAddress, alignmentLog2);                                     \
        auto pointer = coerceAddressToPointer(boundedAddress, llvmMemoryType);                     \
        auto atomicRMW = irBuilder.CreateAtomicRMW(llvm::AtomicRMWInst::BinOp::rmwOpId,            \
                                                   pointer,                                        \
                                                   value,                                          \
                                                   llvm::AtomicOrdering::SequentiallyConsistent);  \
        atomicRMW->setVolatile(true);                                                              \
        push(memToValue(atomicRMW, asLLVMType(llvmContext, ValueType::valueTypeId)));              \
    }

EMIT_ATOMIC_RMW(i32, atomic_rmw8_u_xchg, Xchg, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw16_u_xchg, Xchg, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw_xchg, Xchg, llvmContext.i32Type, 2, identity, identity)

EMIT_ATOMIC_RMW(i64, atomic_rmw8_u_xchg, Xchg, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw16_u_xchg, Xchg, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw32_u_xchg, Xchg, llvmContext.i32Type, 2, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw_xchg, Xchg, llvmContext.i64Type, 3, identity, identity)

EMIT_ATOMIC_RMW(i32, atomic_rmw8_u_add, Add, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw16_u_add, Add, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw_add, Add, llvmContext.i32Type, 2, identity, identity)

EMIT_ATOMIC_RMW(i64, atomic_rmw8_u_add, Add, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw16_u_add, Add, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw32_u_add, Add, llvmContext.i32Type, 2, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw_add, Add, llvmContext.i64Type, 3, identity, identity)

EMIT_ATOMIC_RMW(i32, atomic_rmw8_u_sub, Sub, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw16_u_sub, Sub, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw_sub, Sub, llvmContext.i32Type, 2, identity, identity)

EMIT_ATOMIC_RMW(i64, atomic_rmw8_u_sub, Sub, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw16_u_sub, Sub, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw32_u_sub, Sub, llvmContext.i32Type, 2, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw_sub, Sub, llvmContext.i64Type, 3, identity, identity)

EMIT_ATOMIC_RMW(i32, atomic_rmw8_u_and, And, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw16_u_and, And, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw_and, And, llvmContext.i32Type, 2, identity, identity)

EMIT_ATOMIC_RMW(i64, atomic_rmw8_u_and, And, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw16_u_and, And, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw32_u_and, And, llvmContext.i32Type, 2, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw_and, And, llvmContext.i64Type, 3, identity, identity)

EMIT_ATOMIC_RMW(i32, atomic_rmw8_u_or, Or, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw16_u_or, Or, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw_or, Or, llvmContext.i32Type, 2, identity, identity)

EMIT_ATOMIC_RMW(i64, atomic_rmw8_u_or, Or, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw16_u_or, Or, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw32_u_or, Or, llvmContext.i32Type, 2, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw_or, Or, llvmContext.i64Type, 3, identity, identity)

EMIT_ATOMIC_RMW(i32, atomic_rmw8_u_xor, Xor, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw16_u_xor, Xor, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i32, atomic_rmw_xor, Xor, llvmContext.i32Type, 2, identity, identity)

EMIT_ATOMIC_RMW(i64, atomic_rmw8_u_xor, Xor, llvmContext.i8Type, 0, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw16_u_xor, Xor, llvmContext.i16Type, 1, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw32_u_xor, Xor, llvmContext.i32Type, 2, zext, trunc)

EMIT_ATOMIC_RMW(i64, atomic_rmw_xor, Xor, llvmContext.i64Type, 3, identity, identity)
