idf_component_register(SRCS "hostCore.c" "hostExchange.c" INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
