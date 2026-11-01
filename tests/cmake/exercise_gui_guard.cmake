include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/gui_checks.cmake)

# Minimal driver for the reject tests: it calls exactly one guard so the
# wrapper can assert both the abort and the message in a nested CMake process.
if(GUARD STREQUAL "options")
    tb_validate_gui_options(${REQUIRE_GUI} ${ENABLE_GUI})
elseif(GUARD STREQUAL "qt")
    tb_require_qt_or_fail(${REQUIRE_GUI})
else()
    message(FATAL_ERROR "Unknown GUARD: ${GUARD}")
endif()
