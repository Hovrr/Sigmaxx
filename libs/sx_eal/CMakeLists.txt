# EAL = Engine Abstraction Layer contract.
# Header-only for now; the FlatBuffers schema files land here in the next step.
find_package(Flatbuffers CONFIG REQUIRED)

add_library(sx_eal INTERFACE)
target_include_directories(sx_eal INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(TARGET Flatbuffers::Flatbuffers)
  target_link_libraries(sx_eal INTERFACE Flatbuffers::Flatbuffers)
else()
  target_link_libraries(sx_eal INTERFACE flatbuffers::flatbuffers)
endif()
