#include <stdint.h>
#include <string.h>
#include <vector>
#include <iostream>

#include "RuntimePrivate.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Platform/Memory.h"

using namespace WAVM;
using namespace WAVM::Runtime;

// Global lists of tables; used to query whether an address is reserved by one of them.
static Platform::Mutex tablesMutex;
static std::vector<Table *> tables;

enum {
    numGuardPages = 1
};

static Uptr getNumPlatformPages(Uptr numBytes) {
    return (numBytes + (Uptr(1) << Platform::getPageSizeLog2()) - 1) >> Platform::getPageSizeLog2();
}

static Function *makeDummyFunction(const char *debugName) {
    FunctionMutableData *functionMutableData = new FunctionMutableData(debugName);
    Function *function = new Function(functionMutableData, UINTPTR_MAX, IR::FunctionType::Encoding{0});
    functionMutableData->function = function;
    return function;
}

Object *Runtime::getOutOfBoundsElement() {
    static Function *function = makeDummyFunction("out-of-bounds table element");
    return asObject(function);
}

static Object *getUninitializedElement() {
    static Function *function = makeDummyFunction("uninitialized table element");
    return asObject(function);
}

static Uptr objectToBiasedTableElementValue(Object *object) {
    return reinterpret_cast<Uptr>(object) - reinterpret_cast<Uptr>(getOutOfBoundsElement());
}

static Object *biasedTableElementValueToObject(Uptr biasedValue) {
    return reinterpret_cast<Object *>(biasedValue + reinterpret_cast<Uptr>(getOutOfBoundsElement()));
}

static Table *createTableImpl(Compartment *compartment, IR::TableType type, std::string &&debugName) {
    Table *table = new Table(compartment, type, std::move(debugName));

    // In 64-bit, allocate enough address-space to safely access 32-bit table indices without bounds
    // checking, or 16MB (4M elements) if the host is 32-bit.
    const Uptr pageBytesLog2 = Platform::getPageSizeLog2();
    const U64 tableMaxElements = Uptr(1) << 32;
    const U64 tableMaxBytes = sizeof(Table::Element) * tableMaxElements;
    const U64 tableMaxPages = tableMaxBytes >> pageBytesLog2;

    table->elements = (Table::Element *) Platform::allocateVirtualPages(tableMaxPages + numGuardPages);
    table->numReservedBytes = tableMaxBytes;
    table->numReservedElements = tableMaxElements;
    if (!table->elements) {
        delete table;
        return nullptr;
    }

    // Add the table to the global array.
    {
        Lock<Platform::Mutex> tablesLock(tablesMutex);
        tables.push_back(table);
    }
    return table;
}

static Iptr growTableImpl(Table *table, Uptr numElementsToGrow, bool initializeNewElements) {
    if (!numElementsToGrow) {
        return table->numElements.load(std::memory_order_acquire);
    }

    Lock<Platform::Mutex> resizingLock(table->resizingMutex);

    const Uptr previousNumElements = table->numElements.load(std::memory_order_acquire);

    // If the growth would cause the table's size to exceed its maximum, return -1.
    if (numElementsToGrow > table->type.size.max || previousNumElements > table->type.size.max - numElementsToGrow ||
        numElementsToGrow > IR::maxTableElems || previousNumElements > IR::maxTableElems - numElementsToGrow) {
        return -1;
    }

    // Try to commit pages for the new elements, and return -1 if the commit fails.
    const Uptr newNumElements = previousNumElements + numElementsToGrow;
    const Uptr previousNumPlatformPages = getNumPlatformPages(previousNumElements * sizeof(Table::Element));
    const Uptr newNumPlatformPages = getNumPlatformPages(newNumElements * sizeof(Table::Element));
    if (newNumPlatformPages != previousNumPlatformPages && !Platform::commitVirtualPages(
            (U8 *) table->elements + (previousNumPlatformPages << Platform::getPageSizeLog2()),
            newNumPlatformPages - previousNumPlatformPages)) {
        return -1;
    }

    if (initializeNewElements) {
        // Write the uninitialized sentinel value to the new elements.
        for (Uptr elementIndex = previousNumElements; elementIndex < newNumElements; ++elementIndex) {
            table->elements[elementIndex].biasedValue.store(objectToBiasedTableElementValue(getUninitializedElement()), std::memory_order_release);
        }
    }

    table->numElements.store(newNumElements, std::memory_order_release);
    return previousNumElements;
}

Table *Runtime::createTable(Compartment *compartment, IR::TableType type, std::string &&debugName) {
    wavmAssert(type.size.min <= UINTPTR_MAX);
    Table *table = createTableImpl(compartment, type, std::move(debugName));
    if (!table) {
        return nullptr;
    }

    // Grow the table to the type's minimum size.
    if (growTableImpl(table, Uptr(type.size.min), true) == -1) {
        delete table;
        return nullptr;
    }

    // Add the table to the compartment's tables IndexMap.
    {
        Lock<Platform::Mutex> compartmentLock(compartment->mutex);

        table->id = compartment->tables.add(UINTPTR_MAX, table);
        if (table->id == UINTPTR_MAX) {
            delete table;
            return nullptr;
        }
        compartment->runtimeData->tableBases[table->id] = table->elements;
    }

    return table;
}

Table::~Table() {
    if (id != UINTPTR_MAX) {

        wavmAssert(compartment->tables[id] == this);
        compartment->tables.removeOrFail(id);

        wavmAssert(compartment->runtimeData->tableBases[id] == elements);
        compartment->runtimeData->tableBases[id] = nullptr;
    }

    // Remove the table from the global array.
    {
        Lock<Platform::Mutex> tablesLock(tablesMutex);
        for (Uptr tableIndex = 0; tableIndex < tables.size(); ++tableIndex) {
            if (tables[tableIndex] == this) {
                tables.erase(tables.begin() + tableIndex);
                break;
            }
        }
    }

    // Free the virtual address space.
    const Uptr pageBytesLog2 = Platform::getPageSizeLog2();
    if (numReservedBytes > 0) {
        Platform::freeVirtualPages((U8 *) elements, (numReservedBytes >> pageBytesLog2) + numGuardPages);
    }
    elements = nullptr;
    numElements = numReservedBytes = numReservedElements = 0;
}

static Object *setTableElementNonNull(Table *table, Uptr index, Object *object) {
    wavmAssert(object);

    // Use a saturated index to access the table data to ensure that it's harmless for the CPU to
    // speculate past the above bounds check.
    const Uptr saturatedIndex = Platform::saturateToBounds(index, U64(table->numReservedElements) - 1);

    // Compute the biased value to store in the table.
    const Uptr biasedValue = objectToBiasedTableElementValue(object);

    // Atomically replace the table element, throwing an out-of-bounds exception before the write if
    // the element being replaced is an out-of-bounds sentinel value.
    Uptr oldBiasedValue = table->elements[saturatedIndex].biasedValue;
    while (true) {
        if (table->elements[saturatedIndex].biasedValue.compare_exchange_weak(oldBiasedValue, biasedValue, std::memory_order_acq_rel)) {
            break;
        }
    };

    return biasedTableElementValueToObject(oldBiasedValue);
}

static Object *getTableElementNonNull(Table *table, Uptr index) {
    // Verify the index is within the table's bounds.

    // Use a saturated index to access the table data to ensure that it's harmless for the CPU to
    // speculate past the above bounds check.
    const Uptr saturatedIndex = Platform::saturateToBounds(index, U64(table->numReservedElements) - 1);

    // Read the table element.
    const Uptr biasedValue = table->elements[saturatedIndex].biasedValue.load(std::memory_order_acquire);
    Object *object = biasedTableElementValueToObject(biasedValue);

    wavmAssert(object);
    return object;
}

Object *Runtime::setTableElement(Table *table, Uptr index, Object *newValue) {
    wavmAssert(!newValue || isInCompartment(newValue, table->compartment));

    // If the new value is null, write the uninitialized sentinel value instead.
    if (!newValue) {
        newValue = getUninitializedElement();
    }

    // Write the table element.
    Object *oldObject = setTableElementNonNull(table, index, newValue);

    // If the old table element was the uninitialized sentinel value, return null.
    return oldObject == getUninitializedElement() ? nullptr : oldObject;
}

Object *Runtime::getTableElement(Table *table, Uptr index) {
    Object *object = getTableElementNonNull(table, index);

    // If the old table element was the uninitialized sentinel value, return null.
    return object == getUninitializedElement() ? nullptr : object;
}

Uptr Runtime::getTableNumElements(Table *table) {
    return table->numElements.load(std::memory_order_acquire);
}

DEFINE_INTRINSIC_FUNCTION(wavmIntrinsics, "table.get", Object*, table_get, U32 index, Uptr tableId) {
    Table *table = getTableFromRuntimeData(contextRuntimeData, tableId);
    return getTableElement(table, index);
}

DEFINE_INTRINSIC_FUNCTION(wavmIntrinsics, "table.set", void, table_set, U32 index, Object *value, Uptr tableId) {
    Table *table = getTableFromRuntimeData(contextRuntimeData, tableId);
    setTableElement(table, index, value);
}

DEFINE_INTRINSIC_FUNCTION(wavmIntrinsics, "table.init", void, table_init, U32 destOffset, U32 sourceOffset, U32 numElements, Uptr moduleInstanceId, Uptr tableId, Uptr elemSegmentIndex) {
    ModuleInstance *moduleInstance = getModuleInstanceFromRuntimeData(contextRuntimeData, moduleInstanceId);
    Lock<Platform::Mutex> passiveElemSegmentsLock(moduleInstance->passiveElemSegmentsMutex);

    if (!moduleInstance->passiveElemSegments.contains(elemSegmentIndex)) {
        passiveElemSegmentsLock.unlock();
    } else {
        // Copy the passive elem segment shared_ptr, and unlock the mutex. It's important to
        // explicitly unlock the mutex before calling setTableElement, as setTableElement trigger a
        // signal that will unwind the stack without calling the Lock destructor.
        std::shared_ptr<const std::vector<Object *>> passiveElemSegmentObjects = moduleInstance->passiveElemSegments[elemSegmentIndex];
        passiveElemSegmentsLock.unlock();

        Table *table = getTableFromRuntimeData(contextRuntimeData, tableId);

        for (Uptr index = 0; index < numElements; ++index) {
            const U64 sourceIndex = U64(sourceOffset) + index;
            const U64 destIndex = U64(destOffset) + index;

            setTableElement(table, Uptr(destIndex), asObject((*passiveElemSegmentObjects)[sourceIndex]));
        }
    }
}
