set(Sources
        Lexer.cpp
        Lexer.h
        Parse.cpp
        Parse.h
        ParseFunction.cpp
        ParseNumbers.cpp
        ParseModule.cpp
        )
set(PublicHeaders
        ${WAVM_INCLUDE_DIR}/WASTParse/WASTParse.h)

WAVM_ADD_LIBRARY(WASTParse ${Sources} ${PublicHeaders})
target_link_libraries(WASTParse PRIVATE IR NFA Platform RegExp)
