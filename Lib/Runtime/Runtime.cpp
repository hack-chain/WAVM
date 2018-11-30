#include "WAVM/Runtime/Runtime.h"
#include "RuntimePrivate.h"
#include "WAVM/Platform/Memory.h"
#include "WAVM/Inline/Lock.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Runtime;

Context *Runtime::createContext(Compartment *compartment) {
    wavmAssert(compartment);
    Context *context = new Context(compartment);
    {
        Lock<Platform::Mutex> lock(compartment->mutex);

        // Allocate an ID for the context in the compartment.
        context->id = compartment->contexts.add(UINTPTR_MAX, context);
        if (context->id == UINTPTR_MAX) {
            delete context;
            return nullptr;
        }
        context->runtimeData = &compartment->runtimeData->contexts[context->id];

        // Commit the page(s) for the context's runtime data.
        errorUnless(Platform::commitVirtualPages((U8 *) context->runtimeData,
                                                 sizeof(ContextRuntimeData) >> Platform::getPageSizeLog2()));

        // Initialize the context's global data.
        memcpy(context->runtimeData->mutableGlobals, compartment->initialContextMutableGlobals, maxGlobalBytes);
    }

    return context;
}

Runtime::Context::~Context() {
    compartment->contexts.removeOrFail(id);
}

Global *Runtime::createGlobal(Compartment *compartment, GlobalType type, Value initialValue) {
    errorUnless(isSubtype(initialValue.type, type.valueType));
    errorUnless(!isReferenceType(type.valueType) || !initialValue.object ||
                isInCompartment(initialValue.object, compartment));

    U32 mutableGlobalIndex = UINT32_MAX;
    if (type.isMutable) {
        mutableGlobalIndex = compartment->globalDataAllocationMask.getSmallestNonMember();
        if (mutableGlobalIndex == maxMutableGlobals) {
            return nullptr;
        }
        compartment->globalDataAllocationMask.add(mutableGlobalIndex);

        // Initialize the global value for each context, and the data used to initialize new
        // contexts.
        compartment->initialContextMutableGlobals[mutableGlobalIndex] = initialValue;
        for (Context *context : compartment->contexts) {
            context->runtimeData->mutableGlobals[mutableGlobalIndex] = initialValue;
        }
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

Runtime::Global::~Global() {
    if (id != UINTPTR_MAX) {
        compartment->globals.removeOrFail(id);
    }

    if (type.isMutable) {
        wavmAssert(mutableGlobalIndex < maxMutableGlobals);
        wavmAssert(compartment->globalDataAllocationMask.contains(mutableGlobalIndex));
        compartment->globalDataAllocationMask.remove(mutableGlobalIndex);
    }
}

#define DEFINE_OBJECT_TYPE(kindId, kindName, Type)                                                 \
    Runtime::Type* Runtime::as##kindName(Object* object)                                           \
    {                                                                                              \
        wavmAssert(!object || object->kind == kindId);                                             \
        return (Runtime::Type*)object;                                                             \
    }                                                                                              \
    Runtime::Type* Runtime::as##kindName##Nullable(Object* object)                                 \
    {                                                                                              \
        return object && object->kind == kindId ? (Runtime::Type*)object : nullptr;                \
    }                                                                                              \
    const Runtime::Type* Runtime::as##kindName(const Object* object)                               \
    {                                                                                              \
        wavmAssert(!object || object->kind == kindId);                                             \
        return (const Runtime::Type*)object;                                                       \
    }                                                                                              \
    const Runtime::Type* Runtime::as##kindName##Nullable(const Object* object)                     \
    {                                                                                              \
        return object && object->kind == kindId ? (const Runtime::Type*)object : nullptr;          \
    }                                                                                              \
    Object* Runtime::asObject(Runtime::Type* object) { return (Object*)object; }                   \
    const Object* Runtime::asObject(const Runtime::Type* object) { return (const Object*)object; }

DEFINE_OBJECT_TYPE(ObjectKind::function, Function, Function);

DEFINE_OBJECT_TYPE(ObjectKind::table, Table, Table);

DEFINE_OBJECT_TYPE(ObjectKind::memory, Memory, Memory);

DEFINE_OBJECT_TYPE(ObjectKind::global, Global, Global);

DEFINE_OBJECT_TYPE(ObjectKind::exceptionType, ExceptionType, ExceptionType);

DEFINE_OBJECT_TYPE(ObjectKind::moduleInstance, ModuleInstance, ModuleInstance);

DEFINE_OBJECT_TYPE(ObjectKind::context, Context, Context);

DEFINE_OBJECT_TYPE(ObjectKind::compartment, Compartment, Compartment);

bool Runtime::isA(Object *object, const ExternType &type) {
    if (ObjectKind(type.kind) != object->kind) {
        return false;
    }

    switch (type.kind) {
        case ExternKind::function:
            return asFunction(object)->encodedType == asFunctionType(type);
        case ExternKind::global:
            return isSubtype(asGlobal(object)->type, asGlobalType(type));
        case ExternKind::table:
            return isSubtype(asTable(object)->type, asTableType(type));
        case ExternKind::memory:
            return isSubtype(asMemory(object)->type, asMemoryType(type));
        case ExternKind::exceptionType:
            return isSubtype(asExceptionType(type).params, asExceptionType(object)->sig.params);
        default:
            Errors::unreachable();
    }
}

ExternType Runtime::getObjectType(Object *object) {
    switch (object->kind) {
        case ObjectKind::function:
            return FunctionType(asFunction(object)->encodedType);
        case ObjectKind::global:
            return asGlobal(object)->type;
        case ObjectKind::table:
            return asTable(object)->type;
        case ObjectKind::memory:
            return asMemory(object)->type;
        case ObjectKind::exceptionType:
            return asExceptionType(object)->sig;
        default:
            Errors::unreachable();
    };
}

FunctionType Runtime::getFunctionType(Function *function) {
    return function->encodedType;
}

ModuleInstance *Runtime::getModuleInstanceFromRuntimeData(ContextRuntimeData *contextRuntimeData, Uptr moduleInstanceId) {
    Compartment *compartment = getCompartmentRuntimeData(contextRuntimeData)->compartment;
    Lock<Platform::Mutex> compartmentLock(compartment->mutex);
    wavmAssert(compartment->moduleInstances.contains(moduleInstanceId));
    return compartment->moduleInstances[moduleInstanceId];
}

Table *Runtime::getTableFromRuntimeData(ContextRuntimeData *contextRuntimeData, Uptr tableId) {
    Compartment *compartment = getCompartmentRuntimeData(contextRuntimeData)->compartment;
    Lock<Platform::Mutex> compartmentLock(compartment->mutex);
    wavmAssert(compartment->tables.contains(tableId));
    return compartment->tables[tableId];
}

Memory *Runtime::getMemoryFromRuntimeData(ContextRuntimeData *contextRuntimeData, Uptr memoryId) {
    Compartment *compartment = getCompartmentRuntimeData(contextRuntimeData)->compartment;
    Lock<Platform::Mutex> compartmentLock(compartment->mutex);
    return compartment->memories[memoryId];
}
