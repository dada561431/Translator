#include "asr/WhisperCppAsrBackend.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QtEndian>
#include <iostream>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir; int failures = 0;
    auto check = [&](bool ok, const char *name) { if (!ok) { ++failures; std::cerr << name << '\n'; } };
    auto token = std::make_shared<Asr::Cancellation>();
    token->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    WhisperCppAsrBackend backend;
    check(backend.recognize(std::vector<float>(320), {}, token).error.code == Asr::ErrorCode::ModelNotLoaded, "inference before load");
    check(backend.loadModel(dir.filePath("missing"), token).code == Asr::ErrorCode::ModelLoadFailed, "missing safe");
    check(backend.loadModel(dir.path(), token).code == Asr::ErrorCode::ModelLoadFailed, "directory/unreadable safe");
    QFile corrupt(dir.filePath("corrupt.bin"));
    if (!corrupt.open(QIODevice::WriteOnly)) return 2;
    corrupt.write("bad header"); corrupt.close();
    check(backend.loadModel(corrupt.fileName(), token).code == Asr::ErrorCode::ModelLoadFailed, "corrupt safe");
#ifdef Q_OS_WIN
    const auto filename = corrupt.fileName();
    HANDLE exclusive = CreateFileW(reinterpret_cast<LPCWSTR>(filename.utf16()), GENERIC_READ,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(exclusive != INVALID_HANDLE_VALUE, "exclusive unreadable fixture opened");
    if (exclusive != INVALID_HANDLE_VALUE) {
        check(backend.loadModel(filename, token).code == Asr::ErrorCode::ModelLoadFailed, "locked unreadable model safe");
        CloseHandle(exclusive);
    }
#endif
    QByteArray header(48, '\0');
    const qint32 words[] = {0x67676d6c, 51865, 1500, 512, 8, 6, 448, 512, 8, 6, 80, 1};
    for (int i = 0; i < 12; ++i) qToLittleEndian(words[i], header.data() + i * 4);
    if (!corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    corrupt.write(header); corrupt.close();
    check(backend.loadModel(corrupt.fileName(), token).code == Asr::ErrorCode::ModelLoadFailed, "valid header truncated body safe");
    backend.unloadModel(); backend.unloadModel();
    std::cout << "Whisper contract failures=" << failures << '\n'; return failures ? 1 : 0;
}
