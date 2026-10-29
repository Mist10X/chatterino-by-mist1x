# A developer-only fixture using real chat widgets and local synthetic messages.
# It never starts the normal application or reads the installed user profile.
add_executable(Leafyrino-emote-rmb-tests Main.cpp)
if(WIN32)
    set_target_properties(Leafyrino-emote-rmb-tests PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
target_include_directories(Leafyrino-emote-rmb-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/mocks/include")
target_link_libraries(Leafyrino-emote-rmb-tests PRIVATE chatterino-lib)
set_target_properties(Leafyrino-emote-rmb-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
