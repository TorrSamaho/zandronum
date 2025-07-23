# [AK] Find Opus
# Find the native Opus includes and library.
#
# OPUS_INCLUDE_DIR - Where to find opus.h.
# OPUS_LIBRARIES   - List of libraries when using Opus.
# OPUS_FOUND       - True if Opus found.

IF ( OPUS_INCLUDE_DIR AND OPUS_LIBRARIES )
	# Already in cache, be silent.
	SET( OPUS_FIND_QUIETLY TRUE )
ENDIF ( OPUS_INCLUDE_DIR AND OPUS_LIBRARIES )

FIND_PATH( OPUS_INCLUDE_DIR opus.h PATH_SUFFIXES opus )

FIND_LIBRARY( OPUS_LIBRARIES NAMES opus libopus )
MARK_AS_ADVANCED( CLEAR OPUS_LIBRARIES OPUS_INCLUDE_DIR )

# Handle the QUIETLY and REQUIRED arguments and set OPUS_FOUND to TRUE if 
# all listed variables are TRUE.
INCLUDE( FindPackageHandleStandardArgs )
FIND_PACKAGE_HANDLE_STANDARD_ARGS( Opus DEFAULT_MSG OPUS_LIBRARIES OPUS_INCLUDE_DIR )
