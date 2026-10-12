


add_library(Common INTERFACE)
target_include_directories(Common INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
	$<INSTALL_INTERFACE:includes>
)

add_library(Container INTERFACE)
target_include_directories(Container INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
	$<INSTALL_INTERFACE:includes>
)
target_link_libraries(Container INTERFACE Common)

add_library(Wrappers INTERFACE)
target_include_directories(Wrappers INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
	$<INSTALL_INTERFACE:includes>
)
target_link_libraries(Wrappers INTERFACE Common)

file(GLOB_RECURSE MathSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sources/math/*.cpp"
)
add_library(Math OBJECT ${MathSources})
target_include_directories(Math PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
	$<INSTALL_INTERFACE:includes>
)
target_link_libraries(Math PUBLIC Common)

file(GLOB_RECURSE MemorySources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sources/memory/*.cpp"
)
add_library(Memory OBJECT ${MemorySources})
target_include_directories(Memory PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
	$<INSTALL_INTERFACE:includes>
)
target_link_libraries(Memory PUBLIC Common)

add_library(Core INTERFACE)
target_link_libraries(Core 
INTERFACE 
	Math 
	Wrappers 
	Container 
	Common
)
target_compile_features(Core INTERFACE cxx_std_23)

add_library(Core::Core ALIAS Core)
add_library(Core::Math ALIAS Math)
add_library(Core::Wrappers ALIAS Wrappers)
add_library(Core::Container ALIAS Container)
add_library(Core::Common ALIAS Common)
add_library(Core::Memory ALIAS Memory)