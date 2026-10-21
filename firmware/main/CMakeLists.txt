idf_component_register(
    SRCS "app_main.cpp" "board/amoled.c" "display/eye_renderer.cpp" "behavior/blink.cpp" "behavior/body_behavior.cpp" "behavior/automatic_blink.cpp" "touch/touch_sensor.cpp" "touch/contact_detector.cpp" "touch/petting_detector.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_touch_sens esp_psram spi_flash esp_timer esp_hw_support)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

# 描画だけ速度最適化。浮動小数点の意味を変えるfast-mathは使わない。
set_source_files_properties(display/eye_renderer.cpp PROPERTIES COMPILE_OPTIONS "-O2")
