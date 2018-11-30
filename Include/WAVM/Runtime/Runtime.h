#pragma once

#include <memory>
#include <string>
#include <vector>

#include "WAVM/IR/Types.h"
#include "WAVM/IR/Value.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Platform/Diagnostics.h"

// Declare IR::Module to avoid including the definition.
namespace WAVM {
    namespace IR {
        struct Module;
    }
}

// Declare the different kinds of objects. They are only declared as incomplete struct types here,
// and Runtime clients will only handle opaque pointers to them.
#define DECLARE_OBJECT_TYPE(kindId, kindName, Type)                                                \
    struct Type;                                                                                   \
    RUNTIME_API Runtime::Type* as##kindName(Object* object);                                       \
    RUNTIME_API Runtime::Type* as##kindName##Nullable(Object* object);                             \
    RUNTIME_API const Runtime::Type* as##kindName(const Object* object);                           \
    RUNTIME_API const Runtime::Type* as##kindName##Nullable(const Object* object);                 \
    RUNTIME_API Object* asObject(Type* object);                                                    \
    RUNTIME_API const Object* asObject(const Runtime::Type* object);                               \
    template<> inline Runtime::Type* as<Type>(Object * object) { return as##kindName(object); }    \
    template<> inline const Runtime::Type* as<const Type>(const Object* object)                    \
    {                                                                                              \
        return as##kindName(object);                                                               \
    }

namespace WAVM {
    namespace Runtime {

        struct Object;

        // Tests whether an object is of the given type.
        RUNTIME_API bool isA(Object *object, const IR::ExternType &type);

        RUNTIME_API IR::ExternType getObjectType(Object *object);

        inline Object *asObject(Object *object) {
            return object;
        }

        inline const Object *asObject(const Object *object) {
            return object;
        }

        template<typename Type> Type *as(Object *object);

        template<typename Type> const Type *as(const Object *object);

        DECLARE_OBJECT_TYPE(ObjectKind::function, Function, Function);
        DECLARE_OBJECT_TYPE(ObjectKind::table, Table, Table);
        DECLARE_OBJECT_TYPE(ObjectKind::memory, Memory, Memory);
        DECLARE_OBJECT_TYPE(ObjectKind::global, Global, Global);
        DECLARE_OBJECT_TYPE(ObjectKind::exceptionType, ExceptionType, ExceptionType);
        DECLARE_OBJECT_TYPE(ObjectKind::moduleInstance, ModuleInstance, ModuleInstance);
        DECLARE_OBJECT_TYPE(ObjectKind::context, Context, Context);
        DECLARE_OBJECT_TYPE(ObjectKind::compartment, Compartment, Compartment);

        //
        // Garbage collection
        //

        // A GC root pointer.
        template<typename ObjectType> struct GCPointer {
            GCPointer() : value(nullptr) {
            }

            GCPointer(ObjectType *inValue) {
                value = inValue;
                if (value) {
                    addGCRoot(asObject(value));
                }
            }

            GCPointer(const GCPointer<ObjectType> &inCopy) {
                value = inCopy.value;
                if (value) {
                    addGCRoot(asObject(value));
                }
            }

            GCPointer(GCPointer<ObjectType> &&inMove) {
                value = inMove.value;
                inMove.value = nullptr;
            }

            ~GCPointer() {
                if (value) {
                    removeGCRoot(asObject(value));
                }
            }

            void operator=(ObjectType *inValue) {
                if (value) {
                    removeGCRoot(asObject(value));
                }
                value = inValue;
                if (value) {
                    addGCRoot(asObject(value));
                }
            }

            void operator=(const GCPointer<ObjectType> &inCopy) {
                if (value) {
                    removeGCRoot(asObject(value));
                }
                value = inCopy.value;
                if (value) {
                    addGCRoot(asObject(value));
                }
            }

            void operator=(GCPointer<ObjectType> &&inMove) {
                if (value) {
                    removeGCRoot(asObject(value));
                }
                value = inMove.value;
                inMove.value = nullptr;
            }

            operator ObjectType *() const {
                return value;
            }

            ObjectType &operator*() const {
                return *value;
            }

            ObjectType *operator->() const {
                return value;
            }

        private:
            ObjectType *value;
        };

        // Increments the object's counter of root references.
        RUNTIME_API void addGCRoot(Object *object);

        // Decrements the object's counter of root referencers.
        RUNTIME_API void removeGCRoot(Object *object);

        RUNTIME_API IR::UntaggedValue *invokeFunctionUnchecked(Context *context, Function *function, const IR::UntaggedValue *arguments);

        RUNTIME_API IR::ValueTuple invokeFunctionChecked(Context *context, Function *function, const std::vector<IR::Value> &arguments);

        RUNTIME_API IR::FunctionType getFunctionType(Function *function);

        RUNTIME_API Table *createTable(Compartment *compartment, IR::TableType type, std::string &&debugName);

        RUNTIME_API Object *getTableElement(Table *table, Uptr index);

        RUNTIME_API Object *setTableElement(Table *table, Uptr index, Object *newValue);

        RUNTIME_API Uptr getTableNumElements(Table *table);

        RUNTIME_API Memory *createMemory(Compartment *compartment, IR::MemoryType type, std::string &&debugName);

        RUNTIME_API U8 *getMemoryBaseAddress(Memory *memory);

        RUNTIME_API Uptr getMemoryNumPages(Memory *memory);

        RUNTIME_API Uptr getMemoryMaxPages(Memory *memory);

        RUNTIME_API Iptr growMemory(Memory *memory, Uptr numPages);

        RUNTIME_API U8 *getReservedMemoryOffsetRange(Memory *memory, Uptr offset, Uptr numBytes);

        RUNTIME_API U8 *getValidatedMemoryOffsetRange(Memory *memory, Uptr offset, Uptr numBytes);

        template<typename Value> Value &memoryRef(Memory *memory, Uptr offset) {
            return *(Value *) getValidatedMemoryOffsetRange(memory, offset, sizeof(Value));
        }

        template<typename Value> Value *memoryArrayPtr(Memory *memory, Uptr offset, Uptr numElements) {
            return (Value *) getValidatedMemoryOffsetRange(memory, offset, numElements * sizeof(Value));
        }

        RUNTIME_API Global *createGlobal(Compartment *compartment, IR::GlobalType type, IR::Value initialValue);

        struct ImportBindings {
            std::vector<Function *> functions;
            std::vector<Table *> tables;
            std::vector<Memory *> memories;
            std::vector<Global *> globals;
            std::vector<ExceptionType *> exceptionTypes;
        };

        struct Module;
        typedef std::shared_ptr<Module> ModuleRef;
        typedef const std::shared_ptr<const Module> &ModuleConstRefParam;

        RUNTIME_API ModuleRef compileModule(const IR::Module &irModule);

        RUNTIME_API ModuleInstance *instantiateModule(Compartment *compartment, ModuleConstRefParam module, ImportBindings &&imports, std::string &&debugName);

        RUNTIME_API Function *getStartFunction(ModuleInstance *moduleInstance);

        RUNTIME_API Object *getInstanceExport(ModuleInstance *moduleInstance, const std::string &name);

        RUNTIME_API Compartment *createCompartment();

        RUNTIME_API bool isInCompartment(Object *object, const Compartment *compartment);

        RUNTIME_API Context *createContext(Compartment *compartment);
    }
}
