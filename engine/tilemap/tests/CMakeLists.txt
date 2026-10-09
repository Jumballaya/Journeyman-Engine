# The grid and tileset are GL-free, so they build without the renderer.
add_executable(test_engine_tilemap
    TileGridTest.cpp
    ../TileGrid.cpp
    ../Tileset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)
target_include_directories(test_engine_tilemap PRIVATE ..)
target_link_libraries(test_engine_tilemap PRIVATE
    engine_logger
    nlohmann_json::nlohmann_json
    glm
    GTest::gtest_main
)
gtest_discover_tests(test_engine_tilemap
    DISCOVERY_TIMEOUT 60  # the default 5 s: too short for a cold exe on Windows CI
    PROPERTIES TIMEOUT 15
)
