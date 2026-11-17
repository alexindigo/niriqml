# Use the Qt selected by find_package, never whichever qmake is on PATH.
if(NOT DEFINED NIRIQML_INSTALL_QMLDIR)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Set NIRIQML_INSTALL_QMLDIR to the target Qt QML import root")
    endif()
    set(_niriqml_qt_prefix "${QT6_INSTALL_PREFIX}")
    set(_niriqml_qt_qml "${QT6_INSTALL_QML}")
    if(NOT _niriqml_qt_prefix OR NOT _niriqml_qt_qml)
        # Qt 6.5 does not export QtInstallPaths.cmake. Its CoreTools package
        # does export qtpaths, whose queries report that selected Qt's layout.
        if(TARGET Qt6::qtpaths)
            get_target_property(_niriqml_qtpaths Qt6::qtpaths IMPORTED_LOCATION)
            if(NOT _niriqml_qtpaths)
                get_target_property(_niriqml_configs Qt6::qtpaths IMPORTED_CONFIGURATIONS)
                foreach(_config IN LISTS _niriqml_configs)
                    get_target_property(_niriqml_qtpaths Qt6::qtpaths IMPORTED_LOCATION_${_config})
                    if(_niriqml_qtpaths)
                        break()
                    endif()
                endforeach()
            endif()
        endif()
        if(NOT _niriqml_qtpaths)
            message(FATAL_ERROR "Selected Qt has no layout metadata or qtpaths; set NIRIQML_INSTALL_QMLDIR")
        endif()
        execute_process(COMMAND "${_niriqml_qtpaths}" --query QT_INSTALL_PREFIX
            OUTPUT_VARIABLE _niriqml_qt_prefix OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _prefix_result TIMEOUT 10)
        execute_process(COMMAND "${_niriqml_qtpaths}" --query QT_INSTALL_QML
            OUTPUT_VARIABLE _niriqml_qt_qml OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _qml_result TIMEOUT 10)
        if(NOT _prefix_result EQUAL 0 OR NOT _qml_result EQUAL 0
            OR NOT _niriqml_qt_prefix OR NOT _niriqml_qt_qml)
            message(FATAL_ERROR "Selected Qt layout query failed; set NIRIQML_INSTALL_QMLDIR")
        endif()
    endif()
    if(IS_ABSOLUTE "${_niriqml_qt_qml}")
        file(RELATIVE_PATH _niriqml_qml_default "${_niriqml_qt_prefix}" "${_niriqml_qt_qml}")
        if(_niriqml_qml_default MATCHES "^\\.\\.(/|$)")
            message(FATAL_ERROR "Selected Qt QML root is outside its prefix. Set NIRIQML_INSTALL_QMLDIR explicitly")
        endif()
    else()
        set(_niriqml_qml_default "${_niriqml_qt_qml}")
    endif()
endif()
# STRING preserves relative command-line overrides instead of CMake turning
# an untyped relative cache PATH into a caller-working-directory absolute path.
set(NIRIQML_INSTALL_QMLDIR "${_niriqml_qml_default}" CACHE STRING
    "QML import root (relative to install prefix or absolute)")
