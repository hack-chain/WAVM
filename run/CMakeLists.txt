WAVM_ADD_INSTALLED_EXECUTABLE(run Programs run.cpp)
target_link_libraries(run PRIVATE IR WASTParse Runtime Emscripten)