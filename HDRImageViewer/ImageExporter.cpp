#include "pch.h"
#include "DirectXHelper.h"
#include "ImageExporter.h"
#include "MagicConstants.h"
#include "SimpleTonemapEffect.h"

using namespace Microsoft::WRL;

using namespace HDRImageViewer;
using DX::CHK;

ImageExporter::ImageExporter()
{
    throw ref new Platform::NotImplementedException;
}

ImageExporter::~ImageExporter()
{
}

/// <summary>
/// Converts an HDR image to SDR using a pipeline equivalent to
/// RenderEffectKind::HdrTonemap. Not yet suitable for general purpose use.
/// </summary>
/// <param name="wicFormat">WIC container format GUID (GUID_ContainerFormat...)</param>
void ImageExporter::ExportToSdr(ImageLoader* loader, DX::DeviceResources* res, IStream* stream, GUID wicFormat)
{
    auto ctx = res->GetD2DDeviceContext();

    // Effect graph: ImageSource > ColorManagement  > HDRTonemap > WhiteScale
    // This graph is derived from, but not identical to RenderEffectKind::HdrTonemap.
    // TODO: Is there any way to keep this better in sync with the main render pipeline?

    ComPtr<ID2D1TransformedImageSource> source = loader->GetLoadedImage(1.0f);

    ComPtr<ID2D1Effect> colorManage;
    CHK(ctx->CreateEffect(CLSID_D2D1ColorManagement, &colorManage));
    colorManage->SetInput(0, source.Get());
    CHK(colorManage->SetValue(D2D1_COLORMANAGEMENT_PROP_QUALITY, D2D1_COLORMANAGEMENT_QUALITY_BEST));

    ComPtr<ID2D1ColorContext> sourceCtx = loader->GetImageColorContext();
    CHK(colorManage->SetValue(D2D1_COLORMANAGEMENT_PROP_SOURCE_COLOR_CONTEXT, sourceCtx.Get()));

    ComPtr<ID2D1ColorContext1> destCtx;
    // scRGB
    CHK(ctx->CreateColorContextFromDxgiColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &destCtx));
    CHK(colorManage->SetValue(D2D1_COLORMANAGEMENT_PROP_DESTINATION_COLOR_CONTEXT, destCtx.Get()));

    GUID tmGuid = {};
    if (DX::CheckPlatformSupport(DX::Win1809)) tmGuid = CLSID_D2D1HdrToneMap;
    else tmGuid = CLSID_CustomSimpleTonemapEffect;

    ComPtr<ID2D1Effect> tonemap;
    CHK(ctx->CreateEffect(tmGuid, &tonemap));
    tonemap->SetInputEffect(0, colorManage.Get());
    CHK(tonemap->SetValue(D2D1_HDRTONEMAP_PROP_OUTPUT_MAX_LUMINANCE, sc_DefaultSdrDispMaxNits));
    CHK(tonemap->SetValue(D2D1_HDRTONEMAP_PROP_DISPLAY_MODE, D2D1_HDRTONEMAP_DISPLAY_MODE_SDR));

    ComPtr<ID2D1Effect> whiteScale;
    CHK(ctx->CreateEffect(CLSID_D2D1ColorMatrix, &whiteScale));
    whiteScale->SetInputEffect(0, tonemap.Get());

    float scale = D2D1_SCENE_REFERRED_SDR_WHITE_LEVEL / sc_DefaultSdrDispMaxNits;
    D2D1_MATRIX_5X4_F matrix = D2D1::Matrix5x4F(
        scale, 0, 0, 0,  // [R] Multiply each color channel
        0, scale, 0, 0,  // [G] by the scale factor in 
        0, 0, scale, 0,  // [B] linear gamma space.
        0, 0, 0, 1,      // [A] Preserve alpha values.
        0, 0, 0, 0);     //     No offset.

    CHK(whiteScale->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix));

    ComPtr<ID2D1Image> d2dImage;
    whiteScale->GetOutput(&d2dImage);

    ImageExporter::ExportToWic(d2dImage.Get(), loader->GetImageInfo().size, res, stream, wicFormat);
}

float scRGBtoMessiahRGB(float c)
{
	float luminance = max(c, 0.0) * 80.0f;
	float start = 0.0f, end = 8.0f;
	while ((end - start) > 0.001f)
	{
		float x = (start + end) / 2;
		float y = (200.276765 * x + 1.379809)* x / ((0.039350 * x + 0.958296) * x + 0.171154);
		if (y < luminance)
			start = x;
		else
			end = x;
	}
	return start;
}

float saturate(float x)
{
	return min(max(x, 0.0f), 1.0f);
}

void EncodeRGMB(DirectX::XMFLOAT4& color)
{
	float x = color.x / 8.0f;
	float y = color.y / 8.0f;
	float z = color.z / 8.0f;
	float a = max(x, max(y, max(z, 1.1f / 65025)));
	a = sqrt(saturate(a));
	a = ceil(a * 255.0) / 255.0;
	float s = 1.0f / (a * a);
	color.x = x * s;
	color.y = y * s;
	color.z = z * s;
	color.w = a;
}

void ImageExporter::ExportToMessiah(_In_ ImageLoader* loader, _In_ DX::DeviceResources* res, IStream* stream, GUID wicFormat)
{
	auto size = loader->GetImageInfo().size;
	std::vector<DirectX::XMFLOAT4> data = DumpD2DImage(loader->GetLoadedImage(1.0f), res, size);
	std::vector<byte> outbuffer(size.Width * size.Height * 4);
	
	for (int i = 0; i < size.Height; ++i)
	{
		for (int j = 0; j < size.Width; ++j)
		{
			int index = i * size.Width + j;
			auto color = data[index];

			// Save to SDR
			//color.x = pow(saturate(color.x / 4), 0.45);
			//color.y = pow(saturate(color.y / 4), 0.45);
			//color.z = pow(saturate(color.z / 4), 0.45);

			// Save to Messiah RGMB
			color.x = scRGBtoMessiahRGB(color.x);
			color.y = scRGBtoMessiahRGB(color.y);
			color.z = scRGBtoMessiahRGB(color.z);
			EncodeRGMB(color);

			outbuffer[index * 4 + 0] = color.z * 255;
			outbuffer[index * 4 + 1] = color.y * 255;
			outbuffer[index * 4 + 2] = color.x * 255;
			outbuffer[index * 4 + 3] = color.w * 255;
		}
	}

	auto dev = res->GetD2DDevice();
	auto wic = res->GetWicImagingFactory();

	ComPtr<IWICBitmapEncoder> encoder;
	CHK(wic->CreateEncoder(wicFormat, nullptr, &encoder));
	CHK(encoder->Initialize(stream, WICBitmapEncoderNoCache));

	ComPtr<IWICBitmapFrameEncode> frame;
	CHK(encoder->CreateNewFrame(&frame, nullptr));
	CHK(frame->Initialize(nullptr));

	auto outformat = GUID_WICPixelFormat32bppRGBA;
	frame->SetPixelFormat(&outformat);
	frame->SetSize(size.Width, size.Height);
	frame->WritePixels(size.Height, size.Width * 4, outbuffer.size(), outbuffer.data());

	CHK(frame->Commit());
	CHK(encoder->Commit());
	CHK(stream->Commit(STGC_DEFAULT));
}

/// <summary>
/// Copies D2D target bitmap (typically same as swap chain) data into CPU accessible memory. Primarily for debug/test purposes.
/// </summary>
/// <remarks>
/// For simplicity, relies on IWICImageEncoder to convert to FP16. Caller should get pixel dimensions
/// from the target bitmap.
/// </remarks>
std::vector<DirectX::XMFLOAT4> ImageExporter::DumpD2DTarget(DX::DeviceResources* res)
{
    auto wic = res->GetWicImagingFactory();

    auto ras = ref new Windows::Storage::Streams::InMemoryRandomAccessStream();
    ComPtr<IStream> stream;
    CHK(CreateStreamOverRandomAccessStream(ras, IID_PPV_ARGS(&stream)));

    auto d2dBitmap = res->GetD2DTargetBitmap();
    auto d2dSize = d2dBitmap->GetPixelSize();
    auto size = Windows::Foundation::Size(static_cast<float>(d2dSize.width), static_cast<float>(d2dSize.height));

    ExportToWic(d2dBitmap, size, res, stream.Get(), GUID_ContainerFormatWmp);

    // WIC decoders require stream to be at position 0.
    LARGE_INTEGER zero = {};
    ULARGE_INTEGER ignore = {};
    CHK(stream->Seek(zero, 0, &ignore));

    ComPtr<IWICBitmapDecoder> decode;
    CHK(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decode));
    
    ComPtr<IWICBitmapFrameDecode> frame;
    CHK(decode->GetFrame(0, &frame));
    GUID fmt = {};
    CHK(frame->GetPixelFormat(&fmt));
    CHK(fmt == GUID_WICPixelFormat64bppRGBAHalf ? S_OK : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT); // FP16

    auto width = static_cast<uint32_t>(size.Width);
    auto height = static_cast<uint32_t>(size.Height);

    std::vector<DirectX::XMFLOAT4> pixels = std::vector<DirectX::XMFLOAT4>(width * height);
    CHK(frame->CopyPixels(
        nullptr,                                                            // Rect
        width * sizeof(DirectX::XMFLOAT4),                                  // Stride (bytes)
        static_cast<uint32_t>(pixels.size() * sizeof(DirectX::XMFLOAT4)),   // Total size (bytes)
        reinterpret_cast<byte *>(pixels.data())));                          // Buffer

    return pixels;
}

std::vector<DirectX::XMFLOAT4> HDRImageViewer::ImageExporter::DumpD2DImage(_In_ ID2D1Image* image, _In_ DX::DeviceResources* res, Windows::Foundation::Size size)
{
	auto ras = ref new Windows::Storage::Streams::InMemoryRandomAccessStream();
	ComPtr<IStream> stream;
	CHK(CreateStreamOverRandomAccessStream(ras, IID_PPV_ARGS(&stream)));
	ExportToWic(image, size, res, stream.Get(), GUID_ContainerFormatWmp);

	// WIC decoders require stream to be at position 0.
	LARGE_INTEGER zero = {};
	ULARGE_INTEGER ignore = {};
	CHK(stream->Seek(zero, 0, &ignore));

	auto wic = res->GetWicImagingFactory();
	ComPtr<IWICBitmapDecoder> decode;
	CHK(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decode));

	ComPtr<IWICBitmapFrameDecode> frame;
	CHK(decode->GetFrame(0, &frame));
	GUID fmt = {};
	CHK(frame->GetPixelFormat(&fmt));
	uint32 w, h;
	frame->GetSize(&w, &h);
	CHK(fmt == GUID_WICPixelFormat128bppRGBAFloat ? S_OK : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT); // FP16

	auto width = static_cast<uint32_t>(size.Width);
	auto height = static_cast<uint32_t>(size.Height);

	std::vector<DirectX::XMFLOAT4> pixels = std::vector<DirectX::XMFLOAT4>(width * height);
	CHK(frame->CopyPixels(
		nullptr,                                                            // Rect
		width * sizeof(DirectX::XMFLOAT4),                                  // Stride (bytes)
		static_cast<uint32_t>(pixels.size() * sizeof(DirectX::XMFLOAT4)),   // Total size (bytes)
		reinterpret_cast<byte*>(pixels.data())));                           // Buffer

	return pixels;
}

/// <summary>
/// Encodes to WIC using default encode options.
/// </summary>
/// <remarks>
/// First converts to FP16 in D2D, then uses the WIC encoder's internal converter.
/// </remarks>
/// <param name="wicFormat">The WIC container format to encode to.</param>
void ImageExporter::ExportToWic(ID2D1Image* img, Windows::Foundation::Size size, DX::DeviceResources* res, IStream* stream, GUID wicFormat)
{
    auto dev = res->GetD2DDevice();
    auto wic = res->GetWicImagingFactory();

    ComPtr<IWICBitmapEncoder> encoder;
    CHK(wic->CreateEncoder(wicFormat, nullptr, &encoder));
    CHK(encoder->Initialize(stream, WICBitmapEncoderNoCache));

    ComPtr<IWICBitmapFrameEncode> frame;
    CHK(encoder->CreateNewFrame(&frame, nullptr));
    CHK(frame->Initialize(nullptr));

    // IWICImageEncoder's internal pixel format conversion from float to uint does not perform gamma correction.
    // For simplicity, rely on the IWICBitmapFrameEncode's format converter which does perform gamma correction.
    WICImageParameters params = {
        D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,                             // DpiX
        96.0f,                             // DpiY
        0,                                 // OffsetX
        0,                                 // OffsetY
        static_cast<uint32_t>(size.Width), // SizeX
        static_cast<uint32_t>(size.Height) // SizeY
    };

	if (wicFormat == GUID_ContainerFormatWmp)
	{
		auto wicPixelFormat = GUID_WICPixelFormat128bppRGBAFloat;
		frame->SetPixelFormat(&wicPixelFormat);
	}

    ComPtr<IWICImageEncoder> imageEncoder;
    CHK(wic->CreateImageEncoder(dev, &imageEncoder));
    CHK(imageEncoder->WriteFrame(img, frame.Get(), &params));
    CHK(frame->Commit());
    CHK(encoder->Commit());
    CHK(stream->Commit(STGC_DEFAULT));
}
