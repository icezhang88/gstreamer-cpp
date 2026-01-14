cmake_minimum_required(VERSION 3.30)
project(gstreamer_cpp)

set(CMAKE_CXX_STANDARD 14)


add_executable(gstreamer_cpp main.cpp)



if(WIN32)
    message(STATUS "system:windows")
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
    # 添加头文件目录
    target_include_directories(gstreamer_cpp PRIVATE ${GSTREAMER_INCLUDE_DIRS})
    # 链接库文件
    target_link_libraries(gstreamer_cpp PRIVATE ${GSTREAMER_LIBRARIES})
elseif (UNIX AND NOT APPLE)
    message(STATUS "system:LINUX")
elseif (APPLE)
    message(STATUS "system:macOS")
    execute_process(
            COMMAND sw_vers -productVersion
            OUTPUT_VARIABLE MACOS_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "macOS 版本：${MACOS_VERSION}")
    # 1. 查找 pkg-config 工具（必须）
    find_package(PkgConfig REQUIRED)
    # 2. 通过 pkg-config 查找 GStreamer 1.0 库
    #    会自动调用 pkg-config --cflags --libs gstreamer-1.0
    #    并将结果存入 GSTREAMER 变量中
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
    # 4. 配置编译参数（头文件路径）
    #    GSTREAMER_INCLUDE_DIRS 对应 pkg-config --cflags 的结果
    target_include_directories(${PROJECT_NAME}
            PRIVATE
            ${GSTREAMER_INCLUDE_DIRS}
            # 如果加了扩展模块，补充：
            # ${GSTREAMER_VIDEO_INCLUDE_DIRS}
            # ${GSTREAMER_AUDIO_INCLUDE_DIRS}
    )

    # 5. 配置链接参数（库文件和链接选项）
    #    GSTREAMER_LIBRARIES 对应 pkg-config --libs 的结果
    #    GSTREAMER_LDFLAGS 包含链接路径等额外参数
    target_link_directories(${PROJECT_NAME}
            PRIVATE
            ${GSTREAMER_LIBRARY_DIRS}
            # 扩展模块补充：
            # ${GSTREAMER_VIDEO_LIBRARY_DIRS}
    )

    target_link_libraries(${PROJECT_NAME}
            PRIVATE
            ${GSTREAMER_LIBRARIES}
            # 扩展模块补充：
            # ${GSTREAMER_VIDEO_LIBRARIES}
    )

    # 可选：传递编译定义（如果需要）
    target_compile_definitions(${PROJECT_NAME}
            PRIVATE
            ${GSTREAMER_CFLAGS_OTHER}
    )
elseif(UNIX)
    message(STATUS "当前操作系统：其他 Unix 类系统")
    add_definitions(-D_UNIX_)
    # 5. 未知系统
else()
    message(WARNING "无法识别的操作系统！")

endif ()
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_HOST_SYSTEM_NAME: ${CMAKE_HOST_SYSTEM_NAME}")
# 手动指定GStreamer根目录（替换为你的实际路径）


