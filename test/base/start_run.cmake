if(ZIS_MOD_HELLO)

    set(hello_tests_args 1 2)
    set(hello_tests_pass_regex "^Hello, World!\nHello, 1!\nHello, 2!\n$")

    # Run a module by file path.
    if(NOT ZIS_MOD_HELLO_EMBED)
        add_test(
            NAME base-start_run_f
            COMMAND "$<TARGET_FILE:${zis_start_tgt}>" "$<TARGET_FILE:zis_mod_hello>" ${hello_tests_args}
        )
        set_tests_properties(
            base-start_run_f PROPERTIES
            PASS_REGULAR_EXPRESSION "${hello_tests_pass_regex}"
        )
    endif()

    # Run a module by its name.
    add_test(
        NAME base-start_run_m
        COMMAND "$<TARGET_FILE:${zis_start_tgt}>" "@hello" ${hello_tests_args}
    )
    if(NOT ZIS_MOD_HELLO_EMBED)
        set_tests_properties(
            base-start_run_m PROPERTIES
            ENVIRONMENT "${ZIS_ENVIRON_NAME_PATH}=$<TARGET_FILE_DIR:zis_mod_hello>"
        )
    endif()
    set_tests_properties(
        base-start_run_m PROPERTIES
        PASS_REGULAR_EXPRESSION "${hello_tests_pass_regex}"
    )

endif()
