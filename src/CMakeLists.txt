set(PluginName "assrender")

if (NOT WIN32)
  string(TOLOWER "${PluginName}" PluginName)
endif()

set(ProjectName "${PluginName}")
project(${ProjectName} LANGUAGES C)

set(ASSRender_SRC 
    assrender.c
    assrender.h
    render.c
    render.h
    sub.c
    sub.h
    timecodes.c
    timecodes.h
    )

if(WIN32)
    list(APPEND ASSRender_SRC "ASSRender.rc")
    if(NOT MINGW)
        if (CMAKE_SIZEOF_VOID_P EQUAL 4)
          list(APPEND ASSRender_SRC "assrender.def")
        else()
          list(APPEND ASSRender_SRC "assrender64.def")
        endif()
    endif()
endif()

add_library(${PluginName} SHARED ${ASSRender_SRC})

set_target_properties(${PluginName} PROPERTIES "OUTPUT_NAME" "${PluginName}")
if (MINGW)
  set_target_properties(${PluginName} PROPERTIES PREFIX "")
  set_target_properties(${PluginName} PROPERTIES IMPORT_PREFIX "")
endif()

if (NOT WIN32 OR MINGW)
  find_package(PkgConfig REQUIRED)
  PKG_CHECK_MODULES(LIBASS REQUIRED libass>=0.12.0)
  target_include_directories(${PluginName} PRIVATE ${LIBASS_INCLUDE_DIR})
endif()

#dedicated include dir for avisynth.h
target_include_directories(${ProjectName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (WIN32)
  target_link_libraries(${ProjectName} ${LIBASS_LDFLAGS})
endif()

include(GNUInstallDirs)

install(TARGETS ${ProjectName} LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/vapoursynth")
