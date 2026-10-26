# The offline preview uses the real usercard and ChannelView, with synthetic
# in-memory messages. It deliberately does not link or invoke main.cpp/runGui.
add_executable(Leafyrino-usercard-preview Main.cpp)
if(WIN32)
    set_target_properties(Leafyrino-usercard-preview PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
target_link_libraries(Leafyrino-usercard-preview PRIVATE chatterino-lib)
target_include_directories(Leafyrino-usercard-preview PRIVATE
    "${CMAKE_SOURCE_DIR}/mocks/include")
set_target_properties(Leafyrino-usercard-preview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")

# An optional developer-only source snapshot is outside the application tree.
# It preserves the previous card layout and adds only the same offline seam.
set(LEAFYRINO_USERCARD_BASELINE_DIR "" CACHE PATH
    "Optional isolated source snapshot for the before screenshot")
if(EXISTS "${LEAFYRINO_USERCARD_BASELINE_DIR}/BaselineUserInfoPopup.cpp" AND
   EXISTS "${LEAFYRINO_USERCARD_BASELINE_DIR}/BaselineUserInfoPopup.hpp")
    add_executable(Leafyrino-usercard-baseline-preview Main.cpp
        "${LEAFYRINO_USERCARD_BASELINE_DIR}/BaselineUserInfoPopup.cpp"
        "${LEAFYRINO_USERCARD_BASELINE_DIR}/BaselineUserInfoPopup.hpp")
    if(WIN32)
        set_target_properties(Leafyrino-usercard-baseline-preview PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    target_compile_definitions(Leafyrino-usercard-baseline-preview PRIVATE
        LEAFYRINO_BASELINE_USERCARD_PREVIEW)
    target_include_directories(Leafyrino-usercard-baseline-preview PRIVATE
        "${CMAKE_SOURCE_DIR}/mocks/include"
        "${LEAFYRINO_USERCARD_BASELINE_DIR}")
    target_link_libraries(Leafyrino-usercard-baseline-preview PRIVATE chatterino-lib)
    set_target_properties(Leafyrino-usercard-baseline-preview PROPERTIES
        AUTOMOC ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ArchiveWidgetTests.cpp")
    add_executable(Leafyrino-archive-widget-tests ArchiveWidgetTests.cpp)
    if(WIN32)
        set_target_properties(Leafyrino-archive-widget-tests PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    target_link_libraries(Leafyrino-archive-widget-tests PRIVATE chatterino-lib)
    set_target_properties(Leafyrino-archive-widget-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
endif()
