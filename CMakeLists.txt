cmake_minimum_required(VERSION 3.16)
project(ricochet VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.74 REQUIRED COMPONENTS system)
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
    src/server.cpp
)

target_link_libraries(ricochet ${Boost_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL)

if(MSVC)
    target_compile_options(ricochet PRIVATE /W4)
else()
    target_compile_options(ricochet PRIVATE -Wall -Wextra -Wpedantic)
endif()
