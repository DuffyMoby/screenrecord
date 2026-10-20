#include <windows.h>

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <iostream>

#include "shared/CaptureSettings.h"
#include "shared/buffer.h"

using Microsoft::WRL::ComPtr;

namespace {

// Prints the failing step and the HRESULT
bool failed(HRESULT result, const char* step) {
    if (SUCCEEDED(result)) {
        return false;
    }
    std::cerr << step << " failed: 0x" << std::hex << result << std::dec
              << '\n';
    return true;
}

}  // namespace

struct FrameConverter {
    ComPtr<ID3D11VideoDevice> device;
    ComPtr<ID3D11VideoContext1> context;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    ComPtr<ID3D11VideoProcessorInputView> input_view;
};

// Creates the D3D11 device.
ComPtr<ID3D11Device> create_device() {
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device> device;
    HRESULT result = D3D11CreateDevice(nullptr,  // pAdapter: default adapter
                                       D3D_DRIVER_TYPE_HARDWARE,  // DriverType
                                       nullptr,                   // Software
                                       flags,                     // Flags
                                       nullptr,  // pFeatureLevels: default list
                                       0,        // FeatureLevels
                                       D3D11_SDK_VERSION,  // SDKVersion
                                       &device,            // ppDevice
                                       nullptr,  // pFeatureLevel: don't care
                                       nullptr   // ppImmediateContext: not yet
    );

    if (failed(result, "D3D11CreateDevice")) {
        return {};
    }
    return device;
}

// Walks device -> DXGI device -> adapter -> output -> output1 -> duplication.
// `output_index` picks the monitor; 0 is the primary. Empty on failure.
ComPtr<IDXGIOutputDuplication> get_duplication(ID3D11Device* device,
                                               UINT output_index) {
    ComPtr<IDXGIDevice> dxgi_device;
    if (failed(device->QueryInterface(IID_PPV_ARGS(&dxgi_device)),
               "QueryInterface IDXGIDevice")) {
        return {};
    }

    ComPtr<IDXGIAdapter> adapter;
    if (failed(dxgi_device->GetAdapter(&adapter), "GetAdapter")) {
        return {};
    }

    ComPtr<IDXGIOutput> output;
    if (failed(adapter->EnumOutputs(output_index, &output), "EnumOutputs")) {
        return {};
    }

    ComPtr<IDXGIOutput1> output1;
    if (failed(output.As(&output1), "QueryInterface IDXGIOutput1")) {
        return {};
    }

    ComPtr<IDXGIOutputDuplication> duplication;
    if (failed(output1->DuplicateOutput(device, &duplication),
               "DuplicateOutput")) {
        return {};
    }

    return duplication;
}

// Waits up to `timeout_ms` for the next frame from the duplication, copies it
// to `out_frame`, and releases the frame. False if no new frame arrived, in
// which case `out_frame` keeps its last image.
bool get_raw_frame(IDXGIOutputDuplication* duplication,
                   ID3D11DeviceContext* context, ID3D11Texture2D* out_frame,
                   UINT timeout_ms) {
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    ComPtr<IDXGIResource> res;

    HRESULT result =
        duplication->AcquireNextFrame(timeout_ms, &frame_info, &res);
    if (result == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;  // screen didn't change
    }
    if (failed(result, "AcquireNextFrame")) {
        return false;
    }

    ComPtr<ID3D11Texture2D> tex;
    if (failed(res.As(&tex), "QueryInterface ID3D11Texture2D")) {
        duplication->ReleaseFrame();
        return false;
    }

    context->CopyResource(out_frame, tex.Get());

    duplication->ReleaseFrame();
    return true;
}

// Builds the video processor that scales and converts `in` to `out`.
// Empty on failure.
FrameConverter create_converter(ID3D11Device* device,
                                ID3D11DeviceContext* context, FrameFormat in,
                                FrameFormat out, FrameRate rate) {
    FrameConverter converter;

    if (failed(device->QueryInterface(IID_PPV_ARGS(&converter.device)),
               "QueryInterface ID3D11VideoDevice")) {
        return {};
    }

    if (failed(context->QueryInterface(IID_PPV_ARGS(&converter.context)),
               "QueryInterface ID3D11VideoContext1")) {
        return {};
    }

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputWidth = in.width;
    desc.InputHeight = in.height;
    desc.InputFrameRate = {.Numerator=rate.numerator, .Denominator=rate.denominator};
    desc.OutputWidth = out.width;
    desc.OutputHeight = out.height;
    desc.OutputFrameRate = {.Numerator=rate.numerator, .Denominator=rate.denominator};
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    if (failed(converter.device->CreateVideoProcessorEnumerator(
                   &desc, &converter.enumerator),
               "CreateVideoProcessorEnumerator")) {
        return {};
    }

    UINT support = 0;
    if (failed(converter.enumerator->CheckVideoProcessorFormat(out.format,
                                                              &support),
               "CheckVideoProcessorFormat")) {
        return {};
    }
    if ((support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
        std::cerr << "output format not supported by the video processor\n";
        return {};
    }

    if (failed(converter.device->CreateVideoProcessor(
                   converter.enumerator.Get(), 0, &converter.processor),
               "CreateVideoProcessor")) {
        return {};
    }

    converter.context->VideoProcessorSetStreamColorSpace1(
        converter.processor.Get(), 0, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    converter.context->VideoProcessorSetOutputColorSpace1(
        converter.processor.Get(),
        DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);

    return converter;
}

// Converts the frame behind `converter.input_view` into the texture behind
// `out_view`. False on failure.
bool transform_frame(const FrameConverter& converter,
                     ID3D11VideoProcessorOutputView* out_view) {
    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = TRUE;
    stream.pInputSurface = converter.input_view.Get();

    return !failed(converter.context->VideoProcessorBlt(
                       converter.processor.Get(), out_view, 0, 1, &stream),
                   "VideoProcessorBlt");
}

void capture_loop(Buffer& buffer, CaptureSettings settings) {
    ComPtr<ID3D11Device> device = create_device();
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    ComPtr<IDXGIOutputDuplication> duplication =
        get_duplication(device.Get(), settings.monitor_index);

    DXGI_OUTDUPL_DESC duplication_desc;
    duplication->GetDesc(&duplication_desc);
    FrameFormat source = {.width=duplication_desc.ModeDesc.Width,
                          .height=duplication_desc.ModeDesc.Height,
                          .format=duplication_desc.ModeDesc.Format};

    FrameConverter converter =
        create_converter(device.Get(), context.Get(), source,
                         settings.frame_settings, settings.frame_rate);

    D3D11_TEXTURE2D_DESC raw_desc = {};
    raw_desc.Width = source.width;
    raw_desc.Height = source.height;
    raw_desc.MipLevels = 1;
    raw_desc.ArraySize = 1;
    raw_desc.Format = source.format;
    raw_desc.SampleDesc.Count = 1;
    raw_desc.Usage = D3D11_USAGE_DEFAULT;
    raw_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> raw_frame;
    if (failed(device->CreateTexture2D(&raw_desc, nullptr, &raw_frame),
               "CreateTexture2D raw_frame")) {
        return;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc = {};
    input_desc.FourCC = 0;
    input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;

    if (failed(converter.device->CreateVideoProcessorInputView(
                   raw_frame.Get(), converter.enumerator.Get(), &input_desc,
                   &converter.input_view),
               "CreateVideoProcessorInputView")) {
        return;
    }

    // One frame interval, so that we don't spin if nothing on screen has changed.
    const UINT timeout_ms =
        1000 * settings.frame_rate.denominator / settings.frame_rate.numerator;

    for (;;) {
        // On a timeout raw_frame still holds the last image, so it gets
        // converted again as a repeat frame.
        get_raw_frame(duplication.Get(), context.Get(), raw_frame.Get(),
                      timeout_ms);

        Frame& slot = buffer.acquire_write();
        // Commit even if this fails, otherwise the slot is never given back.
        transform_frame(converter, slot.view.Get());
        buffer.commit_write();
    }
}
