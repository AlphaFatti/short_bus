cmake_minimum_required(VERSION 3.22)
project(ShortBus VERSION 0.1.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(SHORT_BUS_BUILD_PLUGIN "Build the Short Bus VST3" ON)

include(CTest)
add_library(short_bus_dsp STATIC src/dsp/Rack.cpp)
target_include_directories(short_bus_dsp PUBLIC src)
set_target_properties(short_bus_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    add_executable(short_bus_dsp_tests tests/DspTests.cpp)
    target_link_libraries(short_bus_dsp_tests PRIVATE short_bus_dsp)
    add_test(NAME short_bus_dsp_tests COMMAND short_bus_dsp_tests)
endif()

if(SHORT_BUS_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
    juce_add_binary_data(ShortBusFonts
        HEADER_NAME ShortBusFontData.h
        NAMESPACE ShortBusFontData
        SOURCES assets/overpass-regular.otf assets/overpass-bold.otf)
    juce_add_plugin(ShortBus
        COMPANY_NAME "TD Productions"
        BUNDLE_ID "com.tdproductions.shortbus"
        PLUGIN_MANUFACTURER_CODE TdPr
        PLUGIN_CODE SbDy
        PRODUCT_NAME "Short Bus"
        FORMATS VST3
        VST3_CATEGORIES Fx Dynamics
        COPY_PLUGIN_AFTER_BUILD FALSE
    )
    target_sources(ShortBus PRIVATE src/Processor.cpp src/Editor.cpp)
    target_compile_definitions(ShortBus
        PRIVATE SHORT_BUS_VERSION="${PROJECT_VERSION}"
        PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(ShortBus
        PRIVATE short_bus_dsp ShortBusFonts juce::juce_audio_processors juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    if(BUILD_TESTING)
        add_executable(short_bus_vst3_smoke tests/Vst3Smoke.cpp)
        target_compile_definitions(short_bus_vst3_smoke PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_USE_CURL=0)
        target_link_libraries(short_bus_vst3_smoke PRIVATE juce::juce_audio_processors_headless)
        add_dependencies(short_bus_vst3_smoke ShortBus_VST3)
        add_test(NAME short_bus_vst3_smoke
            COMMAND short_bus_vst3_smoke "$<GENEX_EVAL:$<TARGET_PROPERTY:ShortBus_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    endif()
endif()
