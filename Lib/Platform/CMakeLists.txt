set(POSIXSources
        POSIX/Diagnostics.cpp
        POSIX/Event.cpp
        POSIX/Exception.cpp
        POSIX/Memory.cpp
        POSIX/Mutex.cpp
        POSIX/POSIX.S
        POSIX/POSIXPrivate.h)


set(PublicHeaders
        ${WAVM_INCLUDE_DIR}/Platform/Defines.h
        ${WAVM_INCLUDE_DIR}/Platform/Diagnostics.h
        ${WAVM_INCLUDE_DIR}/Platform/Event.h
        ${WAVM_INCLUDE_DIR}/Platform/Exception.h
        ${WAVM_INCLUDE_DIR}/Platform/Intrinsic.h
        ${WAVM_INCLUDE_DIR}/Platform/Memory.h
        ${WAVM_INCLUDE_DIR}/Platform/Mutex.h)

if (MSVC)
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set_source_files_properties(Windows/Win64.asm PROPERTIES HEADER_FILE_ONLY TRUE)
        set_source_files_properties(Windows/Win32.asm PROPERTIES COMPILE_FLAGS "/safeseh")
    else ()
        set_source_files_properties(Windows/Win32.asm PROPERTIES HEADER_FILE_ONLY TRUE)
    endif ()
    set_source_files_properties(${POSIXSources} PROPERTIES HEADER_FILE_ONLY TRUE)
    enable_language(ASM_MASM)

    set_source_files_properties(${POSIXSources} PROPERTIES HEADER_FILE_ONLY TRUE)
else ()
    set(Sources ${POSIXSources})

    # Using if(CMAKE_SYSTEM_NAME STREQUAL Wavix) doesn't work because Wavix is interpreted as a
    # reference to the Wavix target in this project.
    string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" Wavix IS_WAVIX)
    if (IS_WAVIX)
        set_source_files_properties(POSIX/POSIX.S PROPERTIES HEADER_FILE_ONLY TRUE)
    endif ()

    set_source_files_properties(PROPERTIES HEADER_FILE_ONLY TRUE)
endif ()

WAVM_ADD_LIBRARY(Platform ${POSIXSources} ${PublicHeaders})

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(Platform PRIVATE pthread rt)
endif ()

if (NOT MSVC AND WAVM_ENABLE_RUNTIME)
    target_link_libraries(Platform PRIVATE dl WAVMUnwind)

    # This can't be set in the WAVMUnwind target's public include directories without adding the
    # libunwind headers to the install, so just set it manually on Platform
    target_include_directories(Platform PRIVATE ${WAVM_SOURCE_DIR}/ThirdParty/libunwind/include)
endif ()
