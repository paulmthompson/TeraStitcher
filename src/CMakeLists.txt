# needed for Qt 5
cmake_minimum_required(VERSION 3.21 FATAL_ERROR)
project( TeraStitcher )

#[[
Best practice for looking for cmake modules cmake folder in root of the project
From Professional CMake Chapter 11 Modules
]]
list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# apple backwards compatibility
option(MACOSX_BACKWARDS_COMPATIBILITY "MacOSX 10.11 backwords compatibility mode" OFF)
if(APPLE AND MACOSX_BACKWARDS_COMPATIBILITY)
	set(CMAKE_OSX_DEPLOYMENT_TARGET "10.6" CACHE STRING "Minimum OS X deployment version" FORCE)
	set(CMAKE_OSX_SYSROOT "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk")
	message(STATUS ${CMAKE_OSX_DEPLOYMENT_TARGET})
	message(STATUS ${MACOSX_DEPLOYMENT_TARGET})
endif()

#[[
Manifest mode from VCPKG will make all dependancies specified in vcpkg.json be associated and installed
with this particular project
]]
set(VCPKG_MANIFEST_MODE ON)

find_package(TIFF REQUIRED)

include(Utils)
include(Setup)
include(Version)
include(InstallLocation)

# check GUI option
option (WITH_GUI "Build Qt-based GUI" OFF) 
if(WITH_GUI)
	message(STATUS "Include gui ...")
	add_subdirectory( gui )	
	message(STATUS "Include gui ... OK!")
endif(WITH_GUI)

# build modules
add_subdirectory( 3rdparty/zlib )
add_subdirectory( 3rdparty/libtiff )
add_subdirectory( 3rdparty/tinyxml )
add_subdirectory( imagemanager )
add_subdirectory( iomanager )
add_subdirectory( crossmips )
add_subdirectory( volumemanager )
add_subdirectory( stitcher )
add_subdirectory( common )
add_subdirectory( mappingmanager )
add_subdirectory( multilayersmanager )
add_subdirectory( ui )

# build user-selectable modules
# --- find modules ---
SUBDIRLIST(SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR}/utils)
# --- ask for selection of the modules to be included in the project (default is OFF) ---
FOREACH(subdir ${SUBDIRS})
	option (WITH_UTILITY_MODULE_${subdir} "Include this utility module (optional) in the project" OFF) 
ENDFOREACH()
# --- include selected modules only ---
FOREACH(subdir ${SUBDIRS})
	if(WITH_UTILITY_MODULE_${subdir})
		message(STATUS "Include utils/" "${subdir} ...")
		add_subdirectory(utils/${subdir})
		message(STATUS "Include utils/" "${subdir} ... OK!")
	endif(WITH_UTILITY_MODULE_${subdir})
ENDFOREACH()

if(NOT WITH_UTILITY_MODULE_volumeconverter AND (WITH_NEW_MERGE OR WITH_GUI))
	message(STATUS "Include utils/volumeconverter ...")
	add_subdirectory(utils/volumeconverter)
	message(STATUS "Include utils/volumeconverter ...")
	set(WITH_UTILITY_MODULE_volumeconverter ON CACHE BOOL "Otherwise linker fails" FORCE)
endif()

# set CPack
include(TeraStitcherCPack)