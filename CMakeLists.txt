cmake_minimum_required(VERSION 3.16)
project(ricochet VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.74 REQUIRED COMPONENTS system unit_test_framework program_options)
find_package(OpenSSL REQUIRED)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${Boost_INCLUDE_DIRS})

add_executable(ricochet
    src/ricochet.h
    src/ricochet.cpp
    src/session.h
    src/session.cpp
    src/relay.h
    src/relay.cpp
    src/repo.h
    src/repo.cpp
    src/server.h
    src/server.cpp
    src/main.cpp
)

target_link_libraries(ricochet ${Boost_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL)
target_include_directories(ricochet PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(ricochet PRIVATE /W4)
else()
    target_compile_options(ricochet PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Enable testing
enable_testing()

# Create test executable
add_executable(ricochet_tests
    src/ricochet.cpp
    src/repo.cpp
    tests/main.cpp
    tests/protocol/enums.cpp
    tests/protocol/buffer.cpp
    tests/protocol/endpoint.cpp
    tests/protocol/peer.cpp
    tests/protocol/couple.cpp
    tests/protocol/query.cpp
    tests/protocol/reply.cpp
    tests/repo.cpp
)

target_link_libraries(ricochet_tests 
    ${Boost_LIBRARIES} 
    OpenSSL::Crypto 
    OpenSSL::SSL
    Boost::unit_test_framework
)

target_include_directories(ricochet_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(ricochet_tests PRIVATE /W4)
else()
    target_compile_options(ricochet_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Add test
add_test(NAME ricochet_unit_tests COMMAND ricochet_tests)
