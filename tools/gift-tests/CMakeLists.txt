add_executable(Leafyrino-gift-tests Main.cpp)
target_include_directories(Leafyrino-gift-tests PRIVATE "${CMAKE_SOURCE_DIR}/mocks/include")
target_link_libraries(Leafyrino-gift-tests PRIVATE chatterino-lib)
set_target_properties(Leafyrino-gift-tests PROPERTIES
    WIN32_EXECUTABLE TRUE RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
