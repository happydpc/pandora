add_executable(torque "")
include(${CMAKE_CURRENT_LIST_DIR}/src/CMakeLists.txt)
target_include_directories(torque PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/)
target_compile_features(torque PRIVATE cxx_std_17)

target_link_libraries(torque PRIVATE 
    libPandora
	libPbrt_importer2
	libPbf_importer
	OIIO::OpenImageIO
	Boost::program_options
	OptickCore
	glm)
