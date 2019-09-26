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