#include <stdint.h>
#include <string.h>
#include <atomic>
#include <vector>

#include "RuntimePrivate.h"
#include "WAVM/Inline/Lock.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Runtime;

Global *Runtime::createGlobal(Compartment *compartment, GlobalType type, Value initialValue) {
    errorUnless(isSubtype(initialValue.type, type.valueType));
    errorUnless(!isReferenceType(type.valueType) || !initialValue.object
                || isInCompartment(initialValue.object, compartment));

    U32 mutableGlobalIndex = UINT32_MAX;
    if (type.isMutable) {
        mutableGlobalIndex = compartment->globalDataAllocationMask.getSmallestNonMember();
        if (mutableGlobalIndex == maxMutableGlobals) { return nullptr; }
        compartment->globalDataAllocationMask.add(mutableGlobalIndex);

        // Initialize the global value for each context, and the data used to initialize new
        // contexts.
        compartment->initialContextMutableGlobals[mutableGlobalIndex] = initialValue;
        for (Context *context : compartment->contexts) { context->runtimeData->mutableGlobals[mutableGlobalIndex] = initialValue; }
    }

    // Create the global and add it to the compartment's list of globals.
    Global *global = new Global(compartment, type, mutableGlobalIndex, initialValue);
    {
        Lock<Platform::Mutex> compartmentLock(compartment->mutex);
        global->id = compartment->globals.add(UINTPTR_MAX, global);
        if (global->id == UINTPTR_MAX) {
            delete global;
            return nullptr;
        }
    }

    return global;
}

Global *Runtime::cloneGlobal(Global *global, Compartment *newCompartment) {
    IR::UntaggedValue initialValue = global->initialValue;
    if (isReferenceType(global->type.valueType)) {
        initialValue.object = remapToClonedCompartment(initialValue.object, newCompartment);
        if (global->type.isMutable) {
            Object *&initialMutableRef
                    = newCompartment->initialContextMutableGlobals[global->mutableGlobalIndex].object;
            initialMutableRef = remapToClonedCompartment(initialMutableRef, newCompartment);
        }
    }

    Global *newGlobal
            = new Global(newCompartment, global->type, global->mutableGlobalIndex, initialValue);
    newGlobal->id = global->id;

    Lock<Platform::Mutex> compartmentLock(newCompartment->mutex);
    newCompartment->globals.insertOrFail(global->id, newGlobal);
    return newGlobal;
}

Runtime::Global::~Global() {
    if (id != UINTPTR_MAX) {
        wavmAssertMutexIsLockedByCurrentThread(compartment->mutex);
        compartment->globals.removeOrFail(id);
    }

    if (type.isMutable) {
        wavmAssert(mutableGlobalIndex < maxMutableGlobals);
        wavmAssert(compartment->globalDataAllocationMask.contains(mutableGlobalIndex));
        compartment->globalDataAllocationMask.remove(mutableGlobalIndex);
    }
}

Value Runtime::getGlobalValue(const Context *context, Global *global) {
    wavmAssert(context || !global->type.isMutable);
    return Value(global->type.valueType,
                 global->type.isMutable
                 ? context->runtimeData->mutableGlobals[global->mutableGlobalIndex]
                 : global->initialValue);
}

Value Runtime::setGlobalValue(Context *context, Global *global, Value newValue) {
    wavmAssert(context);
    wavmAssert(newValue.type == global->type.valueType);
    wavmAssert(global->type.isMutable);
    errorUnless(context->compartment == global->compartment);
    errorUnless(!isReferenceType(global->type.valueType) || !newValue.object
                || isInCompartment(newValue.object, context->compartment));
    UntaggedValue &value = context->runtimeData->mutableGlobals[global->mutableGlobalIndex];
    const Value previousValue = Value(global->type.valueType, value);
    value = newValue;
    return previousValue;
}
