add_executable(pandoraTest
    #${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_contiguous_allocator_ts.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_free_list_backed_memory_arena.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_growing_free_list_ts.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_memory_arena.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_memory_arena_ts.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_triangle.cpp)

target_link_libraries(pandoraTest PRIVATE GTest::GTest GTest::Main libPandora)
target_compile_features(pandoraTest PRIVATE cxx_std_17)

if (MSVC)
    target_compile_options(pandoraTest PUBLIC "/wd4251" "/wd4275") # Gtest warnings about dll stuff
endif()

enable_testing()
gtest_add_tests(TARGET pandoraTest)