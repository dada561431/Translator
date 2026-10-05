cmake_minimum_required(VERSION 3.20)

project(Translator VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

set(REALTIME_SOURCES
    src/app/RealtimePipelineCoordinator.h src/app/RealtimePipelineCoordinator.cpp
    src/processing/FrameComparator.h src/processing/FrameComparator.cpp
    src/processing/TextDeduplicator.h src/processing/TextDeduplicator.cpp
)

set(TRANSLATION_SOURCES
    src/app/TranslationCoordinator.h
    src/app/TranslationCoordinator.cpp
    src/translator/TranslatorFactory.h
    src/translator/TranslatorFactory.cpp
)

set(PADDLE_OCR_SOURCES
    src/ocr/PaddleOcrEngine.h src/ocr/PaddleOcrEngine.cpp
    src/ocr/PaddleRuntimeLocator.h src/ocr/PaddleRuntimeLocator.cpp
)

add_library(TranslatorProviders STATIC
    src/translator/TranslationTypes.h
    src/translator/ITranslator.h
    src/translator/TranslationLanguageMapper.h
    src/translator/TranslationLanguageMapper.cpp
    src/translator/DeepLTranslator.h
    src/translator/DeepLTranslator.cpp
    src/translator/OpenAiCompatibleTranslator.h
    src/translator/OpenAiCompatibleTranslator.cpp
    src/translator/TranslationProviderRegistry.h
    src/translator/TranslationProviderRegistry.cpp
    src/credentials/ICredentialStore.h
    src/credentials/WindowsCredentialStore.h
    src/credentials/WindowsCredentialStore.cpp
)
target_include_directories(TranslatorProviders PUBLIC src)
target_link_libraries(TranslatorProviders PUBLIC Qt6::Core Qt6::Network)
if(WIN32)
    target_link_libraries(TranslatorProviders PRIVATE advapi32)
endif()

add_executable(Translator
    ${PADDLE_OCR_SOURCES}
    src/ocr/OcrEngineFactory.h src/ocr/OcrEngineFactory.cpp
    ${REALTIME_SOURCES}
    ${TRANSLATION_SOURCES}
    src/main.cpp
    src/app/RuntimeSelfCheck.cpp src/app/RuntimeSelfCheck.h
    src/app/CaptureCoordinator.cpp
    src/app/CaptureCoordinator.h
    src/app/OcrCoordinator.cpp
    src/app/OcrCoordinator.h
    src/capture/RegionSelector.cpp
    src/capture/RegionSelector.h
    src/capture/ScreenCaptureService.cpp
    src/capture/ScreenCaptureService.h
    src/config/SettingsManager.cpp
    src/config/SettingsManager.h
    src/gui/SettingsDialog.cpp
    src/gui/SettingsDialog.h
    src/gui/TranslationWindow.cpp
    src/gui/TranslationWindow.h
    src/ocr/IOcrEngine.h
    src/ocr/OcrImagePreprocessor.cpp
    src/ocr/OcrImagePreprocessor.h
    src/ocr/OcrTypes.h
    src/ocr/TesseractOcrEngine.cpp
    src/ocr/TesseractOcrEngine.h
)

target_include_directories(Translator PRIVATE src)

target_link_libraries(Translator PRIVATE
    TranslatorProviders
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

set_target_properties(Translator PROPERTIES
    WIN32_EXECUTABLE TRUE
)

include(CTest)

if(BUILD_TESTING)
    add_executable(TranslatorRuntimeLocatorTests tests/RuntimeLocatorTest.cpp ${PADDLE_OCR_SOURCES})
    target_include_directories(TranslatorRuntimeLocatorTests PRIVATE src)
    target_link_libraries(TranslatorRuntimeLocatorTests PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME phase61c_runtime_locator COMMAND TranslatorRuntimeLocatorTests)
    add_executable(TranslatorPaddleProbe
        tests/PaddleIntegrationProbe.cpp ${PADDLE_OCR_SOURCES} ${REALTIME_SOURCES} ${TRANSLATION_SOURCES}
        src/app/CaptureCoordinator.cpp src/app/CaptureCoordinator.h
        src/capture/RegionSelector.cpp src/capture/RegionSelector.h
        src/ocr/OcrEngineFactory.cpp src/ocr/OcrEngineFactory.h
        src/ocr/TesseractOcrEngine.cpp src/ocr/OcrImagePreprocessor.cpp
        src/app/OcrCoordinator.cpp src/app/OcrCoordinator.h
        src/capture/ScreenCaptureService.cpp
        src/config/SettingsManager.cpp src/config/SettingsManager.h
        src/gui/TranslationWindow.cpp src/gui/TranslationWindow.h
        src/gui/SettingsDialog.cpp src/gui/SettingsDialog.h
    )
    target_include_directories(TranslatorPaddleProbe PRIVATE src)
    target_link_libraries(TranslatorPaddleProbe PRIVATE TranslatorProviders Qt6::Gui Qt6::Widgets)
    add_executable(TranslatorTesseractBatch
        tools/ocr_benchmark/TesseractBatch.cpp
        src/ocr/TesseractOcrEngine.cpp src/ocr/TesseractOcrEngine.h
        src/ocr/OcrImagePreprocessor.cpp src/ocr/OcrImagePreprocessor.h
    )
    target_include_directories(TranslatorTesseractBatch PRIVATE src)
    target_link_libraries(TranslatorTesseractBatch PRIVATE Qt6::Core Qt6::Gui)
    if(WIN32)
        target_link_libraries(TranslatorTesseractBatch PRIVATE psapi)
    endif()
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME phase61c_package_validator COMMAND ${Python3_EXECUTABLE}
            -m unittest discover -s scripts -p test_*.py)
        set_tests_properties(phase61c_package_validator PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME phase61b1_false_positive_analysis COMMAND ${Python3_EXECUTABLE}
            -m unittest discover -s tools/ocr_false_positive_analysis -p test_*.py)
        set_tests_properties(phase61b1_false_positive_analysis PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME phase61b_helper_protocol COMMAND ${Python3_EXECUTABLE}
            -m unittest discover -s helpers/ocr -p test_*.py)
        set_tests_properties(phase61b_helper_protocol PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME phase61a_benchmark_harness COMMAND ${Python3_EXECUTABLE}
            -m unittest discover -s tools/ocr_benchmark -p test_*.py)
        set_tests_properties(phase61a_benchmark_harness PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    add_executable(TranslatorPhase61Tests
        tests/Phase61PaddleTest.cpp ${PADDLE_OCR_SOURCES}
        src/ocr/OcrEngineFactory.h src/ocr/OcrEngineFactory.cpp
        src/ocr/TesseractOcrEngine.h src/ocr/TesseractOcrEngine.cpp
        src/ocr/OcrImagePreprocessor.h src/ocr/OcrImagePreprocessor.cpp
        src/app/OcrCoordinator.h src/app/OcrCoordinator.cpp
        src/config/SettingsManager.h src/config/SettingsManager.cpp
        src/gui/SettingsDialog.h src/gui/SettingsDialog.cpp
    )
    target_include_directories(TranslatorPhase61Tests PRIVATE src)
    target_link_libraries(TranslatorPhase61Tests PRIVATE TranslatorProviders Qt6::Gui Qt6::Widgets)
    add_test(NAME phase61b_persistent_helper COMMAND TranslatorPhase61Tests)
    set_tests_properties(phase61b_persistent_helper PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    add_executable(TranslatorPhase6Tests
        tests/Phase6RealtimeTest.cpp
        ${REALTIME_SOURCES}
        src/app/OcrCoordinator.h src/app/OcrCoordinator.cpp
        src/capture/ScreenCaptureService.h src/capture/ScreenCaptureService.cpp
        src/config/SettingsManager.h src/config/SettingsManager.cpp
        ${TRANSLATION_SOURCES}
    )
    target_include_directories(TranslatorPhase6Tests PRIVATE src)
    target_link_libraries(TranslatorPhase6Tests PRIVATE TranslatorProviders Qt6::Gui Qt6::Widgets)
    add_executable(TranslatorRealtimeProbe
        tests/RealtimeProbe.cpp
        ${REALTIME_SOURCES}
        src/app/OcrCoordinator.h src/app/OcrCoordinator.cpp
        src/capture/ScreenCaptureService.h src/capture/ScreenCaptureService.cpp
        src/config/SettingsManager.h src/config/SettingsManager.cpp
        src/ocr/TesseractOcrEngine.h src/ocr/TesseractOcrEngine.cpp
        src/ocr/OcrImagePreprocessor.h src/ocr/OcrImagePreprocessor.cpp
        ${TRANSLATION_SOURCES}
    )
    target_include_directories(TranslatorRealtimeProbe PRIVATE src)
    target_link_libraries(TranslatorRealtimeProbe PRIVATE TranslatorProviders Qt6::Gui Qt6::Widgets)
    add_test(NAME phase6_realtime_pipeline COMMAND TranslatorPhase6Tests)
    set_tests_properties(phase6_realtime_pipeline PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(TranslatorPhase51Tests
        tests/Phase51ProviderTest.cpp
        ${TRANSLATION_SOURCES}
        src/config/SettingsManager.h src/config/SettingsManager.cpp
        src/gui/SettingsDialog.h src/gui/SettingsDialog.cpp
    )
    target_include_directories(TranslatorPhase51Tests PRIVATE src)
    target_link_libraries(TranslatorPhase51Tests PRIVATE TranslatorProviders Qt6::Gui Qt6::Widgets)
    add_test(NAME phase51_provider_and_credentials COMMAND TranslatorPhase51Tests)
    set_tests_properties(phase51_provider_and_credentials PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(TranslatorCredentialProbe tests/CredentialProbe.cpp)
    target_link_libraries(TranslatorCredentialProbe PRIVATE TranslatorProviders)
    add_executable(TranslatorPhase5Tests
        tests/Phase5TranslationTest.cpp
        ${TRANSLATION_SOURCES}
        src/config/SettingsManager.h
        src/config/SettingsManager.cpp
        src/gui/SettingsDialog.h
        src/gui/SettingsDialog.cpp
        src/gui/TranslationWindow.h
        src/gui/TranslationWindow.cpp
    )
    target_include_directories(TranslatorPhase5Tests PRIVATE src)
    target_link_libraries(TranslatorPhase5Tests PRIVATE TranslatorProviders Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
    add_test(NAME phase5_translation COMMAND TranslatorPhase5Tests)
    set_tests_properties(phase5_translation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(TranslatorDeepLProbe
        tests/DeepLProbe.cpp
        src/config/SettingsManager.h
        src/config/SettingsManager.cpp
    )
    target_include_directories(TranslatorDeepLProbe PRIVATE src)
    target_link_libraries(TranslatorDeepLProbe PRIVATE TranslatorProviders Qt6::Gui)
    add_executable(TranslatorPhase2Tests
        tests/Phase2UiTest.cpp
        src/config/SettingsManager.cpp
        src/config/SettingsManager.h
        src/gui/SettingsDialog.cpp
        src/gui/SettingsDialog.h
        src/gui/TranslationWindow.cpp
        src/gui/TranslationWindow.h
    )

    target_include_directories(TranslatorPhase2Tests PRIVATE src)

    target_link_libraries(TranslatorPhase2Tests PRIVATE
        TranslatorProviders
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )

    add_test(NAME phase2_ui_and_settings COMMAND TranslatorPhase2Tests)
    set_tests_properties(phase2_ui_and_settings PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(TranslatorPhase3Tests
        tests/Phase3CaptureTest.cpp
        src/capture/RegionSelector.cpp
        src/capture/RegionSelector.h
        src/capture/ScreenCaptureService.cpp
        src/capture/ScreenCaptureService.h
        src/config/SettingsManager.cpp
        src/config/SettingsManager.h
    )
    target_include_directories(TranslatorPhase3Tests PRIVATE src)
    target_link_libraries(TranslatorPhase3Tests PRIVATE TranslatorProviders Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME phase3_capture_logic COMMAND TranslatorPhase3Tests)
    set_tests_properties(phase3_capture_logic PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(TranslatorPhase4Tests
        tests/Phase4OcrTest.cpp
        src/app/OcrCoordinator.cpp
        src/app/OcrCoordinator.h
        src/config/SettingsManager.cpp
        src/config/SettingsManager.h
        src/gui/SettingsDialog.cpp
        src/gui/SettingsDialog.h
        src/gui/TranslationWindow.cpp
        src/gui/TranslationWindow.h
        src/ocr/IOcrEngine.h
        src/ocr/OcrTypes.h
    )
    target_include_directories(TranslatorPhase4Tests PRIVATE src)
    target_link_libraries(TranslatorPhase4Tests PRIVATE TranslatorProviders Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME phase4_ocr_logic COMMAND TranslatorPhase4Tests)
    set_tests_properties(phase4_ocr_logic PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(TranslatorPhase41Tests
        tests/Phase41OcrTuningTest.cpp
        src/ocr/IOcrEngine.h
        src/ocr/OcrImagePreprocessor.cpp
        src/ocr/OcrImagePreprocessor.h
        src/ocr/OcrTypes.h
        src/ocr/TesseractOcrEngine.cpp
        src/ocr/TesseractOcrEngine.h
    )
    target_include_directories(TranslatorPhase41Tests PRIVATE src)
    target_link_libraries(TranslatorPhase41Tests PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME phase41_ocr_tuning COMMAND TranslatorPhase41Tests)

    add_executable(TranslatorOcrBenchmark
        tests/OcrBenchmark.cpp
        src/ocr/IOcrEngine.h
        src/ocr/OcrImagePreprocessor.cpp
        src/ocr/OcrImagePreprocessor.h
        src/ocr/OcrTypes.h
        src/ocr/TesseractOcrEngine.cpp
        src/ocr/TesseractOcrEngine.h
    )
    target_include_directories(TranslatorOcrBenchmark PRIVATE src)
    target_link_libraries(TranslatorOcrBenchmark PRIVATE Qt6::Core Qt6::Gui)

    add_executable(TranslatorOcrSampleGenerator
        tests/OcrSampleGenerator.cpp
    )
    target_link_libraries(TranslatorOcrSampleGenerator PRIVATE Qt6::Core Qt6::Gui)

    add_executable(TranslatorScreenOcrProbe
        tests/ScreenOcrProbe.cpp
        src/capture/ScreenCaptureService.cpp
        src/capture/ScreenCaptureService.h
        src/ocr/IOcrEngine.h
        src/ocr/OcrImagePreprocessor.cpp
        src/ocr/OcrImagePreprocessor.h
        src/ocr/OcrTypes.h
        src/ocr/TesseractOcrEngine.cpp
        src/ocr/TesseractOcrEngine.h
    )
    target_include_directories(TranslatorScreenOcrProbe PRIVATE src)
    target_link_libraries(TranslatorScreenOcrProbe PRIVATE Qt6::Core Qt6::Gui)
endif()
