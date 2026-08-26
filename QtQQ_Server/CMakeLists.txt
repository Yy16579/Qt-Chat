cmake_minimum_required(VERSION 3.16)
project(QtQQ_Server LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#MSVC 下强制按 UTF-8 解释源文件和执行字符集，解决中文字符串乱码
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Core
        Network               # QTcpServer / QTcpSocket / QUdpSocket 均依赖此模块
        Sql                   # 数据库访问
    REQUIRED
)
qt_standard_project_setup()

#服务端源码文件
set(PROJECT_SOURCES
    Protocol.h
    main.cpp
    ServerTask.h
    ServerTask.cpp
    TcpServer.h
    TcpServer.cpp
    TcpSocket.h
    TcpSocket.cpp

)

qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Core
        Qt::Network
        Qt::Sql
)

# ========== 自动复制配置文件到 exe 同级目录 ==========
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/config.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying config.ini to QtQQ_Server output directory"
)

# ========== 自动部署 Qt 运行时依赖到 exe 同级目录 ==========
set(_windeployqt "$ENV{QTDIR}/bin/windeployqt.exe")
if(NOT EXISTS "${_windeployqt}")
    set(_windeployqt "${CMAKE_PREFIX_PATH}/bin/windeployqt.exe")
endif()
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${_windeployqt}" --debug --no-translations
            "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "Running windeployqt to deploy Qt runtime dependencies"
)

# ========== 复制 MySQL 客户端运行库 ==========
set(_mysql_dll "C:/Program Files/MySQL/MySQL Server 8.0/lib/libmysql.dll")
if(EXISTS "${_mysql_dll}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_mysql_dll}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying libmysql.dll to QtQQ_Server output directory"
    )
else()
    message(WARNING "libmysql.dll not found at ${_mysql_dll}, "
                    "MySQL connection will fail at runtime")
endif()
