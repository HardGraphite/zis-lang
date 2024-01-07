if(ZIS_MOD_HELLO)

    # Run a module by file path.
    if(NOT ZIS_MOD_HELLO_EMBED)
        add_test(
            NAME zis_test_start_run_f
            COMMAND "$<TARGET_FILE:${zis_start_tgt}>" "$<TARGET_FILE:zis_mod_hello>"
        )
        set_tests_properties(
            zis_test_start_run_f PROPERTIES
            PASS_REGULAR_EXPRESSION "^Hello, World!\n$"
        )
    endif()

    # Run a module by its name.
    add_test(
        NAME zis_test_start_run_m
        COMMAND "$<TARGET_FILE:${zis_start_tgt}>" "@hello"
    )
    if(NOT ZIS_MOD_HELLO_EMBED)
        set_tests_properties(
            zis_test_start_run_m PROPERTIES
            ENVIRONMENT "${ZIS_ENVIRON_NAME_PATH}=$<TARGET_FILE_DIR:zis_mod_hello>"
        )
    endif()
    set_tests_properties(
        zis_test_start_run_m PROPERTIES
        PASS_REGULAR_EXPRESSION "^Hello, World!\n$"
    )

endif()
