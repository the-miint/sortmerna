#===============================================================================
# CMakeLists.txt for smr_api static library
#===============================================================================

add_library(smr_api STATIC smr_api.cpp)

target_include_directories(smr_api
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CONCURRENTQUEUE_HOME}
)

target_link_libraries(smr_api
    PUBLIC
        smr_objs
)
