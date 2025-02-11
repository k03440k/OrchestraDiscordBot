#Dependincies: download and install via vcpkg dpp:64x-windows, ffmpeg:64x-windows
#cmake --build . --config Release --target OrchestraDiscordBot
#cmake --build . --config Debug --target OrchestraDiscordBot
cmake_minimum_required(VERSION 3.26.4)

set(CMAKE_CXX_STANDARD 20)

set(PROJECT_NAME OrchestraDiscordBot)

project(${PROJECT_NAME})

set(PROJECT_HEADERS
	"Source/FFmpeg/FFmpegUniquePtrManager.hpp"
	"Source/FFmpeg/Decoder.hpp"

	"Source/DiscordBot/Command.hpp"
	"Source/DiscordBot/DiscordBot.hpp"
	"Source/DiscordBot/OrchestraDiscordBot.hpp"
	"Source/DiscordBot/Player.hpp"

	"Source/Workers/Worker.hpp"
	"Source/Workers/WorkersManager.hpp"

	"Source/Utils.hpp"
	)
set(PROJECT_SOURCES
	"Source/FFmpeg/FFmpegUniquePtrManager.cpp"
	"Source/FFmpeg/Decoder.cpp"
	
	"Source/DiscordBot/Command.cpp"
	"Source/DiscordBot/DiscordBot.cpp"
	"Source/DiscordBot/OrchestraDiscordBot.cpp"
	"Source/DiscordBot/Player.cpp"

	"Source/main.cpp"
	)
set(PROJECT_CODE
	${PROJECT_HEADERS}
	${PROJECT_SOURCES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${PROJECT_CODE}
)

add_executable(${PROJECT_NAME} ${PROJECT_CODE})

#TODO: this
if(DEFINED VCPKG_ROOT)
    message(STATUS "Using vcpkg at: ${VCPKG_ROOT}")
	
	include("${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
	
	#adjust if you want Debug or Release .dlls, because I didn't find a way to do it in CMake ._.
	#Release
	#set(DLL_PATH "${VCPKG_ROOT}/installed/x64-windows/bin")
	#Debug
	set(DLL_PATH "${VCPKG_ROOT}/installed/x64-windows/debug/bin")
	
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		set(DLL_PATH "${VCPKG_ROOT}/installed/x64-windows/bin")
	elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
		set(DLL_PATH "${VCPKG_ROOT}/installed/x64-windows/debug/bin")
	endif()
	
else()
    message(WARNING "VCPKG_ROOT is not set! It may not build, so it is better to pass with -DVCPKG_ROOT=<path>")
endif()

function(CopyDLL libName)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${DLL_PATH}/${libName}
    $<TARGET_FILE_DIR:${PROJECT_NAME}>)
endfunction()

# -- ffmpeg
#I really do not know how to build FFmpeg with CMake or do other stuff, so here is my option...:
find_package(FFmpeg REQUIRED)

find_path(AVCODEC_INCLUDE_DIR "libavcodec/avcodec.h")
find_library(AVCODEC_LIBRARY avcodec)

find_path(AVFORMAT_INCLUDE_DIR "libavformat/avformat.h")
find_library(AVFORMAT_LIBRARY avformat)

find_path(AVUTIL_INCLUDE_DIR "libavutil/avutil.h")
find_library(AVUTIL_LIBRARY avutil)

find_path(AVDEVICE_INCLUDE_DIR "libavdevice/avdevice.h")
find_library(AVDEVICE_LIBRARY avdevice)

find_path(AVFILTER_INCLUDE_DIR "libavfilter/avfilter.h")
find_library(AVFILTER_LIBRARY NAMES avfilter )

find_path(SWSCALE_INCLUDE_DIR "libswscale/swscale.h")
find_library(SWSCALE_LIBRARY NAMES swscale )

find_path(SWRESAMPLE_INCLUDE_DIR "libswresample/swresample.h")
find_library(SWRESAMPLE_LIBRARY NAMES swresample )

set(FFmpeg_INCLUDE_DIRS ${AVCODEC_INCLUDE_DIR} ${AVFORMAT_INCLUDE_DIR} ${AVUTIL_INCLUDE_DIR}
                     ${AVDEVICE_INCLUDE_DIR} ${AVFILTER_INCLUDE_DIR} ${SWSCALE_INCLUDE_DIR} ${SWRESAMPLE_INCLUDE_DIR})

set(FFmpeg_LIBS ${AVUTIL_LIBRARY} ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVDEVICE_LIBRARY}
                ${AVFILTER_LIBRARY} ${SWSCALE_LIBRARY} ${SWRESAMPLE_LIBRARY})

target_link_libraries(${PROJECT_NAME} PUBLIC ${FFmpeg_LIBS})
include_directories(${FFmpeg_INCLUDE_DIRS})

#ffmpeg
CopyDLL("avutil-58.dll")
CopyDLL("swresample-4.dll")
CopyDLL("swscale-7.dll")
CopyDLL("avcodec-60.dll")
CopyDLL("avfilter-9.dll")
CopyDLL("avformat-60.dll")
CopyDLL("avdevice-60.dll")
# -- ffmpeg

# -- dpp
find_package(dpp CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC dpp::dpp)

CopyDLL("dpp.dll")
CopyDLL("libsodium.dll")
CopyDLL("libcrypto-3-x64.dll")
CopyDLL("libssl-3-x64.dll")
CopyDLL("opus.dll")

#adjust if you want Debug or Release .dlls, because I didn't find a way to do it in CMake ._.
#Release
#CopyDLL("zlib1.dll")
#Debug
CopyDLL("zlibd1.dll")

if(CMAKE_BUILD_TYPE EQUAL "Release")
    CopyDLL("zlib1.dll")
elseif(CMAKE_BUILD_TYPE EQUAL "Debug")
    CopyDLL("zlibd1.dll")
endif()
# -- dpp

# -- GuelderConsoleLog
add_subdirectory("External/GuelderConsoleLog" "${CMAKE_CURRENT_BINARY_DIR}/GuelderConsoleLog")
target_link_libraries(${PROJECT_NAME} PUBLIC GuelderConsoleLog)
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}/External/GuelderConsoleLog/include")
# -- GuelderConsoleLog

# -- GuelderResourcesManager
add_subdirectory("External/GuelderResourcesManager" "${CMAKE_CURRENT_BINARY_DIR}/GuelderResourcesManager")
target_link_libraries(${PROJECT_NAME} PUBLIC GuelderResourcesManager)
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}/External/GuelderResourcesManager/include")
# -- GuelderResourcesManager

#copying Resources directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	"${CMAKE_SOURCE_DIR}/Resources"
	"${CMAKE_BINARY_DIR}/bin/$<CONFIG>/Resources"
	)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "${PROJECT_NAME}")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")