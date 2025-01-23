#-----------------------------------------------------------------------------
# SRCGRP.CMAKE
#
# SanyaSho (2024)
#-----------------------------------------------------------------------------

include_guard( GLOBAL )

macro( BEGIN_SRC TARGET_SRC GROUP_NAME )
	set( _CURRENT_SRC	${TARGET_SRC} )
	set( _CURRENT_GROUP	${GROUP_NAME} )
endmacro()

macro( SRC_GRP )
	cmake_parse_arguments(
		GRP_ARGS
		""
		"TARGET_SRC;SUBGROUP"
		"SOURCES;NO_PCH"
		${ARGN}
	)

	if( NOT GRP_ARGS_TARGET_SRC )
		set( GRP_ARGS_TARGET_SRC "${_CURRENT_SRC}" )
	endif()

	if( NOT GRP_ARGS_SUBGROUP )
		set( GRP_ARGS_SUBGROUP "" )
	endif()

	#message( STATUS "${_CURRENT_GROUP}//${GRP_ARGS_SUBGROUP}" )

	foreach( FILE IN LISTS GRP_ARGS_SOURCES )
		#message( STATUS "SOURCES: ${FILE}" )

		set( GROUP_NAME "${_CURRENT_GROUP}//${GRP_ARGS_SUBGROUP}" )
		list( APPEND ${GRP_ARGS_TARGET_SRC} ${FILE} )

		# Full path support (TODO)
		#message( STATUS "OldSrc: ${FILE}" )
		#if( ${FILE} MATCHES ".*:([A-Z]:.*)>$" )
		#	set( FILE ${CMAKE_MATCH_1} )
		#endif()
		#message( STATUS "Src: ${FILE}" )

		# Lazy stripper
		#message( "${FILE}" )
		string( REGEX REPLACE "\\$<\\$<(.*)>:" "" _FILE "${FILE}" )
		string( REGEX REPLACE ">" "" _FILE "${_FILE}" )
		#message( "${_FILE}" )

		source_group( ${GROUP_NAME} FILES "${_FILE}" )
	endforeach()

	foreach( FILE IN LISTS GRP_ARGS_NO_PCH )
		#message( STATUS "NO_PCH: ${FILE}" )
		set_source_files_properties( ${FILE} PROPERTIES SKIP_PRECOMPILE_HEADERS ON )
	endforeach()

	set( ${GRP_ARGS_TARGET_SRC} ${${GRP_ARGS_TARGET_SRC}} PARENT_SCOPE )
endmacro()

macro( END_SRC TARGET_SRC GROUP_NAME )
	unset( _CURRENT_SRC )
	unset( _CURRENT_GROUP )
endmacro()
