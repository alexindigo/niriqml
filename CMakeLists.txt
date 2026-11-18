cmake_minimum_required(VERSION 3.16)
project(niriqml VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_COVERAGE "Instrument targets for code coverage collection" OFF)

function(niriqml_enable_coverage target)
    if(ENABLE_COVERAGE)
        # Tests also exercise shared code concurrently, so counters must be atomic.
        set(_coverage_flags --coverage -fprofile-abs-path -fprofile-update=atomic)
        target_compile_options(${target} PRIVATE ${_coverage_flags})
        target_link_options(${target} PRIVATE ${_coverage_flags})
    endif()
endfunction()

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
    nirieventstreamstate.cpp
    nirieventstreamstate.h
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
    niristateprojection.h
    niriselectors.h
    niriselectors.cpp
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
niriqml_enable_coverage(niriqml)
niriqml_enable_coverage(niriqmlplugin)

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
    nirieventstreamstate.h
    niristateprojection.h
    niriselectors.h
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
    add_library(nirimock STATIC tests/nirimock.cpp tests/nirimock.h)
    niriqml_enable_coverage(nirimock)
    target_link_libraries(nirimock PUBLIC niriqml Qt6::Network)
    target_compile_definitions(nirimock PRIVATE
        NIRIQML_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
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
        if("${NAME}" MATCHES "^test_live_")
            add_test(NAME ${NAME} COMMAND bash
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/live/run-live.sh --suite $<TARGET_FILE:${NAME}>)
            set_tests_properties(${NAME} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 200)
        else()
            add_test(NAME ${NAME} COMMAND ${NAME})
        endif()
        set_tests_properties(${NAME} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
        niriqml_enable_coverage(${NAME})
    endmacro()

    add_niriqml_test(test_framing)
    add_niriqml_test(test_linebuffer)
    add_niriqml_test(test_wire)
    add_niriqml_test(test_session)
    add_niriqml_test(test_session_review)
    add_niriqml_test(test_requests)
    add_niriqml_test(test_codec)
    add_niriqml_test(test_generated_actions)
    add_niriqml_test(test_generated_requests)
    add_niriqml_test(test_generated_events)
    add_niriqml_test(test_state)
    add_niriqml_test(test_state_projections)
    add_niriqml_test(test_projection_reset)
    add_niriqml_test(test_eventlog)
    add_niriqml_test(test_ipc_inventory)
    add_niriqml_test(test_types)
    add_niriqml_test(test_converter)
    add_niriqml_test(test_events)
    add_niriqml_test(test_mock)
    target_link_libraries(test_mock PRIVATE nirimock)
    add_niriqml_test(test_live_wire)
    add_niriqml_test(test_live_events)
    add_niriqml_test(test_live_layer2)
    add_niriqml_test(test_live_write)
    add_niriqml_test(test_live_acceptance)
    target_compile_definitions(test_live_acceptance PRIVATE
        NIRIQML_LIVE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/tests/live")

    add_executable(test_api
        tests/test_api.cpp
    )
    niriqml_enable_coverage(test_api)
    target_include_directories(test_api PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_api PRIVATE
        Qt6::Core Qt6::Quick Qt6::QuickTest Qt6::Network niriqml nirimock
    )
    enable_testing()
    add_test(NAME test_api
        COMMAND test_api
            -input ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api.qml
    )
    set_tests_properties(test_api PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_executable(test_examples tests/test_examples.cpp)
    niriqml_enable_coverage(test_examples)
    target_link_libraries(test_examples PRIVATE
        Qt6::Core Qt6::Quick Qt6::QuickTest Qt6::Network niriqml nirimock)
    target_compile_definitions(test_examples PRIVATE
        NIRIQML_EXAMPLE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME test_examples COMMAND test_examples
        -input ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_examples.qml)
    set_tests_properties(test_examples PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}"
        TIMEOUT 120)
    add_test(NAME test_examples_qmllint COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_qml_examples.py
        --qmllint $<TARGET_FILE:Qt6::qmllint>
        --imports ${CMAKE_CURRENT_BINARY_DIR}
        --output ${CMAKE_CURRENT_BINARY_DIR}/documentation-snippets)
    add_executable(test_connection
        tests/test_connection.cpp
    )
    niriqml_enable_coverage(test_connection)
    target_include_directories(test_connection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_connection PRIVATE
        Qt6::Core Qt6::Quick Qt6::Test Qt6::Network niriqml nirimock
    )
    enable_testing()
    add_test(NAME test_connection COMMAND test_connection)
    set_tests_properties(test_connection PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
    add_test(NAME test_scripts COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scripts.sh
        ${CMAKE_CURRENT_BINARY_DIR}/test_scripts_scratch)
    add_test(NAME test_fixture_runner COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fixture_runner.sh
        ${CMAKE_CURRENT_BINARY_DIR}/test_fixture_runner_scratch)
    get_property(_tests DIRECTORY PROPERTY TESTS)
    foreach(_test IN LISTS _tests)
        if(_test MATCHES "^test_live_")
            set_tests_properties(${_test} PROPERTIES LABELS live RUN_SERIAL TRUE)
        elseif(_test STREQUAL "test_api" OR _test MATCHES "^test_examples")
            set_tests_properties(${_test} PROPERTIES LABELS qml)
        else()
            set_tests_properties(${_test} PROPERTIES LABELS unit)
        endif()
    endforeach()
    set_property(TEST test_session test_session_review test_requests test_codec
        test_generated_actions test_generated_requests test_generated_events test_state
        test_state_projections test_projection_reset APPEND PROPERTY LABELS qml)
endif()
