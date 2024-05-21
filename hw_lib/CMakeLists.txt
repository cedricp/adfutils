set(ADF_LIBS "")
if(MINGW)
    set(ADF_LIBS "${LIBRARIES};setupAPI;ws2_32")
endif()

file(GLOB LIB_SOURCE *.cpp capsapi/*.cpp)
add_library(hw_lib STATIC ${LIB_SOURCE})
target_include_directories(hw_lib PUBLIC . ../adf_lib)
target_link_libraries(hw_lib PRIVATE ${ADF_LIBS})