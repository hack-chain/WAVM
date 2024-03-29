#include <stddef.h>
#include <atomic>
#include <memory>

#include "RuntimePrivate.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Platform/Memory.h"

using namespace WAVM;
using namespace WAVM::Runtime;

Runtime::Compartment::Compartment()
        : GCObject(ObjectKind::compartment, this), unalignedRuntimeData(nullptr), tables(0, maxTables - 1),
          memories(0, maxMemories - 1)
// Use UINTPTR_MAX as an invalid ID for globals, exception types, and module instances.
        , globals(0, UINTPTR_MAX - 1), exceptionTypes(0, UINTPTR_MAX - 1), moduleInstances(0, UINTPTR_MAX - 1),
          contexts(0, maxContexts - 1) {
    runtimeData = (CompartmentRuntimeData *) Platform::allocateAlignedVirtualPages(compartmentReservedBytes
                                                                                           >> Platform::getPageSizeLog2(), compartmentRuntimeDataAlignmentLog2, unalignedRuntimeData);

    errorUnless(Platform::commitVirtualPages((U8 *) runtimeData, offsetof(CompartmentRuntimeData, contexts)
            >> Platform::getPageSizeLog2()));

    runtimeData->compartment = this;
}

Runtime::Compartment::~Compartment() {
    Lock<Platform::Mutex> compartmentLock(mutex);

    wavmAssert(!memories.size());
    wavmAssert(!tables.size());
    wavmAssert(!exceptionTypes.size());
    wavmAssert(!globals.size());
    wavmAssert(!moduleInstances.size());
    wavmAssert(!contexts.size());

    Platform::freeAlignedVirtualPages(unalignedRuntimeData, compartmentReservedBytes
            >> Platform::getPageSizeLog2(), compartmentRuntimeDataAlignmentLog2);
    runtimeData = nullptr;
    unalignedRuntimeData = nullptr;
}

Compartment *Runtime::createCompartment() {
    return new Compartment;
}

bool Runtime::isInCompartment(Object *object, const Compartment *compartment) {
    if (object->kind == ObjectKind::function) {
        // The function may be in multiple compartments, but if this compartment maps the function's
        // moduleInstanceId to a ModuleInstance with the LLVMJIT LoadedModule that contains this
        // function, then the function is in this compartment.
        Function *function = (Function *) object;

        // Treat functions with moduleInstanceId=UINTPTR_MAX as if they are in all compartments.
        if (function->moduleInstanceId == UINTPTR_MAX) {
            return true;
        }

        if (!compartment->moduleInstances.contains(function->moduleInstanceId)) {
            return false;
        }
        ModuleInstance *moduleInstance = compartment->moduleInstances[function->moduleInstanceId];
        return moduleInstance->jitModule.get() == function->mutableData->jitModule;
    } else {
        GCObject *gcObject = (GCObject *) object;
        return gcObject->compartment == compartment;
    }
}
