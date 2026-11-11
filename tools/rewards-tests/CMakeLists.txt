add_executable(Leafyrino-rewards-tests Main.cpp)
target_include_directories(Leafyrino-rewards-tests PRIVATE "${CMAKE_SOURCE_DIR}/mocks/include")
target_link_libraries(Leafyrino-rewards-tests PRIVATE chatterino-lib)
set_target_properties(Leafyrino-rewards-tests PROPERTIES
    WIN32_EXECUTABLE TRUE RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
