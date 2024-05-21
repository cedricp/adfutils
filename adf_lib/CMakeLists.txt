file(GLOB LIB_SOURCE *.c)
add_library(adf_lib STATIC ${LIB_SOURCE})
target_include_directories(adf_lib PUBLIC .)
target_link_libraries(adf_lib PRIVATE ${ADF_LIBS})