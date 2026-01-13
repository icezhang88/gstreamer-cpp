cmake_minimum_required(VERSION 4.0)
project(gstreamer_cpp)

set(CMAKE_CXX_STANDARD 14)

# 手动指定GStreamer根目录（替换为你的实际路径）
set(GSTREAMER_ROOT "D:/gstreamer/1.0/mingw_x86_64")

# 手动设置头文件路径
set(GSTREAMER_INCLUDE_DIRS
        ${GSTREAMER_ROOT}/include/gstreamer-1.0
        ${GSTREAMER_ROOT}/include/glib-2.0
        ${GSTREAMER_ROOT}/lib/glib-2.0/include
        ${GSTREAMER_ROOT}/include
)

# 手动设置库文件路径（根据你的GStreamer版本/架构调整）
set(GSTREAMER_LIBRARIES
        ${GSTREAMER_ROOT}/lib/gstreamer-1.0.lib
        ${GSTREAMER_ROOT}/lib/glib-2.0.lib
        ${GSTREAMER_ROOT}/lib/gobject-2.0.lib
        # 按需添加其他组件库，比如app、video等
)

add_executable(gstreamer_cpp main.cpp)


# 添加头文件目录
target_include_directories(gstreamer_cpp PRIVATE ${GSTREAMER_INCLUDE_DIRS})
# 链接库文件
target_link_libraries(gstreamer_cpp PRIVATE ${GSTREAMER_LIBRARIES})