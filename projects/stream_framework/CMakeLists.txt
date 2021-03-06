add_library(stream
	"src/cache/dummy_cache.cpp"
	"src/cache/lru_cache.cpp"
	"src/cache/lru_cache_ts.cpp"
	"src/cache/evictable.cpp"
	"src/serialize/file_serializer.cpp"
	"src/serialize/in_memory_serializer.cpp"
	"src/stats.cpp"
	"src/task_graph.cpp")
target_include_directories(stream
	PUBLIC
		"include"
	PRIVATE
		"src")

target_compile_features(stream PUBLIC cxx_std_17)
if (WIN32)
	target_compile_definitions(stream PUBLIC "NOMINMAX")
endif()

target_link_libraries(stream PUBLIC
	metrics
	EABase EASTL
	fmt::fmt
	spdlog::spdlog
	mio_cache_control
	OptickCore
	TBB::tbb TBB::tbbmalloc Threads::Threads)

#add_subdirectory("example")

if (STREAM_BUILD_TESTS)
	add_subdirectory("tests")
endif()