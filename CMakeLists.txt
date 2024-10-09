cmake_minimum_required(VERSION 3.5.0)
project(StructRelay_UDP VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# debug flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall")

# release flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3")


find_library(PTHREAD_LIBRARY pthread)

include_directories(include)

add_executable(server src/server.cpp)
target_link_libraries(server ${PTHREAD_LIBRARY})

add_executable(client src/client.cpp)
target_link_libraries(client ${PTHREAD_LIBRARY})

add_executable(test_delay src/test_delay.cpp)
target_link_libraries(test_delay ${PTHREAD_LIBRARY})

add_executable(twoway src/twoway.cpp)
target_link_libraries(twoway ${PTHREAD_LIBRARY})