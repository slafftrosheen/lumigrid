idf_component_register(SRCS "GammaLut.cpp" "PowerBudget.cpp" "SignageEffects.cpp" "SignageEngine.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES output config board esp_timer)
