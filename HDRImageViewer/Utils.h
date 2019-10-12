#pragma once
#include "pch.h"

namespace Utils
{
	static float scRGBtoMessiahRGB(float c)
	{
		float luminance = max(c, 0.0) * 80.0f;
		float start = 0.0f, end = 8.0f;
		while ((end - start) > 0.001f)
		{
			float x = (start + end) / 2;
			float y = (200.276765 * x + 1.379809) * x / ((0.039350 * x + 0.958296) * x + 0.171154);
			if (y < luminance)
				start = x;
			else
				end = x;
		}
		return start;
	}

	static float saturate(float x)
	{
		return min(max(x, 0.0f), 1.0f);
	}

	static void EncodeRGMB(DirectX::XMFLOAT4& color)
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

	static float HeatmapToLuminance(DirectX::XMFLOAT4& color)
	{
		constexpr int NumStops = 9;
		DirectX::XMFLOAT4 stops_colors[NumStops] = {
			{0.0f, 0.0f, 0.0f, 1.0f},
			{0.0f, 0.0f, 1.0f, 1.0f},
			{0.0f, 1.0f, 1.0f, 1.0f},
			{0.0f, 1.0f, 0.0f, 1.0f},
			{1.0f, 1.0f, 0.0f, 1.0f},
			{1.0f, 0.2f, 0.0f, 1.0f},
			{1.0f, 0.0f, 0.0f, 1.0f},
			{1.0f, 0.0f, 1.0f, 1.0f},
			{1.0f, 1.0f, 1.0f, 1.0f},
		};

		float stops_nits[NumStops] = {
			0.00f,
			3.16f,
			10.0f,
			31.6f,
			100.f,
			316.f,
			1000.f,
			3160.f,
			10000.f,
		};

		constexpr int N = 1000;
		float luminance = 0;
		float minError = 100000.0;
		DirectX::XMVECTOR vc = DirectX::XMLoadFloat4(&color);
		vc = DirectX::XMVectorDivide(vc, DirectX::XMVectorSet(2.0f, 2.0f, 2.0f, 1.0f));

		for (int i = 0; i < NumStops - 1; ++i)
		{
			DirectX::XMFLOAT4& a = stops_colors[i];
			DirectX::XMFLOAT4& b = stops_colors[i + 1];
			DirectX::XMVECTOR va = DirectX::XMLoadFloat4(&a);
			DirectX::XMVECTOR vb = DirectX::XMLoadFloat4(&b);

			float la = stops_nits[i];
			float lb = stops_nits[i + 1];
			
			for (int j = 0; j <= N; ++j)
			{
				float s = static_cast<float>(j) / N;
				DirectX::XMVECTOR vt = DirectX::XMVectorLerp(va, vb, s);
				DirectX::XMVECTOR dd = DirectX::XMVectorSubtract(vc, vt);
				DirectX::XMVECTOR len = DirectX::XMVector4Length(dd);
				float error = DirectX::XMVectorGetX(len);
				if (error < minError)
				{
					minError = error;
					luminance = la + (lb - la) * s;
				}
			}
		}
		return luminance;
	}

	static float half_to_float(uint16 Value) noexcept
	{
		uint32 Mantissa = 0;
		uint32 Exponent = 0;
		uint32 Result = 0;

		Mantissa = (uint32)(Value & 0x03FF);

		if ((Value & 0x7C00) != 0)  // The value is normalized
		{
			Exponent = (uint32)((Value >> 10) & 0x1F);
		}
		else if (Mantissa != 0)     // The value is denormalized
		{
			// Normalize the value in the resulting float
			Exponent = 1;
			do
			{
				Exponent--;
				Mantissa <<= 1;
			} while ((Mantissa & 0x0400) == 0);

			Mantissa &= 0x03FF;
		}
		else                        // The value is zero
		{
			Exponent = (uint32)-112;
		}

		Result = ((Value & 0x8000) << 16) | // Sign
			((Exponent + 112) << 23) | // Exponent
			(Mantissa << 13);          // Mantissa

		union
		{
			float f;
			uint32 i = 0;
		}s;
		s.i = Result;

		return s.f;
	}
}