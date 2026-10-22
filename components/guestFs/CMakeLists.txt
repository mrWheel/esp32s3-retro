idf_component_register(SRCS "guestFs.c" "guestFsCpm.c" "guestFsApple.c" "guestFsProdos.c"
                            "guestFsPascal.c" "guestFsDos33.c" INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
