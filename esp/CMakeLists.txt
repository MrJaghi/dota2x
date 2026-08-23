cmake_minimum_required(VERSION 3.15)
project(AzhengESP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)

add_executable(AzhengESP
	main.cpp
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
	${IMGUI_DIR}/backends/imgui_impl_win32.cpp
	${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(AzhengESP PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)

target_link_libraries(AzhengESP PRIVATE user32 gdi32 d3d11 dxgi)

add_library(AzhengInternal SHARED
	internal/main.cpp
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
	${IMGUI_DIR}/backends/imgui_impl_win32.cpp
	${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(AzhengInternal PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(AzhengInternal PRIVATE user32 gdi32 d3d11 dxgi)
target_compile_options(AzhengInternal PRIVATE /EHa)

add_executable(AzhengInjector internal/Injector.cpp)
target_link_libraries(AzhengInjector PRIVATE user32)

add_custom_command(TARGET AzhengInternal POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/assets
		$<TARGET_FILE_DIR:AzhengInternal>/assets
)
