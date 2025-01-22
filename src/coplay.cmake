#-----------------------------------------------------------------------------
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#-----------------------------------------------------------------------------

#=============================================================================
# coplay.cmake
#
# Coplay project script for use in CoaXioN codebase.
#=============================================================================

include_guard( GLOBAL )

set( COPLAY_LIBDIR "${SRCDIR}/coplay/lib/${PLATFORM_SUBDIR}" )
set( COPLAY_SRCDIR "${SRCDIR}/coplay/src" )

set( COPLAY_SOURCE_FILES )
BEGIN_SRC( COPLAY_SOURCE_FILES "Source Files" )
	SRC_GRP(
		SUBGROUP "Coplay"
		SOURCES
		#{
			"${COPLAY_SRCDIR}/coplay_connection.cpp"
			"${COPLAY_SRCDIR}/coplay_system.cpp"
			"${COPLAY_SRCDIR}/coplay_client.cpp"
			"${COPLAY_SRCDIR}/coplay_host.cpp"

			"${COPLAY_SRCDIR}/coplay.h"
			"${COPLAY_SRCDIR}/coplay_connection.h"
			"${COPLAY_SRCDIR}/coplay_system.h"
			"${COPLAY_SRCDIR}/coplay_client.h"
			"${COPLAY_SRCDIR}/coplay_host.h"
		#}
	)
END_SRC( COPLAY_SOURCE_FILES "Source Files" )

function( target_use_coplay )
	cmake_parse_arguments(
		COPLAY
		"USE_LOBBIES;DONT_UPDATE_RPC;DONT_LINK_SDL2;DONT_LINK_SDL2_NET"
		"TARGET"
		""
		${ARGN}
	)

	target_sources(
		${COPLAY_TARGET} PRIVATE
		${COPLAY_SOURCE_FILES}
	)

	target_link_directories(
		${COPLAY_TARGET} PRIVATE
		"${COPLAY_LIBDIR}"
	)

	target_include_directories(
		${COPLAY_TARGET} PRIVATE
		"${COPLAY_SRCDIR}"
		"${SRCDIR}/coplay/include"
	)

	target_compile_definitions(
		${COPLAY_TARGET} PRIVATE
		"$<$<BOOL:${COPLAY_USE_LOBBIES}>:COPLAY_USE_LOBBIES>"
		"$<$<BOOL:${COPLAY_DONT_UPDATE_RPC}>:COPLAY_DONT_UPDATE_RPC>"
		"$<$<BOOL:${COPLAY_DONT_LINK_SDL2}>:COPLAY_DONT_LINK_SDL2>"
		"$<$<BOOL:${COPLAY_DONT_LINK_SDL2_NET}>:COPLAY_DONT_LINK_SDL2_NET>"
	)

	target_link_libraries(
		${COPLAY_TARGET} PRIVATE
		"$<$<AND:${IS_LINUX},$<NOT:$<BOOL:${COPLAY_DONT_LINK_SDL2}>>>:${COPLAY_LIBDIR}/libSDL2${IMPLIB_EXT}>"
		"$<$<AND:${IS_LINUX},$<NOT:$<BOOL:${COPLAY_DONT_LINK_SDL2_NET}>>>:${COPLAY_LIBDIR}/libSDL2_net${IMPLIB_EXT}>"
		"$<$<AND:${IS_WINDOWS},$<NOT:$<BOOL:${COPLAY_DONT_LINK_SDL2}>>>:${COPLAY_LIBDIR}/SDL2${IMPLIB_EXT}>"
		"$<$<AND:${IS_WINDOWS},$<NOT:$<BOOL:${COPLAY_DONT_LINK_SDL2_NET}>>>:${COPLAY_LIBDIR}/SDL2_net${IMPLIB_EXT}>"
	)
endfunction()
