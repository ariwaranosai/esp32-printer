idf_component_register(SRCS "weather.cpp" INCLUDE_DIRS "." REQUIRES frame json)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
