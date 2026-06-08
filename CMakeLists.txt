cmake_minimum_required(VERSION 3.19)
project(stc LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

qt_standard_project_setup()

qt_add_executable(stc
    main.cpp
    torrentfile.h torrentfile.cpp
)

target_link_libraries(stc
    PRIVATE
        Qt::Core
)

install(TARGETS stc
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
