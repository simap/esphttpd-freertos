idf_component_register(SRCS "cgi.c"
                            "cgi-test.c"
                            "io.c"
                            "user_main.c"
                    INCLUDE_DIRS ""
                    REQUIRES "espfs"
                             "libesphttpd"
                             "nvs_flash")
