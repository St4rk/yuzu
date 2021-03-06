// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/core_timing.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/event.h"
#include "core/hle/service/audio/audout_u.h"

namespace Service {
namespace Audio {

// switch sample rate frequency
constexpr u32 sample_rate = 48000;
// TODO(st4rk): dynamic number of channels, as I think Switch has support
// to more audio channels (probably when Docked I guess)
constexpr u32 audio_channels = 2;
// TODO(st4rk): find a proper value for the audio_ticks
constexpr u64 audio_ticks = static_cast<u64>(BASE_CLOCK_RATE / 1000);

class IAudioOut final : public ServiceFramework<IAudioOut> {
public:
    IAudioOut() : ServiceFramework("IAudioOut") {
        static const FunctionInfo functions[] = {
            {0x0, nullptr, "GetAudioOutState"},
            {0x1, &IAudioOut::StartAudioOut, "StartAudioOut"},
            {0x2, &IAudioOut::StopAudioOut, "StopAudioOut"},
            {0x3, &IAudioOut::AppendAudioOutBuffer_1, "AppendAudioOutBuffer_1"},
            {0x4, &IAudioOut::RegisterBufferEvent, "RegisterBufferEvent"},
            {0x5, &IAudioOut::GetReleasedAudioOutBuffer_1, "GetReleasedAudioOutBuffer_1"},
            {0x6, nullptr, "ContainsAudioOutBuffer"},
            {0x7, nullptr, "AppendAudioOutBuffer_2"},
            {0x8, nullptr, "GetReleasedAudioOutBuffer_2"},
        };
        RegisterHandlers(functions);

        // This is the event handle used to check if the audio buffer was released
        buffer_event = Kernel::Event::Create(Kernel::ResetType::OneShot, "IAudioOut buffer released event handle");

        // Register event callback to update the Audio Buffer
        audio_event = CoreTiming::RegisterEvent(
            "IAudioOut::UpdateAudioBuffersCallback",
            [this](u64 userdata, int cycles_late) {
                UpdateAudioBuffersCallback();
                CoreTiming::ScheduleEvent(audio_ticks - cycles_late, audio_event);
            });

        // Start the audio event
        CoreTiming::ScheduleEvent(audio_ticks, audio_event);

        // start with the audio stopped
        audio_out_state = 1;
    }

    ~IAudioOut() = default;
private:

    void StartAudioOut(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) called");

        // start audio
        audio_out_state = 0x0;

        IPC::RequestBuilder rb{ctx, 2, 0, 0, 0};
        rb.Push(RESULT_SUCCESS);
    }

    void StopAudioOut(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) called");

        // stop audio
        audio_out_state = 0x1;

        IPC::RequestBuilder rb{ctx, 2, 0, 0, 0};
        rb.Push(RESULT_SUCCESS);
    }

    void RegisterBufferEvent(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) called");

        IPC::RequestBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(buffer_event);
    }

    void AppendAudioOutBuffer_1(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) called");
        IPC::RequestParser rp{ctx};

        released_buffer = rp.Pop<u64>();

        IPC::RequestBuilder rb{ctx, 2, 0, 0, 0};
        rb.Push(RESULT_SUCCESS);
    }

    void GetReleasedAudioOutBuffer_1(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) called");

        const auto& buffer = ctx.BufferDescriptorB()[0];

        // TODO(st4rk): this is how libtransistor currently implements the
        // GetReleasedAudioOutBuffer, it should return the key (a VA) to
        // the APP and this address is used to know which buffer should
        // be filled with data and send again to the service through
        // AppendAudioOutBuffer. Check if this is the proper way to
        // do it.

        Memory::WriteBlock(buffer.Address(), &released_buffer, sizeof(u64));

        IPC::RequestBuilder rb{ctx, 3, 0, 0, 0};
        rb.Push(RESULT_SUCCESS);
        // This might be the total of released buffers
        rb.Push<u32>(0);
    }

    void UpdateAudioBuffersCallback() {
        if (!audio_out_state) {
            buffer_event->Signal();
        }
    }

    // This is used to trigger the audio event callback that is going
    // to read the samples from the audio_buffer list and enqueue the samples
    // using the sink (audio_core).
    CoreTiming::EventType* audio_event;

    // This is the evend handle used to check if the audio buffer was released
    Kernel::SharedPtr<Kernel::Event> buffer_event;

    // (st4rk): this is just a temporary workaround for the future implementation.
    // Libtransistor uses the key as an address in the App, so we need to return
    // when the GetReleasedAudioOutBuffer_1 is called, otherwise we'll run in
    // problems, because libtransistor uses the key returned as an pointer;
    u64 released_buffer;

    // current audio state: 0 is started and 1 is stopped
    u32 audio_out_state;
};

void AudOutU::ListAudioOuts(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::RequestParser rp{ctx};

    auto& buffer = ctx.BufferDescriptorB()[0];
    const std::string audio_interface = "AudioInterface";

    Memory::WriteBlock(buffer.Address(), &audio_interface[0], audio_interface.size());

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0, 0, 0);

    rb.Push(RESULT_SUCCESS);
    // TODO(st4rk): we're currently returning only one audio interface
    // (stringlist size)
    // however, it's highly possible to have more than one interface (despite that
    // libtransistor
    // requires only one).
    rb.Push<u32>(1);
}

void AudOutU::OpenAudioOut(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");

    if (audio_out_interface == nullptr) {
        audio_out_interface = std::make_shared<IAudioOut>();
    }

    auto sessions = Kernel::ServerSession::CreateSessionPair(audio_out_interface->GetServiceName());
    auto server = std::get<Kernel::SharedPtr<Kernel::ServerSession>>(sessions);
    auto client = std::get<Kernel::SharedPtr<Kernel::ClientSession>>(sessions);
    audio_out_interface->ClientConnected(server);
    LOG_DEBUG(Service, "called, initialized IAudioOut -> session=%u",
              client->GetObjectId());
    IPC::RequestBuilder rb{ctx, 6,0,1};

    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(sample_rate);
    rb.Push<u32>(audio_channels);
    rb.Push<u32>(PCM_FORMAT::INT16);
    // this field is unknown
    rb.Push<u32>(0);
    rb.PushMoveObjects(std::move(client));
}

AudOutU::AudOutU() : ServiceFramework("audout:u") {
    static const FunctionInfo functions[] = {{0x00000000, &AudOutU::ListAudioOuts, "ListAudioOuts"},
                                             {0x00000001, &AudOutU::OpenAudioOut, "OpenAudioOut"},
                                             {0x00000002, nullptr, "Unknown2"},
                                             {0x00000003, nullptr, "Unknown3"}};
    RegisterHandlers(functions);
}

} // namespace Audio
} // namespace Service
