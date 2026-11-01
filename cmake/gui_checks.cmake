# Validation helpers for the optional Qt6 GUI.
#
# These live in a standalone module (instead of inline in the top-level
# CMakeLists.txt) so the fatal branches can be exercised by script-mode CTest
# cases without a full project configure. See tests/cmake/test_gui_*.cmake.

function(tb_validate_gui_options require_gui enable_gui)
    if(require_gui AND NOT enable_gui)
        message(FATAL_ERROR "TB_REQUIRE_GUI=ON requires TB_ENABLE_GUI=ON.")
    endif()
endfunction()

function(tb_require_qt_or_fail require_gui)
    if(require_gui)
        message(FATAL_ERROR
            "Qt6 was not found, but TB_REQUIRE_GUI=ON. Install Qt6 "
            "(qt6-base-dev, brew install qt, or the MSYS2 qt6-base package) "
            "or configure with -DTB_ENABLE_GUI=OFF -DTB_REQUIRE_GUI=OFF "
            "for a CLI-only build.")
    endif()
endfunction()
