# 设置CMake最低版本要求
cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(TCPMultiThreadServer)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)

# 查找并添加需要的库
find_package(Threads REQUIRED)

# 添加源文件
add_executable(TCPMultiThreadServer TCPMultiThreadServer.cpp)
add_executable(TCPNBClient TCPNBClient.cpp)

# 链接库
target_link_libraries(TCPMultiThreadServer Threads::Threads ws2_32)
target_link_libraries(TCPNBClient ws2_32)