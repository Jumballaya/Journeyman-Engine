file(GLOB_RECURSE LOGGER_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB_RECURSE LOGGER_HEADERS CONFIGURE_DEPENDS
    *.hpp
)

add_library(engine_logger STATIC ${LOGGER_SOURCES} ${LOGGER_HEADERS})
target_include_directories(engine_logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_logger PUBLIC spdlog::spdlog)
target_compile_features(engine_logger PUBLIC cxx_std_20)