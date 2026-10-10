# Pure UI logic (parsing, cascade, layout, fonts) — compiled from source so
# the tests don't link the module registration or the renderer.
add_executable(test_engine_ui
    FontRegistryTest.cpp
    HtmlCssLayoutTest.cpp
    ../Font.cpp
    ../FontRegistry.cpp
    ../HtmlParser.cpp
    ../Css.cpp
    ../Style.cpp
    ../Layout.cpp
    ../UIDocument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)
target_include_directories(test_engine_ui PRIVATE ..)
# Real fonts to load: the engine's default and a demo's.
target_compile_definitions(test_engine_ui PRIVATE JM_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(test_engine_ui PRIVATE
    engine_logger
    engine_assets
    glm
    stb_lib
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)
gtest_discover_tests(test_engine_ui
    DISCOVERY_TIMEOUT 60  # the default 5 s: too short for a cold exe on Windows CI
    PROPERTIES TIMEOUT 15
)
