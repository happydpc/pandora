target_sources(atlas
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/main.cpp"

        "${CMAKE_CURRENT_LIST_DIR}/ui/fps_camera_controls.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/ui/fps_camera_controls.h"
        "${CMAKE_CURRENT_LIST_DIR}/ui/framebuffer_gl.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/ui/framebuffer_gl.h"
		"${CMAKE_CURRENT_LIST_DIR}/ui/gl_error.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/ui/gl_error.h"
		"${CMAKE_CURRENT_LIST_DIR}/ui/window.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/ui/window.h")