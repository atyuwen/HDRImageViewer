//*********************************************************
//
// ImageExporter
//
// Saves image data in various DirectX formats to disk.
// Used for both app features and for testing.
//
//*********************************************************

#pragma once
#include "DeviceResources.h"
#include "ImageLoader.h"

namespace HDRImageViewer
{
    class ImageExporter
    {
    public:
        ImageExporter();
        ~ImageExporter();

        static void ExportToSdr(_In_ ImageLoader* loader, _In_ DX::DeviceResources* res, IStream* stream, GUID wicFormat);

		static void ExportToMessiah(_In_ ImageLoader* loader, _In_ DX::DeviceResources* res, IStream* stream, GUID wicFormat);

        static std::vector<DirectX::XMFLOAT4> DumpD2DTarget(_In_ DX::DeviceResources* res);

		static std::vector<DirectX::XMFLOAT4> DumpD2DImage(_In_ ID2D1Image* image, _In_ DX::DeviceResources* res, Windows::Foundation::Size size);

    private:
        static void ExportToWic(_In_ ID2D1Image* img, Windows::Foundation::Size size, _In_ DX::DeviceResources* res, IStream* stream, GUID wicFormat);
    };
}
