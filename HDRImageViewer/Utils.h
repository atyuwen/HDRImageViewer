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

}