#include <string.h>
#include <atomic>
#include <memory>

#include "RuntimePrivate.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Inline/Serialization.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Runtime;

static Value evaluateInitializer(const std::vector<Global *> &moduleGlobals, InitializerExpression expression) {
    switch (expression.type) {
        case InitializerExpression::Type::i32_const:
            return expression.i32;
        case InitializerExpression::Type::i64_const:
            return expression.i64;
        case InitializerExpression::Type::f32_const:
            return expression.f32;
        case InitializerExpression::Type::f64_const:
            return expression.f64;
        case InitializerExpression::Type::v128_const:
            return expression.v128;
        case InitializerExpression::Type::get_global: {
            // Find the import this refers to.
            errorUnless(expression.globalRef < moduleGlobals.size());
            Global *global = moduleGlobals[expression.globalRef];
            errorUnless(global);
            errorUnless(!global->type.isMutable);
            return IR::Value(global->type.valueType, global->initialValue);
        }
        case InitializerExpression::Type::ref_null:
            return nullptr;
        default:
            Errors::unreachable();
    };
}

ModuleRef Runtime::compileModule(const IR::Module &irModule) {
    std::vector<U8> objectCode = LLVMJIT::compileModule(irModule);
    return std::make_shared<Module>(IR::Module(irModule), std::move(objectCode));
}

ModuleInstance::~ModuleInstance() {
    if (id != UINTPTR_MAX) {
        compartment->moduleInstances.removeOrFail(id);
    }
}

ModuleInstance *Runtime::instantiateModule(Compartment *compartment, ModuleConstRefParam module, ImportBindings &&imports, std::string &&moduleDebugName) {
    Uptr id = UINTPTR_MAX;
    {
        Lock<Platform::Mutex> compartmentLock(compartment->mutex);
        id = compartment->moduleInstances.add(UINTPTR_MAX, nullptr);
    }
    if (id == UINTPTR_MAX) {
        return nullptr;
    }

    // Check the type of the ModuleInstance's imports.
    std::vector<Function *> functions = std::move(imports.functions);
    errorUnless(functions.size() == module->ir.functions.imports.size());
    for (Uptr importIndex = 0; importIndex < module->ir.functions.imports.size(); ++importIndex) {
        Object *importObject = asObject(functions[importIndex]);
        errorUnless(isA(importObject, module->ir.types[module->ir.functions.getType(importIndex).index]));
        errorUnless(isInCompartment(importObject, compartment));
    }

    std::vector<Table *> tables = std::move(imports.tables);
    errorUnless(tables.size() == module->ir.tables.imports.size());
    for (Uptr importIndex = 0; importIndex < module->ir.tables.imports.size(); ++importIndex) {
        Object *importObject = asObject(tables[importIndex]);
        errorUnless(isA(importObject, module->ir.tables.getType(importIndex)));
        errorUnless(isInCompartment(importObject, compartment));
    }

    std::vector<Memory *> memories = std::move(imports.memories);
    errorUnless(memories.size() == module->ir.memories.imports.size());
    for (Uptr importIndex = 0; importIndex < module->ir.memories.imports.size(); ++importIndex) {
        Object *importObject = asObject(memories[importIndex]);
        errorUnless(isA(importObject, module->ir.memories.getType(importIndex)));
        errorUnless(isInCompartment(importObject, compartment));
    }

    std::vector<Global *> globals = std::move(imports.globals);
    errorUnless(globals.size() == module->ir.globals.imports.size());
    for (Uptr importIndex = 0; importIndex < module->ir.globals.imports.size(); ++importIndex) {
        Object *importObject = asObject(globals[importIndex]);
        errorUnless(isA(importObject, module->ir.globals.getType(importIndex)));
        errorUnless(isInCompartment(importObject, compartment));
    }

    std::vector<ExceptionType *> exceptionTypes = std::move(imports.exceptionTypes);
    errorUnless(exceptionTypes.size() == module->ir.exceptionTypes.imports.size());
    for (Uptr importIndex = 0; importIndex < module->ir.exceptionTypes.imports.size(); ++importIndex) {
        Object *importObject = asObject(exceptionTypes[importIndex]);
        errorUnless(isA(importObject, module->ir.exceptionTypes.getType(importIndex)));
        errorUnless(isInCompartment(importObject, compartment));
    }

    // Deserialize the disassembly names.
    DisassemblyNames disassemblyNames;
    getDisassemblyNames(module->ir, disassemblyNames);

    // Instantiate the module's memory and table definitions.
    for (Uptr tableDefIndex = 0; tableDefIndex < module->ir.tables.defs.size(); ++tableDefIndex) {
        std::string debugName = disassemblyNames.tables[module->ir.tables.imports.size() + tableDefIndex];
        auto table = createTable(compartment, module->ir.tables.defs[tableDefIndex].type, std::move(debugName));
        tables.push_back(table);
    }
    for (Uptr memoryDefIndex = 0; memoryDefIndex < module->ir.memories.defs.size(); ++memoryDefIndex) {
        std::string debugName = disassemblyNames.memories[module->ir.memories.imports.size() + memoryDefIndex];
        auto memory = createMemory(compartment, module->ir.memories.defs[memoryDefIndex].type, std::move(debugName));

        memories.push_back(memory);
    }

    // Instantiate the module's global definitions.
    for (const GlobalDef &globalDef : module->ir.globals.defs) {
        const Value initialValue = evaluateInitializer(globals, globalDef.initializer);
        errorUnless(isSubtype(initialValue.type, globalDef.type.valueType));
        globals.push_back(createGlobal(compartment, globalDef.type, initialValue));
    }

    // Instantiate the module's exception types.
    for (Uptr exceptionTypeDefIndex = 0;
         exceptionTypeDefIndex < module->ir.exceptionTypes.defs.size(); ++exceptionTypeDefIndex) {
        const ExceptionTypeDef &exceptionTypeDef = module->ir.exceptionTypes.defs[exceptionTypeDefIndex];
        std::string debugName = disassemblyNames.exceptionTypes[module->ir.exceptionTypes.imports.size() +
                                                                exceptionTypeDefIndex];
    }

    // Set up the values to bind to the symbols in the LLVMJIT object code.
    HashMap<std::string, LLVMJIT::FunctionBinding> wavmIntrinsicsExportMap;
    for (const HashMapPair<std::string, Intrinsics::Function *> &intrinsicFunctionPair :
            Intrinsics::getUninstantiatedFunctions(INTRINSIC_MODULE_REF(wavmIntrinsics))) {
        LLVMJIT::FunctionBinding functionBinding{intrinsicFunctionPair.value->getCallingConvention(), intrinsicFunctionPair.value->getNativeFunction()};
        wavmIntrinsicsExportMap.add(intrinsicFunctionPair.key, functionBinding);
    }

    std::vector<LLVMJIT::FunctionBinding> jitFunctionImports;
    for (Uptr importIndex = 0; importIndex < module->ir.functions.imports.size(); ++importIndex) {
        jitFunctionImports.push_back({CallingConvention::wasm, const_cast<U8 *>(functions[importIndex]->code)});
    }

    std::vector<LLVMJIT::TableBinding> jitTables;
    for (Table *table : tables) {
        jitTables.push_back({table->id});
    }

    std::vector<LLVMJIT::MemoryBinding> jitMemories;
    for (Memory *memory : memories) {
        jitMemories.push_back({memory->id});
    }

    std::vector<LLVMJIT::GlobalBinding> jitGlobals;
    for (Global *global : globals) {
        LLVMJIT::GlobalBinding globalSpec;
        globalSpec.type = global->type;
        if (global->type.isMutable) {
            globalSpec.mutableGlobalIndex = global->mutableGlobalIndex;
        } else {
            globalSpec.immutableValuePointer = &global->initialValue;
        }
        jitGlobals.push_back(globalSpec);
    }

    std::vector<LLVMJIT::ExceptionTypeBinding> jitExceptionTypes;
    for (ExceptionType *exceptionType : exceptionTypes) {
        jitExceptionTypes.push_back({exceptionType->id});
    }

    // Create a FunctionMutableData for each function definition.
    std::vector<FunctionMutableData *> functionDefMutableDatas;
    for (Uptr functionDefIndex = 0; functionDefIndex < module->ir.functions.defs.size(); ++functionDefIndex) {
        std::string debugName = disassemblyNames.functions[module->ir.functions.imports.size() + functionDefIndex].name;
        if (!debugName.size()) {
            debugName = "<function #" + std::to_string(functionDefIndex) + ">";
        }
        debugName = "wasm!" + moduleDebugName + '!' + debugName;

        functionDefMutableDatas.push_back(new FunctionMutableData(std::move(debugName)));
    }

    // Load the compiled module's object code with this module instance's imports.
    std::vector<FunctionType> jitTypes = module->ir.types;
    std::vector<Runtime::Function *> jitFunctionDefs;
    jitFunctionDefs.resize(module->ir.functions.defs.size(), nullptr);
    std::shared_ptr<LLVMJIT::Module> jitModule = LLVMJIT::loadModule(module->objectCode, std::move(wavmIntrinsicsExportMap), std::move(jitTypes), std::move(jitFunctionImports), std::move(jitTables), std::move(jitMemories), std::move(jitGlobals), std::move(jitExceptionTypes), {id}, reinterpret_cast<Uptr>(getOutOfBoundsElement()), functionDefMutableDatas);

    // LLVMJIT::loadModule filled in the functionDefMutableDatas' function pointers with the
    // compiled functions. Add those functions to the module.
    for (FunctionMutableData *functionMutableData : functionDefMutableDatas) {
        functions.push_back(functionMutableData->function);
    }

    // Set up the instance's exports.
    HashMap<std::string, Object *> exportMap;
    for (const Export &exportIt : module->ir.exports) {
        Object *exportedObject = nullptr;
        switch (exportIt.kind) {
            case IR::ExternKind::function:
                exportedObject = asObject(functions[exportIt.index]);
                break;
            case IR::ExternKind::table:
                exportedObject = tables[exportIt.index];
                break;
            case IR::ExternKind::memory:
                exportedObject = memories[exportIt.index];
                break;
            case IR::ExternKind::global:
                exportedObject = globals[exportIt.index];
                break;
            case IR::ExternKind::exceptionType:
                exportedObject = exceptionTypes[exportIt.index];
                break;
            default:
                Errors::unreachable();
        }
        exportMap.addOrFail(exportIt.name, exportedObject);
    }

    // Copy the module's passive data and table segments into the ModuleInstance for later use.
    PassiveDataSegmentMap passiveDataSegments;
    PassiveElemSegmentMap passiveElemSegments;
    for (Uptr segmentIndex = 0; segmentIndex < module->ir.dataSegments.size(); ++segmentIndex) {
        const DataSegment &dataSegment = module->ir.dataSegments[segmentIndex];
        if (!dataSegment.isActive) {
            passiveDataSegments.add(segmentIndex, std::make_shared<std::vector<U8>>(dataSegment.data));
        }
    }
    for (Uptr segmentIndex = 0; segmentIndex < module->ir.elemSegments.size(); ++segmentIndex) {
        const ElemSegment &elemSegment = module->ir.elemSegments[segmentIndex];
        if (!elemSegment.isActive) {
            auto passiveElemSegmentObjects = std::make_shared<std::vector<Object *>>();
            for (Uptr functionIndex : elemSegment.indices) {
                passiveElemSegmentObjects->push_back(asObject(functions[functionIndex]));
            }
            passiveElemSegments.add(segmentIndex, passiveElemSegmentObjects);
        }
    }

    // Look up the module's start function.
    Function *startFunction = nullptr;
    if (module->ir.startFunctionIndex != UINTPTR_MAX) {
        startFunction = functions[module->ir.startFunctionIndex];
        wavmAssert(FunctionType(startFunction->encodedType) == FunctionType());
    }

    // Create the ModuleInstance and add it to the compartment's modules list.
    ModuleInstance *moduleInstance = new ModuleInstance(compartment, id, std::move(exportMap), std::move(functions), std::move(tables), std::move(memories), std::move(globals), std::move(exceptionTypes), startFunction, std::move(passiveDataSegments), std::move(passiveElemSegments), std::move(jitModule), std::move(moduleDebugName));
    {
        Lock<Platform::Mutex> compartmentLock(compartment->mutex);
        compartment->moduleInstances[id] = moduleInstance;
    }

    // Copy the module's data segments into their designated memory instances.
    for (const DataSegment &dataSegment : module->ir.dataSegments) {
        if (dataSegment.isActive) {
            Memory *memory = moduleInstance->memories[dataSegment.memoryIndex];

            const Value baseOffsetValue = evaluateInitializer(moduleInstance->globals, dataSegment.baseOffset);
            errorUnless(baseOffsetValue.type == ValueType::i32);
            const U32 baseOffset = baseOffsetValue.i32;

            if (dataSegment.data.size()) {
                Platform::bytewiseMemCopy(
                        memory->baseAddress + baseOffset, dataSegment.data.data(), dataSegment.data.size());
            }
        }
    }

    // Copy the module's elem segments into their designated table instances.
    for (const ElemSegment &elemSegment : module->ir.elemSegments) {
        if (elemSegment.isActive) {
            Table *table = moduleInstance->tables[elemSegment.tableIndex];

            const Value baseOffsetValue = evaluateInitializer(moduleInstance->globals, elemSegment.baseOffset);
            errorUnless(baseOffsetValue.type == ValueType::i32);
            const U32 baseOffset = baseOffsetValue.i32;

            if (elemSegment.indices.size()) {
                for (Uptr index = 0; index < elemSegment.indices.size(); ++index) {
                    const Uptr functionIndex = elemSegment.indices[index];
                    wavmAssert(functionIndex < moduleInstance->functions.size());
                    Function *function = moduleInstance->functions[functionIndex];
                    setTableElement(table, baseOffset + index, asObject(function));
                }
            }
        }
    }

    return moduleInstance;
}

Function *Runtime::getStartFunction(ModuleInstance *moduleInstance) {
    return moduleInstance->startFunction;
}

Object *Runtime::getInstanceExport(ModuleInstance *moduleInstance, const std::string &name) {
    wavmAssert(moduleInstance);
    Object *const *exportedObjectPtr = moduleInstance->exportMap.get(name);
    return exportedObjectPtr ? *exportedObjectPtr : nullptr;
}
