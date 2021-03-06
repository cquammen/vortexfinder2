set (common_headers
  diy-ext.hpp
  FieldLine.h
  MeshGraphRegular3D.h
  VortexObject.h
  Inclusions.h
  MeshGraphRegular3DTets.h
  VortexSequence.h 
  Interval.h
  Puncture.h
  VortexTransition.h
  MeshGraph.h 
  VortexEvents.h
  VortexTransitionMatrix.h
  MeshGraphRegular2D.h
  VortexLine.h
)

set (common_sources
  MeshGraph.cpp
  MeshGraphRegular2D.cpp
  MeshGraphRegular3D.cpp
  MeshGraphRegular3DTets.cpp
  VortexLine.cpp
  VortexTransitionMatrix.cpp
  VortexTransition.cpp
  Inclusions.cpp
  FieldLine.cpp
  Puncture.cpp
  zcolor.cpp
  random_color.cpp
  graph_color.cpp
)

set (common_protos
  MeshGraph.proto
  DataInfo.proto
  Inclusions.proto
  FieldLine.proto
  Puncture.proto
)

if (APPLE AND NOT PARAVIEW_DO_UNIX_STYLE_INSTALL)
  set(CMAKE_INSTALL_NAME_DIR "@loader_path/../Libraries")
endif ()

add_library (glcommon SHARED ${common_sources} ${common_proto_sources})
target_include_directories(glcommon PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

set (deps ${CMAKE_THREAD_LIBS_INIT})
if (WITH_VTK)
  list (APPEND deps ${VTK_LIBRARIES})
endif ()
if (WITH_ROCKSDB)
  list (APPEND deps ${RocksDB_LIBRARY})
endif ()

target_link_libraries (glcommon PUBLIC ${deps})

if (WITH_PARAVIEW)
  install (FILES ${common_headers}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/common)
  install (TARGETS glcommon
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif ()
