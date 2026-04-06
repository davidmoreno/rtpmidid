# Download htmx and Alpine.js at configure time (major-version URLs on unpkg).
set(RTPMIDID_WEB_DEPS_DIR "${CMAKE_BINARY_DIR}/web-deps")
file(MAKE_DIRECTORY "${RTPMIDID_WEB_DEPS_DIR}")

set(RTPMIDID_HTMX_URL "https://unpkg.com/htmx.org@2/dist/htmx.min.js")
set(RTPMIDID_ALPINE_URL "https://unpkg.com/alpinejs@3/dist/cdn.min.js")

set(RTPMIDID_HTMX_JS "${RTPMIDID_WEB_DEPS_DIR}/htmx.min.js")
set(RTPMIDID_ALPINE_JS "${RTPMIDID_WEB_DEPS_DIR}/alpine.min.js")

if(NOT EXISTS "${RTPMIDID_HTMX_JS}")
  message(STATUS "Downloading htmx.min.js …")
  file(DOWNLOAD "${RTPMIDID_HTMX_URL}" "${RTPMIDID_HTMX_JS}" SHOW_PROGRESS TLS_VERIFY ON)
endif()
if(NOT EXISTS "${RTPMIDID_ALPINE_JS}")
  message(STATUS "Downloading alpine.min.js …")
  file(DOWNLOAD "${RTPMIDID_ALPINE_URL}" "${RTPMIDID_ALPINE_JS}" SHOW_PROGRESS TLS_VERIFY ON)
endif()

set(RTPMIDID_WEB_STAGE "${CMAKE_BINARY_DIR}/web-rtpmidid")
add_custom_target(
  rtpmidid_web_ui ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${RTPMIDID_WEB_STAGE}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/web/static"
          "${RTPMIDID_WEB_STAGE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${RTPMIDID_WEB_STAGE}/vendor"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RTPMIDID_HTMX_JS}"
          "${RTPMIDID_WEB_STAGE}/vendor/htmx.min.js"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RTPMIDID_ALPINE_JS}"
          "${RTPMIDID_WEB_STAGE}/vendor/alpine.min.js"
  COMMENT "Assemble rtpmidid web UI into ${RTPMIDID_WEB_STAGE}"
  VERBATIM)

include(GNUInstallDirs)
set(RTPMIDID_WEB_INSTALL_DIR "${CMAKE_INSTALL_FULL_DATADIR}/rtpmidid/web")
