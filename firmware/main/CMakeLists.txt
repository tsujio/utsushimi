idf_component_register(
    SRCS
        "app_main.cpp"
        "diagnostics/task_load.cpp"
        "audio/audio_output.cpp"
        "audio/wake_word.cpp"
        "board/amoled.c"
        "display/eye_renderer.cpp"
        "behavior/blink.cpp"
        "behavior/body_behavior.cpp"
        "behavior/automatic_blink.cpp"
        "touch/touch_sensor.cpp"
        "touch/contact_detector.cpp"
        "touch/petting_detector.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_i2s esp_lcd esp_driver_spi esp_driver_i2c esp_driver_touch_sens
        esp_psram spi_flash esp_timer esp_hw_support)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

# 描画と認識入力のFIRを速度最適化。fast-mathは使わない。
set_source_files_properties(display/eye_renderer.cpp audio/wake_word.cpp PROPERTIES COMPILE_OPTIONS "-O2")

if(CONFIG_UTSUSHIMI_WAKE_WORD)
    if(NOT CONFIG_PARTITION_TABLE_CUSTOM OR NOT CONFIG_MODEL_IN_FLASH)
        # Keep menuconfig accessible when migrating an existing sdkconfig.
        message(WARNING "Wake word will not load: select custom partitions.csv and MODEL_IN_FLASH in menuconfig; see main/README.md Step 2.3")
    else()
        partition_table_get_partition_info(model_size "--partition-name model" "size")
        if(NOT model_size OR model_size LESS 1048576)
            message(WARNING "Wake word requires model partition >= 1MiB; see main/README.md Step 2.3")
        endif()
    endif()
endif()
