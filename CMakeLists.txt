cmake_minimum_required(VERSION 3.16)
project(niriqml LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Quick Network)
qt_policy(SET QTP0001 NEW)

include(GNUInstallDirs)

# ==================== QML MODULE ====================

set(NIRIQML_SOURCES
    niriconnection.cpp
    niriconnection.h
    nirievents.cpp
    nirievents.h
    niriactions.cpp
    niriactions.h
    nirirequests.cpp
    nirirequests.h
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
)

qt_add_qml_module(niriqml
    URI Niri
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Niri
    # INSTALL_DIRECTORY is handled manually below
    SOURCES
        ${NIRIQML_SOURCES}
)

target_link_libraries(niriqml PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Network
)

# ==================== INSTALL ====================

install(TARGETS niriqml
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Niri/
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/Niri
)

install(FILES
    niriconnection.h
    nirievents.h
    niriactions.h
    nirirequests.h
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
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/niriqml
)

# ==================== TESTS ====================

option(BUILD_TEST_MODE "Build C++ test executables" ON)

if(BUILD_TEST_MODE)
    find_package(Qt6 REQUIRED COMPONENTS Test)

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
    add_niriqml_test(test_types)
    add_niriqml_test(test_live_wire)

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
