prefix=${CMAKE_INSTALL_PREFIX}
exec_prefix=${CMAKE_INSTALL_PREFIX}
libdir=${CMAKE_INSTALL_FULL_LIBDIR}
includedir=${CMAKE_INSTALL_FULL_INCLUDEDIR}


Name: Suno Tag Parser
Description: Audio meta-data library (SunoAudiobookPlayer bindings)
Requires: taglib
Version: ${TAGLIB_LIB_VERSION_STRING}
Libs: -L${CMAKE_INSTALL_FULL_LIBDIR} -lsunotagparser
Cflags: -I${CMAKE_INSTALL_FULL_INCLUDEDIR}/taglib
