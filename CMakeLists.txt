cmake_minimum_required(VERSION 3.14)
project(LOBEngine)

# Enforce C++17 standard for std::shared_mutex and updated standard library features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Enable FetchContent to download dependencies
include(FetchContent)

# Fetch cpp-httplib
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.15.3
)
FetchContent_MakeAvailable(httplib)

# Fetch nlohmann-json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# Create the executable
add_executable(lob_engine main.cpp)

# Tell the compiler where to find OrderBook.hpp
target_include_directories(lob_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link the downloaded libraries and the system threading library
target_link_libraries(lob_engine PRIVATE httplib::httplib nlohmann_json::nlohmann_json pthread)
