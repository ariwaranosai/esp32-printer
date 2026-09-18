idf_component_register(SRCS "frame.cpp" "image.cpp" "photo_cache.cpp" INCLUDE_DIRS "." PRIV_INCLUDE_DIRS "../../third_party")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
