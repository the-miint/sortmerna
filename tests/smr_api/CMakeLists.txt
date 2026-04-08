#===============================================================================
# CMakeLists.txt for smr_api tests
#===============================================================================

# Compile as C to verify smr_api.h is C-linkable
add_executable(test_smr_api test_smoke.c)

target_include_directories(test_smr_api
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CONCURRENTQUEUE_HOME}
)

target_link_libraries(test_smr_api
    smr_api
    build_version
    alp
    $<TARGET_OBJECTS:cmph>
    ${CMAKE_DL_LIBS}
)

# Wire golden data directory as a compile definition
target_compile_definitions(test_smr_api
    PRIVATE
        SMR_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden"
        SMR_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)

add_test(NAME smr_api_smoke COMMAND test_smr_api)
