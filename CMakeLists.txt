cmake_minimum_required(VERSION 3.16)
project(niriqml VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Qml Quick Network)
qt_policy(SET QTP0001 NEW)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

include(cmake/NiriqmlInstallPaths.cmake)
get_filename_component(_niriqml_qml_install "${NIRIQML_INSTALL_QMLDIR}/Niri"
    ABSOLUTE BASE_DIR "${CMAKE_INSTALL_PREFIX}")
get_filename_component(_niriqml_lib_install "${CMAKE_INSTALL_LIBDIR}"
    ABSOLUTE BASE_DIR "${CMAKE_INSTALL_PREFIX}")
file(RELATIVE_PATH _niriqml_plugin_to_lib "${_niriqml_qml_install}" "${_niriqml_lib_install}")
message(STATUS "Niri QML installation: ${_niriqml_qml_install}")

# ==================== QML MODULE ====================

set(NIRIQML_SOURCES
    niriconnection.cpp
    niriconnection.h
    nirisession.cpp
    nirisession.h
    niripeerinfo.cpp
    niripeerinfo.h
    nirilogging.cpp
    nirilogging.h
    nirievents.cpp
    nirievents.h
    niriactions.cpp
    niriactions.h
    nirirequests.cpp
    nirirequests.h
    nirirequestoperation.cpp
    nirirequestoperation.h
    nirirequestsequence.cpp
    nirirequestsequence.h
    niriwindowsmodel.cpp
    niriwindowsmodel.h
    niriworkspacesmodel.cpp
    niriworkspacesmodel.h
    niriwindowreactive.cpp
    niriwindowreactive.h
    niriworkspacereactive.cpp
    niriworkspacereactive.h
    niristate.cpp
    niristate.h
    niritypes.cpp
    niritypes.h
    niripendingreply.cpp
    niripendingreply.h
    nirierror.cpp
    nirierror.h
    nirieventlog.cpp
    nirieventlog.h
    nirilinebuffer.cpp
    nirilinebuffer.h
    niriutils.cpp
    niriutils.h
    niriwire.cpp
    niriwire.h
)

include(generated/ipc-generated.cmake)
list(APPEND NIRIQML_SOURCES ${NIRIQML_GENERATED_SOURCES})

add_library(niriqml SHARED)
add_library(niriqml::niriqml ALIAS niriqml)
qt_add_qml_module(niriqml
    URI Niri
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Niri
    PLUGIN_TARGET niriqmlplugin
    SOURCES
        ${NIRIQML_SOURCES}
)

target_include_directories(niriqml PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/niriqml>
)
target_compile_features(niriqml PUBLIC cxx_std_17)
target_link_libraries(niriqml PUBLIC
    Qt6::Core
    Qt6::Qml
    Qt6::Network
    PRIVATE Qt6::Quick
)

set_target_properties(niriqml niriqmlplugin PROPERTIES
    BUILD_RPATH_USE_ORIGIN ON
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH_USE_LINK_PATH OFF
)
set_target_properties(niriqml PROPERTIES INSTALL_RPATH "$ORIGIN")
set_target_properties(niriqmlplugin PROPERTIES
    INSTALL_RPATH "$ORIGIN/${_niriqml_plugin_to_lib}"
)

# ==================== INSTALL ====================

install(TARGETS niriqml
    EXPORT niriqmlTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS niriqmlplugin
    LIBRARY DESTINATION ${NIRIQML_INSTALL_QMLDIR}/Niri
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/Niri/qmldir
    ${CMAKE_CURRENT_BINARY_DIR}/Niri/niriqml.qmltypes
    DESTINATION ${NIRIQML_INSTALL_QMLDIR}/Niri
)

install(FILES
    niriconnection.h
    nirisession.h
    niripeerinfo.h
    nirilogging.h
    nirievents.h
    niriactions.h
    nirirequests.h
    nirirequestoperation.h
    nirirequestsequence.h
    niriwindowsmodel.h
    niriworkspacesmodel.h
    niriwindowreactive.h
    niriworkspacereactive.h
    niristate.h
    niritypes.h
    niripendingreply.h
    nirierror.h
    nirieventlog.h
    nirilinebuffer.h
    niriutils.h
    niriwire.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/niriqml
)
install(FILES ${NIRIQML_GENERATED_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/niriqml/generated)

configure_package_config_file(cmake/niriqmlConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/niriqmlConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/niriqml
)
# Pre-1.0 releases make no cross-version ABI compatibility promise.
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/niriqmlConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion
)
install(EXPORT niriqmlTargets NAMESPACE niriqml::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/niriqml
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/niriqmlConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/niriqmlConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/niriqml
)

# ==================== TESTS ====================

option(BUILD_TEST_MODE "Build C++ test executables" ON)

if(BUILD_TEST_MODE)
    find_package(Qt6 REQUIRED COMPONENTS QuickTest Test)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    enable_testing()
    add_test(NAME test_generated_staleness
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate-ipc.py --check)
    add_test(NAME test_generator
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_generator.py)

    macro(add_niriqml_test NAME)
        add_executable(${NAME} tests/${NAME}.cpp)
        target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(${NAME} PRIVATE
            Qt6::Core Qt6::Quick Qt6::Test Qt6::Network niriqml
        )
        enable_testing()
        add_test(NAME ${NAME} COMMAND ${NAME})
        set_tests_properties(${NAME} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
        if(ENABLE_COVERAGE)
            target_compile_options(${NAME} PRIVATE --coverage -fprofile-abs-path)
            target_link_options(${NAME} PRIVATE --coverage -fprofile-abs-path)
        endif()
    endmacro()

    add_niriqml_test(test_framing)
    add_niriqml_test(test_linebuffer)
    add_niriqml_test(test_wire)
    add_niriqml_test(test_session)
    add_niriqml_test(test_session_review)
    add_niriqml_test(test_requests)
    add_niriqml_test(test_codec)
    add_niriqml_test(test_generated_actions)
    add_niriqml_test(test_types)
    add_niriqml_test(test_converter)
    add_niriqml_test(test_events)
    add_niriqml_test(test_live_wire)
    add_niriqml_test(test_live_events)
    add_niriqml_test(test_live_layer2)
    add_niriqml_test(test_live_write)

    add_executable(test_api
        tests/test_api.cpp
    )
    target_include_directories(test_api PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_api PRIVATE
        Qt6::Core Qt6::Quick Qt6::QuickTest Qt6::Network niriqml
    )
    enable_testing()
    add_test(NAME test_api
        COMMAND test_api
            -input ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api.qml
    )
    set_tests_properties(test_api PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}"
    )
    if(ENABLE_COVERAGE)
        target_compile_options(test_api PRIVATE --coverage -fprofile-abs-path)
        target_link_options(test_api PRIVATE --coverage -fprofile-abs-path)
    endif()

    add_executable(test_connection
        tests/test_connection.cpp
        tests/nirimock.h
        tests/nirimock.cpp
    )
    target_include_directories(test_connection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_connection PRIVATE
        Qt6::Core Qt6::Quick Qt6::Test Qt6::Network niriqml
    )
    enable_testing()
    add_test(NAME test_connection COMMAND test_connection)
    set_tests_properties(test_connection PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
    add_test(NAME test_scripts COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scripts.sh
        ${CMAKE_CURRENT_BINARY_DIR}/test_scripts_scratch)
    if(ENABLE_COVERAGE)
        target_compile_options(test_connection PRIVATE --coverage -fprofile-abs-path)
        target_link_options(test_connection PRIVATE --coverage -fprofile-abs-path)
    endif()
endif()

# ==================== COVERAGE ====================

option(ENABLE_COVERAGE "Instrument targets for code coverage collection" OFF)

if(ENABLE_COVERAGE)
    message(STATUS "Enabling code coverage flags")

    target_compile_options(niriqml PRIVATE --coverage -fprofile-abs-path)
    target_link_options(niriqml PRIVATE --coverage -fprofile-abs-path)

    # coverage flags are set per-test via add_niriqml_test macro above
endif()
