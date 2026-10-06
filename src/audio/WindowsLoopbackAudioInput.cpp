#include "audio/WindowsLoopbackAudioInput.h"
#include "audio/AudioChunkBuffer.h"
#include "audio/PcmConverter.h"
#include <QLoggingCategory>
#include <QTimer>
#include <QSignalBlocker>
#include <atomic>
#include <mutex>
#include <thread>

Q_DECLARE_LOGGING_CATEGORY(audioLog)
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>

namespace {
template<class T> struct ComPtr {
    T *p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T **put() { return &p; }
    T *operator->() const { return p; }
};
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
struct EventHandle {
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~EventHandle() { if (handle) CloseHandle(handle); }
};
struct TaskMemory {
    WAVEFORMATEX *format = nullptr;
    ~TaskMemory() { CoTaskMemFree(format); }
};
Audio::Error nativeError(const char *api, HRESULT hr, const QByteArray &id = {})
{
    Audio::ErrorCode code = Audio::ErrorCode::BackendFailure;
    if (hr == E_ACCESSDENIED) code = Audio::ErrorCode::PermissionDenied;
    else if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
        code = Audio::ErrorCode::DeviceUnavailable;
    else if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) code = Audio::ErrorCode::UnsupportedFormat;
    else if (QByteArray(api).contains("Initialize") || QByteArray(api).contains("Activate")
             || QByteArray(api).contains("IAudioClient::Start")) code = Audio::ErrorCode::OpenFailed;
    if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && QByteArray(api) == "GetDefaultAudioEndpoint")
        code = Audio::ErrorCode::NoDevice;
    return {code, QStringLiteral("Unable to capture system audio. Check the selected output device and Start again."),
        QStringLiteral("%1 HRESULT=0x%2 endpoint=%3").arg(QLatin1String(api))
            .arg(quint32(hr), 8, 16, QLatin1Char('0')).arg(QString::fromUtf8(id))};
}
void checked(HRESULT hr, const char *api, const QByteArray &id = {})
{
    if (FAILED(hr)) throw nativeError(api, hr, id);
}
QByteArray endpointId(IMMDevice *device)
{
    LPWSTR value = nullptr;
    checked(device->GetId(&value), "IMMDevice::GetId");
    const auto id = QString::fromWCharArray(value).toUtf8();
    CoTaskMemFree(value); return id;
}
Audio::NativeFormat convertFormat(const WAVEFORMATEX *f)
{
    WORD tag = f->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (f->cbSize < 22) return {};
        const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(f);
        // MinGW declares these KS GUID symbols but does not supply their storage.
        static const GUID pcm = {STATIC_KSDATAFORMAT_SUBTYPE_PCM};
        static const GUID ieeeFloat = {STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT};
        if (IsEqualGUID(ext->SubFormat, pcm)) tag = WAVE_FORMAT_PCM;
        else if (IsEqualGUID(ext->SubFormat, ieeeFloat)) tag = WAVE_FORMAT_IEEE_FLOAT;
        else return {};
    }
    Audio::NativeFormat result{int(f->nSamplesPerSec), int(f->nChannels), Audio::SampleType::Int16};
    if (tag == WAVE_FORMAT_IEEE_FLOAT && f->wBitsPerSample == 32) result.sampleType = Audio::SampleType::Float32;
    else if (tag == WAVE_FORMAT_PCM) {
        switch (f->wBitsPerSample) {
        case 8: result.sampleType = Audio::SampleType::UInt8; break;
        case 16: result.sampleType = Audio::SampleType::Int16; break;
        case 24: result.sampleType = Audio::SampleType::Int24; break;
        case 32: result.sampleType = Audio::SampleType::Int32; break;
        default: return {};
        }
    } else return {};
    if (f->nBlockAlign != result.channels * result.bytesPerSample()) return {};
    return result;
}
}
#endif

struct WindowsLoopbackAudioInput::Impl {
    QTimer delivery;
    AudioChunkBuffer chunks;
    std::atomic_bool active{false}, stopping{false};
    std::thread worker;
    mutable std::mutex mutex;
    Audio::NativeFormat format;
    Audio::Error error;
    bool ready = false;
    QByteArray selectedId;
#ifdef Q_OS_WIN
    HANDLE stopEvent = nullptr;
#endif
};
WindowsLoopbackAudioInput::WindowsLoopbackAudioInput(QObject *parent)
    : IAudioInput(parent), impl_(std::make_unique<Impl>())
{
    impl_->delivery.setInterval(20);
    connect(&impl_->delivery, &QTimer::timeout, this, &WindowsLoopbackAudioInput::drain);
}
WindowsLoopbackAudioInput::~WindowsLoopbackAudioInput() { stop(); }
bool WindowsLoopbackAudioInput::running() const { return impl_->active; }
quint64 WindowsLoopbackAudioInput::droppedChunks() const { return impl_->chunks.dropped(); }
Audio::NativeFormat WindowsLoopbackAudioInput::nativeFormat() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->format;
}
Audio::DeviceInfo WindowsLoopbackAudioInput::selectedDevice() const
{
    QByteArray id;
    { std::lock_guard<std::mutex> lock(impl_->mutex); id = impl_->selectedId; }
    for (const auto &device : devices()) if (device.id == id) return device;
    return {id, QString::fromUtf8(id), Audio::InputKind::SystemLoopback, false};
}
QList<Audio::DeviceInfo> WindowsLoopbackAudioInput::devices() const
{
    QList<Audio::DeviceInfo> result;
#ifdef Q_OS_WIN
    ComScope com;
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE) return result;
    try {
        ComPtr<IMMDeviceEnumerator> enumerator;
        checked(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(enumerator.put())), "CoCreateInstance");
        QByteArray defaultId;
        ComPtr<IMMDevice> defaultDevice;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, defaultDevice.put())))
            defaultId = endpointId(defaultDevice.p);
        ComPtr<IMMDeviceCollection> collection;
        checked(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.put()), "EnumAudioEndpoints");
        UINT count = 0; checked(collection->GetCount(&count), "GetCount");
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device; checked(collection->Item(i, device.put()), "Item");
            const auto id = endpointId(device.p);
            QString name = QString::fromUtf8(id);
            ComPtr<IPropertyStore> properties;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.put()))) {
                PROPVARIANT value; PropVariantInit(&value);
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
                    name = QString::fromWCharArray(value.pwszVal);
                PropVariantClear(&value);
            }
            result.append({id, name, Audio::InputKind::SystemLoopback, id == defaultId});
        }
    } catch (const Audio::Error &e) { qCWarning(audioLog).noquote() << e.detail; }
#endif
    return result;
}
void WindowsLoopbackAudioInput::start(const QByteArray &id)
{
    if (impl_->worker.joinable()) return;
#ifndef Q_OS_WIN
    Q_UNUSED(id);
    emit errorOccurred({Audio::ErrorCode::UnsupportedFormat,
        QStringLiteral("System loopback capture is only available on Windows."), {}});
#else
    stop();
    impl_->stopping = false; impl_->chunks.clear();
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->ready = false; impl_->error = {}; impl_->format = {}; }
    impl_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!impl_->stopEvent) {
        emit errorOccurred(nativeError("CreateEvent", HRESULT_FROM_WIN32(GetLastError()), id)); return;
    }
    try {
        impl_->worker = std::thread([this, id] {
            try {
                ComScope com; checked(com.hr, "CoInitializeEx", id);
                ComPtr<IMMDeviceEnumerator> enumerator;
                checked(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                    __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(enumerator.put())), "CoCreateInstance", id);
                ComPtr<IMMDevice> device;
                if (id.isEmpty()) checked(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()), "GetDefaultAudioEndpoint");
                else checked(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(QString::fromUtf8(id).utf16()), device.put()), "GetDevice", id);
                const auto selectedId = endpointId(device.p);
                ComPtr<IAudioClient> client;
                checked(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                    reinterpret_cast<void **>(client.put())), "IMMDevice::Activate", selectedId);
                TaskMemory native;
                checked(client->GetMixFormat(&native.format), "GetMixFormat", selectedId);
                const auto format = convertFormat(native.format);
                PcmConverter converter;
                if (!converter.reset(format)) throw Audio::Error{Audio::ErrorCode::UnsupportedFormat,
                    QStringLiteral("System output mix format is unsupported."), QStringLiteral("Only PCM 8/16/24/32 or float32, 1-8 channels, 8-192 kHz supported.")};
                checked(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    1000000, 0, native.format, nullptr), "IAudioClient::Initialize", selectedId);
                EventHandle event;
                if (!event.handle) throw nativeError("CreateEvent", HRESULT_FROM_WIN32(GetLastError()), selectedId);
                checked(client->SetEventHandle(event.handle), "SetEventHandle", selectedId);
                ComPtr<IAudioCaptureClient> capture;
                checked(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(capture.put())), "GetService", selectedId);
                checked(client->Start(), "IAudioClient::Start", selectedId);
                struct StopClient { IAudioClient *p; ~StopClient() { p->Stop(); } } stopClient{client.p};
                {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    impl_->format = format; impl_->selectedId = selectedId; impl_->ready = true;
                }
                impl_->active = true;
                HANDLE events[] = {impl_->stopEvent, event.handle};
                qint64 lastDeviceCheck = Audio::monotonicUs();
                while (!impl_->stopping) {
                    const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 250);
                    if (wait == WAIT_OBJECT_0) break;
                    if (wait == WAIT_FAILED) throw nativeError("WaitForMultipleObjects", HRESULT_FROM_WIN32(GetLastError()), selectedId);
                    if (Audio::monotonicUs() - lastDeviceCheck >= 250000) {
                        lastDeviceCheck = Audio::monotonicUs();
                        DWORD state = 0; checked(device->GetState(&state), "GetState", selectedId);
                        if (state != DEVICE_STATE_ACTIVE) throw nativeError("Device inactive", AUDCLNT_E_DEVICE_INVALIDATED, selectedId);
                        if (id.isEmpty()) {
                            ComPtr<IMMDevice> current;
                            checked(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, current.put()), "GetDefaultAudioEndpoint", selectedId);
                            if (endpointId(current.p) != selectedId) throw nativeError("Default output changed", AUDCLNT_E_DEVICE_INVALIDATED, selectedId);
                        }
                    }
                    UINT32 frames = 0;
                    checked(capture->GetNextPacketSize(&frames), "GetNextPacketSize", selectedId);
                    while (frames && !impl_->stopping) {
                        BYTE *bytes = nullptr; DWORD flags = 0;
                        checked(capture->GetBuffer(&bytes, &frames, &flags, nullptr, nullptr), "GetBuffer", selectedId);
                        if (frames > UINT32(format.sampleRate)) {
                            capture->ReleaseBuffer(frames);
                            throw Audio::Error{Audio::ErrorCode::Overflow, QStringLiteral("System audio packet exceeded the capture buffer limit."), {}};
                        }
                        const bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
                        const QByteArray packet = silent ? QByteArray{} : QByteArray(reinterpret_cast<const char *>(bytes),
                            qsizetype(frames) * native.format->nBlockAlign);
                        checked(capture->ReleaseBuffer(frames), "ReleaseBuffer", selectedId);
                        if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) converter.markDiscontinuity();
                        const auto publish = [this](const Audio::PcmChunk &chunk) { impl_->chunks.push(chunk); };
                        if (silent) converter.appendSilence(frames, publish);
                        else converter.append(packet, publish);
                        checked(capture->GetNextPacketSize(&frames), "GetNextPacketSize", selectedId);
                    }
                }
            } catch (const Audio::Error &e) {
                std::lock_guard<std::mutex> lock(impl_->mutex); impl_->error = e;
            } catch (const std::exception &e) {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->error = {Audio::ErrorCode::BackendFailure, QStringLiteral("System audio worker failed."), QString::fromUtf8(e.what())};
            }
            impl_->active = false;
        });
    } catch (const std::exception &e) {
        CloseHandle(impl_->stopEvent); impl_->stopEvent = nullptr;
        emit errorOccurred({Audio::ErrorCode::BackendFailure, QStringLiteral("Unable to start system audio worker."), QString::fromUtf8(e.what())});
        return;
    }
    impl_->delivery.start();
#endif
}
void WindowsLoopbackAudioInput::drain()
{
    Audio::Error error; bool ready = false; QByteArray id;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        error = impl_->error; impl_->error = {}; ready = impl_->ready; impl_->ready = false; id = impl_->selectedId;
    }
    if (error.code != Audio::ErrorCode::None) {
        // Fatal termination must not enqueue Stopped before Error: the coordinator
        // would otherwise retire the session before it can retain this diagnostic.
        { QSignalBlocker blocker(this); stop(); }
        qCWarning(audioLog).noquote() << error.message << error.detail;
        emit errorOccurred(error); return;
    }
    if (ready) {
        qCInfo(audioLog).noquote() << "WASAPI loopback started" << QString::fromUtf8(id)
            << Audio::describe(nativeFormat()) << "-> 16000 Hz / mono / int16 LE";
        emit started();
        return; // Let the queued coordinator Started transition precede PCM delivery.
    }
    for (const auto &chunk : impl_->chunks.take()) if (impl_->active) emit pcmReady(chunk);
}
void WindowsLoopbackAudioInput::stop()
{
    impl_->delivery.stop(); impl_->stopping = true;
    const bool hadWorker = impl_->worker.joinable();
#ifdef Q_OS_WIN
    if (impl_->stopEvent) SetEvent(impl_->stopEvent);
#endif
    if (hadWorker) impl_->worker.join();
#ifdef Q_OS_WIN
    if (impl_->stopEvent) { CloseHandle(impl_->stopEvent); impl_->stopEvent = nullptr; }
#endif
    impl_->active = false;
    impl_->chunks.clear();
    if (hadWorker) { qCInfo(audioLog) << "WASAPI loopback stopped"; emit stopped(); }
}
