cmake_minimum_required(VERSION 3.21)

project(ricochet VERSION 1.0.0 DESCRIPTION "Relay server for non-public p2p applications" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

if(NOT DEFINED CMAKE_CXX_VISIBILITY_PRESET)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
endif()
if(NOT DEFINED CMAKE_VISIBILITY_INLINES_HIDDEN)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

find_package(Boost 1.74 CONFIG REQUIRED COMPONENTS system unit_test_framework program_options coroutine log)
find_package(OpenSSL REQUIRED)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${Boost_INCLUDE_DIRS})

add_executable(ricochet
    src/proto.cpp
    src/session.cpp
    src/relay.cpp
    src/repo.cpp
    src/server.cpp
    src/logging.cpp
    src/main.cpp
)

target_link_libraries(ricochet Boost::coroutine Boost::program_options Boost::log OpenSSL::Crypto OpenSSL::SSL)
target_include_directories(ricochet PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ricochet PROPERTIES DEBUG_POSTFIX "d")

include(GenerateExportHeader)

add_library(libricochet src/proto.cpp src/client.cpp src/agent.cpp)
add_library(ricochet::libricochet ALIAS libricochet)

generate_export_header(libricochet EXPORT_FILE_NAME "export.h")

target_link_libraries(libricochet Boost::coroutine OpenSSL::Crypto OpenSSL::SSL)
target_include_directories(libricochet PRIVATE ${CMAKE_SOURCE_DIR}/src)

set_target_properties(libricochet PROPERTIES DEBUG_POSTFIX "d" OUTPUT_NAME ricochet IMPORT_PREFIX "lib")
set_target_properties(libricochet PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR} VERSION ${PROJECT_VERSION})

if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(libricochet PUBLIC LIBRICOCHET_STATIC_DEFINE)
endif()

if(MSVC)
    set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
    set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
    set(CMAKE_STATIC_LIBRARY_SUFFIX "-static.lib")
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

########## install ##########

if (NOT CMAKE_SKIP_INSTALL_RULES)

    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(RICICHET_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/ricochet")

    configure_package_config_file(cmake/ricochet-config.cmake.in ricochet-config.cmake INSTALL_DESTINATION "${RICICHET_INSTALL_CMAKEDIR}")
    write_basic_package_version_file(ricochet-config-version.cmake COMPATIBILITY SameMajorVersion)

    install(TARGETS ricochet RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT binary_files)

    install(TARGETS libricochet EXPORT ricochet_export
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT runtime_files
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT library_files
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT archive_files
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    set(TARGET_FILE ricochet-shared-targets.cmake)

    if(NOT BUILD_SHARED_LIBS)
        set(TARGET_FILE ricochet-static-targets.cmake)
    endif()

    install(EXPORT ricochet_export COMPONENT cmake_files
        FILE ${TARGET_FILE}
        DESTINATION "${RICICHET_INSTALL_CMAKEDIR}"
        NAMESPACE ricochet::)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ricochet-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ricochet-config-version.cmake"
        COMPONENT cmake_files
        DESTINATION "${RICICHET_INSTALL_CMAKEDIR}")

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/export.h" "src/agent.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ricochet" COMPONENT header_files)

    if(MSVC)
        install(FILES "$<TARGET_PDB_FILE:ricochet>" COMPONENT pdb_files
            CONFIGURATIONS Debug RelWithDebInfo
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            OPTIONAL)
        if(BUILD_SHARED_LIBS)
            install(FILES "$<TARGET_PDB_FILE:libricochet>" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                OPTIONAL)
        else()
            install(FILES "$<TARGET_FILE_DIR:libricochet>/$<TARGET_FILE_PREFIX:libricochet>$<TARGET_FILE_BASE_NAME:libricochet>-static.pdb" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                OPTIONAL)
        endif()
    endif()
endif()

########## tests ##########

enable_testing()

add_executable(ricochet_tests
    src/proto.cpp
    src/repo.cpp
    src/relay.cpp
    src/session.cpp
    src/server.cpp
    src/client.cpp
    src/agent.cpp
    src/logging.cpp
    tests/repo.cpp
    tests/relay.cpp
    tests/main.cpp
    tests/protocol/enums.cpp
    tests/protocol/buffer.cpp
    tests/protocol/endpoint.cpp
    tests/protocol/peer.cpp
    tests/protocol/couple.cpp
    tests/protocol/query.cpp
    tests/protocol/reply.cpp
    tests/integration/fixture.cpp
    tests/integration/client.cpp
    tests/integration/agent.cpp
)

target_link_libraries(ricochet_tests Boost::coroutine Boost::log Boost::unit_test_framework OpenSSL::Crypto OpenSSL::SSL)

target_include_directories(ricochet_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ricochet_tests PROPERTIES DEBUG_POSTFIX "d")

add_test(NAME ricochet_tests COMMAND ricochet_tests)

########## packages ##########

if(NOT CMAKE_SKIP_INSTALL_RULES)

    if(NOT CPACK_PACKAGE_RELEASE)
        set(CPACK_PACKAGE_RELEASE "1")
    endif()

    set(DEVEL_DOC_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
        )

    if (CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
        set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)
        cmake_host_system_information(RESULT DISTR QUERY DISTRIB_ID)
        if (DISTR MATCHES "(debian)|(ubuntu)|(kali)|(astra)")
            cmake_host_system_information(RESULT DISTR_CODENAME QUERY DISTRIB_VERSION_CODENAME)
            set(CPACK_DEBIAN_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}+${DISTR_CODENAME}")
            set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
            set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
            set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
            set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)
            set(CPACK_DEB_COMPONENT_INSTALL ON)

            set(DEBIAN_DOC_FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/linux/copyright"
                "${CMAKE_CURRENT_SOURCE_DIR}/linux/README.Debian"
                )
            install(FILES ${DEBIAN_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/ricochet" COMPONENT bin_deb_files)
            if(BUILD_SHARED_LIBS)
                install(FILES ${DEBIAN_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/libricochet" COMPONENT lib_deb_files)
            else()
                install(FILES ${DEBIAN_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/libricochet-dev" COMPONENT dev_deb_files)
                install(FILES ${DEVEL_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/libricochet-dev" COMPONENT dev_doc_files)
            endif()
        else()
            cmake_host_system_information(RESULT DISTR_VERSION QUERY DISTRIB_VERSION_ID)
            if (DISTR MATCHES "fedora")
                set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}.fc${DISTR_VERSION}")
            elseif(DISTRO_ID MATCHES "rhel|centos|almalinux|rocky")
                string(REGEX REPLACE "\\." "" DISTR_VERSION "${DISTR_VERSION}")
                set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}.el${DISTR_VERSION}")
            else()
                set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}")
            endif()

            if(NOT BUILD_SHARED_LIBS)
                install(FILES ${DEVEL_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/libricochet-devel" COMPONENT dev_doc_files)
            endif()

            set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
            set(CPACK_RPM_PACKAGE_AUTOREQ "1")
            set(CPACK_RPM_PACKAGE_LICENSE "Apache 2.0")
            set(CPACK_RPM_COMPONENT_INSTALL ON)
            set(CPACK_RPM_BUILD_SOURCE_DIRS_PREFIX "/src/ricochet")
        endif()
    elseif(NOT BUILD_SHARED_LIBS)
        install(FILES ${DEVEL_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/libricochet" COMPONENT dev_doc_files)
    endif()

    string(TOLOWER "${PROJECT_NAME}-${PROJECT_VERSION}-${CPACK_PACKAGE_RELEASE}-${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}" CPACK_ARCHIVE_FILE_NAME)
    include(CPack)
endif()