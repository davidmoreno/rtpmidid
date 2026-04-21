# dm-json: Python code generator + runtime static library
find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(dmjson_add_library target_name)
  set(DMJSON_OUT "${CMAKE_CURRENT_BINARY_DIR}/dmjson_gen")
  set(DMJSON_H "${DMJSON_OUT}/dm_json_generated.hpp")
  set(DMJSON_CPP "${DMJSON_OUT}/dm_json_generated.cpp")
  add_custom_command(
    OUTPUT "${DMJSON_H}" "${DMJSON_CPP}"
    COMMAND
      "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/dm_json_gen.py"
      --out-dir "${DMJSON_OUT}" --header dm_json_generated.hpp --source
      dm_json_generated.cpp "${CMAKE_SOURCE_DIR}/src/dm_json_status.hpp"
      "${CMAKE_SOURCE_DIR}/src/dm_json_rpc.hpp"
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/dm_json_gen.py"
            "${CMAKE_SOURCE_DIR}/src/dm_json_status.hpp"
            "${CMAKE_SOURCE_DIR}/src/dm_json_rpc.hpp"
    VERBATIM)
  add_library(${target_name} STATIC "${CMAKE_SOURCE_DIR}/lib/dm_json/runtime.cpp"
                                    "${DMJSON_CPP}")
  target_include_directories(
    ${target_name}
    PUBLIC "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src"
           "${DMJSON_OUT}")
endfunction()
