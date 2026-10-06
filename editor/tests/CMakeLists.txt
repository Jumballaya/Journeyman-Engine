# The editor's GL-free logic.
add_executable(test_editor_tiled
    TiledFilesTest.cpp
    ../TiledFiles.cpp
    ${CMAKE_SOURCE_DIR}/engine/tilemap/Tileset.cpp
)
target_include_directories(test_editor_tiled PRIVATE .. ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(test_editor_tiled PRIVATE
    nlohmann_json::nlohmann_json
    glm
    GTest::gtest_main
)
gtest_discover_tests(test_editor_tiled
    PROPERTIES TIMEOUT 15
)
