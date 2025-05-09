# [SB] Removed for CMake 4.0 compatibility.
#cmake_minimum_required( VERSION 2.4 )

set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG" )

# Disable warnings for << operator precedence (4554) and
# unreferenced labels (4102) from VC
if( MSVC )
	set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4554 /wd4102" )
endif( MSVC )

if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
	set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra" )
endif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )

include_directories( ${CMAKE_CURRENT_BINARY_DIR} )
add_definitions( -DINFNAN_CHECK -DMULTIPLE_THREADS )

if( NOT MSVC AND NOT APPLE )
	if( NOT CMAKE_CROSSCOMPILING )
		add_executable( arithchk arithchk.c )
	endif( NOT CMAKE_CROSSCOMPILING )
	add_custom_command( OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/arith.h 
		COMMAND arithchk >${CMAKE_CURRENT_BINARY_DIR}/arith.h
		DEPENDS arithchk )

	if( NOT CMAKE_CROSSCOMPILING )
		add_executable( qnan qnan.c arith.h )
		set( CROSS_EXPORTS ${CROSS_EXPORTS} arithchk qnan PARENT_SCOPE )
	endif( NOT CMAKE_CROSSCOMPILING )
	add_custom_command( OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gd_qnan.h
		COMMAND qnan >${CMAKE_CURRENT_BINARY_DIR}/gd_qnan.h
		DEPENDS qnan )
		
	set( GEN_FP_FILES arith.h gd_qnan.h )
	set( GEN_FP_DEPS ${CMAKE_CURRENT_BINARY_DIR}/arith.h ${CMAKE_CURRENTY_BINARY_DIR}/gd_qnan.h )
endif( NOT MSVC AND NOT APPLE )

add_library( gdtoa
	${GEN_FP_FILES}
	dmisc.c
	dtoa.c
	misc.c
	)
target_link_libraries( gdtoa )

