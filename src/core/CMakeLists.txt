
cmake_minimum_required(VERSION 3.20)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(PrismCore LANGUAGES CXX)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PRISMCORE_BUILD_SHARED_LIBS "Build PrismCore shared libraries" OFF)
if(PRISMCORE_BUILD_SHARED_LIBS)
	set(PrismCoreLibraryType SHARED)
else()
	set(PrismCoreLibraryType STATIC)
endif()

add_library(Common ${PrismCoreLibraryType}
	sources/common/Error.cpp
	sources/common/Assert.cpp
	sources/common/GId.cpp
)
set_target_properties(Common PROPERTIES
	OUTPUT_NAME PrismCoreCommon
	POSITION_INDEPENDENT_CODE ON
	WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_include_directories(Common PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(Common PUBLIC cxx_std_23)
if(WIN32)
	target_link_libraries(Common PRIVATE ole32)
endif()

add_library(Container INTERFACE)
target_link_libraries(Container INTERFACE Common)

add_library(Wrappers INTERFACE)
target_link_libraries(Wrappers INTERFACE Common)

add_library(Reflect INTERFACE)
target_link_libraries(Reflect INTERFACE Common)

add_library(Serde INTERFACE)
target_link_libraries(Serde INTERFACE Reflect)

file(GLOB_RECURSE MathSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sources/math/*.cpp"
)
add_library(Math ${PrismCoreLibraryType} ${MathSources})
set_target_properties(Math PROPERTIES
	OUTPUT_NAME PrismCoreMath
	POSITION_INDEPENDENT_CODE ON
	WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_link_libraries(Math PUBLIC Common)

file(GLOB_RECURSE MemorySources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sources/memory/*.cpp"
)
add_library(Memory ${PrismCoreLibraryType} ${MemorySources})
set_target_properties(Memory PROPERTIES
	OUTPUT_NAME PrismCoreMemory
	POSITION_INDEPENDENT_CODE ON
	WINDOWS_EXPORT_ALL_SYMBOLS ON
)
find_package(Threads REQUIRED)
target_link_libraries(Memory PUBLIC Common Container Threads::Threads)

function(prismcore_memory_mode Variable Macro)
	set(${Variable} "AUTO" CACHE STRING "Memory diagnostic mode: AUTO, ON or OFF")
	set_property(CACHE ${Variable} PROPERTY STRINGS AUTO ON OFF)
	if("${${Variable}}" STREQUAL "AUTO")
		if(CMAKE_CONFIGURATION_TYPES)
			set(Mode "$<IF:$<CONFIG:Debug>,1,0>")
		elseif(CMAKE_BUILD_TYPE MATCHES "^[Dd][Ee][Bb][Uu][Gg]$")
			set(Mode 1)
		else()
			set(Mode 0)
		endif()
	elseif("${${Variable}}" STREQUAL "ON")
		set(Mode 1)
	elseif("${${Variable}}" STREQUAL "OFF")
		set(Mode 0)
	else()
		message(FATAL_ERROR "${Variable} must be AUTO, ON or OFF")
	endif()
	target_compile_definitions(Memory PUBLIC "${Macro}=${Mode}")
endfunction()

prismcore_memory_mode(PRISMCORE_MEMORY_DEBUG_GUARDS CORE_MEM_ENABLE_DEBUG_GUARDS)
prismcore_memory_mode(PRISMCORE_MEMORY_UAF_DETECTION CORE_MEM_ENABLE_UAF_DETECTION)
prismcore_memory_mode(PRISMCORE_MEMORY_CAPTURE_STACK CORE_MEM_CAPTURE_STACK)
option(PRISMCORE_MEMORY_ENABLE_GLOBAL_HOOKS "Enable process-wide allocation hooks" OFF)
target_compile_definitions(Memory PUBLIC
	CORE_MEM_ENABLE_GLOBAL_HOOKS=$<BOOL:${PRISMCORE_MEMORY_ENABLE_GLOBAL_HOOKS}>
	CORE_MEM_ENABLE_LAZY_COMMIT=1
	CORE_MEM_ENABLE_INTERNAL_BENCH_TIMING=0
	CORE_MEM_QUARANTINE_RELEASE_ONLY_ON_FLUSH=0
)
if(PRISMCORE_MEMORY_ENABLE_GLOBAL_HOOKS)
	target_link_libraries(Memory PRIVATE ${CMAKE_DL_LIBS})
endif()

add_library(Core INTERFACE)
target_link_libraries(Core 
INTERFACE 
	Math 
	Memory
	Wrappers 
	Container 
	Reflect
	Serde
	Common
)

add_library(Core::Core ALIAS Core)
add_library(Core::Math ALIAS Math)
add_library(Core::Wrappers ALIAS Wrappers)
add_library(Core::Container ALIAS Container)
add_library(Core::Common ALIAS Common)
add_library(Core::Memory ALIAS Memory)
add_library(Core::Reflect ALIAS Reflect)
add_library(Core::Serde ALIAS Serde)

install(TARGETS Core Math Memory Wrappers Container Reflect Serde Common
	EXPORT PrismCoreTargets
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY includes/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
	FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

set(PrismCorePackageDir "${CMAKE_INSTALL_LIBDIR}/cmake/PrismCore")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PrismCoreConfig.cmake.in" [=[
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(Threads)
include("${CMAKE_CURRENT_LIST_DIR}/PrismCoreTargets.cmake")
check_required_components(PrismCore)
]=])
configure_package_config_file(
	"${CMAKE_CURRENT_BINARY_DIR}/PrismCoreConfig.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/PrismCoreConfig.cmake"
	INSTALL_DESTINATION "${PrismCorePackageDir}"
)
install(EXPORT PrismCoreTargets
	FILE PrismCoreTargets.cmake
	NAMESPACE Core::
	DESTINATION "${PrismCorePackageDir}"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PrismCoreConfig.cmake"
	DESTINATION "${PrismCorePackageDir}"
)