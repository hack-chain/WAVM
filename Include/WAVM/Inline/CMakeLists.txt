set(PublicHeaders
        Assert.h
        BasicTypes.h
        Config.h.in
        DenseStaticIntSet.h
        Errors.h
        FloatComponents.h
        Hash.h
        HashMap.h HashMapImpl.h HashMap.natvis
        HashSet.h HashSetImpl.h HashSet.natvis
        HashTable.h HashTableImpl.h HashTable.natvis
        IndexMap.h
        Lock.h
        OptionalStorage.h
        Serialization.h
        Unicode.h)
add_custom_target(Inline SOURCES ${PublicHeaders})
set_target_properties(Inline PROPERTIES FOLDER Libraries)

add_subdirectory(xxhash)