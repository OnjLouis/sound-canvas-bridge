#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Midi2.h>
#include <winrt/Windows.Devices.Midi2.Enumeration.h>
#include <winrt/Windows.Devices.Midi2.Transports.Virtual.h>
#include "VirtualMidiProtocol.h"

using namespace winrt::Windows::Devices::Midi2;
using namespace winrt::Windows::Devices::Midi2::Enumeration;
using namespace winrt::Windows::Devices::Midi2::Transports::Virtual;

int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    auto handle=[](const wchar_t* text) { return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::wcstoull(text,nullptr,10))); };
    const auto pipe=handle(argv[1]),stop=handle(argv[2]);
    std::mutex writeMutex;
    auto send=[&](const scbridge::VirtualMidiFrame& frame) {
        const std::lock_guard<std::mutex> guard(writeMutex);
        DWORD written=0;
        return WriteFile(pipe,&frame,sizeof(frame),&written,nullptr) && written==sizeof(frame);
    };
    auto progress=[&](uint32_t step) { scbridge::VirtualMidiFrame frame{};frame.data[0]=2;frame.data[1]=step;send(frame); };
    try {
        progress(1);
        winrt::init_apartment();
        progress(2);
        if(!MidiApi::EnsureServiceAvailable()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE));
        progress(3);
        auto session=MidiSession::Create(L"Sound Canvas Bridge");
        if(!session) throw winrt::hresult_error(E_FAIL);
        MidiDeclaredEndpointInfo info;
        info.Name(L"Sound Canvas Bridge");
        info.ProductInstanceId(L"OnjMusic-SoundCanvasBridge-"+std::to_wstring(GetCurrentProcessId()));
        info.HasStaticFunctionBlocks(true);info.SupportsMidi10Protocol(true);info.SupportsMidi20Protocol(false);
        info.SupportsReceivingJitterReductionTimestamps(false);info.SupportsSendingJitterReductionTimestamps(false);
        info.SpecificationVersionMajor(1);info.SpecificationVersionMinor(1);
        MidiVirtualDeviceCreationConfig config(info.Name(),L"Sound Canvas Bridge MIDI input",L"OnjMusic",info);
        MidiFunctionBlock block;
        block.Number(0);block.IsActive(true);block.Name(L"MIDI");block.FirstGroup(MidiGroup(uint8_t(0)));
        block.GroupCount(1);block.Direction(MidiFunctionBlockDirection::BlockInput);
        block.RepresentsMidi10Connection(MidiFunctionBlockRepresentsMidi10Connection::YesBandwidthUnrestricted);
        config.FunctionBlocks().Append(block);
        progress(4);
        auto device=MidiVirtualDeviceManager::CreateVirtualDevice(config);
        progress(5);
        if(!device) throw winrt::hresult_error(E_FAIL);
        auto connection=session.CreateEndpointConnection(device.DeviceEndpointDeviceId());
        progress(6);
        if(!connection) throw winrt::hresult_error(E_FAIL);
        auto token=connection.MessageReceived([&](auto const&,MidiMessageReceivedEventArgs const& args) {
            scbridge::VirtualMidiFrame frame{};
            frame.words=args.FillWordArray(0,frame.data);
            if(frame.words && frame.words<=4) send(frame);
        });
        if(connection.AddMessageProcessingPlugin(device)!=MidiMessageProcessingPluginAddResult::Succeeded || !connection.Open())
            throw winrt::hresult_error(E_FAIL);
        progress(7);
        scbridge::VirtualMidiFrame ready{};ready.data[0]=1;
        if(!send(ready)) return 5;
        WaitForSingleObject(stop,INFINITE);
        progress(8);
        connection.MessageReceived(token);
        progress(9);
        session.DisconnectEndpointConnection(connection.ConnectionId());
        progress(10);
        session.Close();progress(11);device=nullptr;progress(12);
        return 0;
    } catch(winrt::hresult_error const& error) {
        scbridge::VirtualMidiFrame frame{};frame.data[1]=static_cast<uint32_t>(error.code().value);
        send(frame);return 10;
    } catch(...) {
        scbridge::VirtualMidiFrame frame{};frame.data[1]=static_cast<uint32_t>(E_FAIL);
        send(frame);return 11;
    }
}
