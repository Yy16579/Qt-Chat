cmake_minimum_required(VERSION 3.16)
project(QtQQ LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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
        Gui
        Widgets
        WebEngineCore      # WebEngineWidgets 的传递依赖，显式声明确保被加载
        Quick               # 同上
        QuickWidgets        # 同上（之前缓存中缺失，导致目标未创建）
        PrintSupport        # 同上（之前缓存中缺失，导致目标未创建）
        WebEngineWidgets
        Xml
        Sql                 # MySQL 数据库访问模块
        Network             # TCP 网络通信模块（QTcpSocket 依赖）
    REQUIRED                # 缺失立即报错，避免拖到链接阶段才暴露
)
qt_standard_project_setup()

set(PROJECT_SOURCES
    main.cpp
    CCMainWindow.ui
    CCMainWindow.h
    CCMainWindow.cpp
    CCMainWindow.qrc
    titlebar.h          
    titlebar.cpp
    basicwindow.h
    basicwindow.cpp
    NotifyManager.h
    NotifyManager.cpp
    CommonUtils.h
    CommonUtils.cpp
    UserLogin.ui
    UserLogin.h
    UserLogin.cpp
    SkinWindow.h
    SkinWindow.cpp
    SkinWindow.ui
    QClickLabel.h
    QClickLabel.cpp
    SysTray.h
    SysTray.cpp
    CustomMenu.h
    CustomMenu.cpp
    RootContactItem.h
    RootContactItem.cpp
    ContactItem.h
    ContactItem.cpp
    ContactItem.ui
    TalkWindowShell.h
    TalkWindowShell.cpp
    TalkWindowShell.ui
    EmotionWindow.h
    EmotionWindow.cpp
    EmotionWindow.ui
    EmotionLabelItem.h
    EmotionLabelItem.cpp
    TalkWindowItem.h
    TalkWindowItem.cpp
    TalkWindowItem.ui
    TalkWindow.h
    TalkWindow.cpp
    TalkWindow.ui
    WindowManager.h
    WindowManager.cpp
    TalkSessionStore.h
    TalkSessionStore.cpp
    QMsgTextEdit.h
    QMsgTextEdit.cpp
    MsgWebView.h
    MsgWebView.cpp
    TcpClient.h
    TcpClient.cpp
    Protocol.h

)

qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}  # 当前CMakeLists.txt所在根目录
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        WIN32_EXECUTABLE TRUE
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::WebEngineWidgets
        Qt::Xml
        Qt::Sql
        Qt::Network
)

# ========== 自动复制配置文件到 exe 同级目录 ==========
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/tradeprintinfo.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying tradeprintinfo.ini to output directory"
)

# 复制连接配置文件 config.ini（数据库 + TCP 连接参数）到 exe 同级目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/config.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying config.ini to output directory"
)

# ========== 自动部署 Qt 运行时依赖到 exe 同级目录 ==========
# 每次构建后自动运行 windeployqt，复制所需 Qt DLL 和插件，避免运行时找不到 DLL
# 优先使用 QTDIR 环境变量推导路径，兜底使用 CMAKE_PREFIX_PATH（CMakePresets.json 中配置）
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
# qsqlmysql.dll 运行时依赖 libmysql.dll，windeployqt 不会自动复制（非 Qt 自带 DLL）
set(_mysql_dll "C:/Program Files/MySQL/MySQL Server 8.0/lib/libmysql.dll")
if(EXISTS "${_mysql_dll}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_mysql_dll}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying libmysql.dll to output directory"
    )
else()
    message(WARNING "libmysql.dll not found at ${_mysql_dll}, "
                    "MySQL connection will fail at runtime")
endif()

