// Test for BC7Enc.dll: loads the built DLL the way the game does (the "encode" export), encodes a test picture in
// DXT1, DXT5 and BC7 with its whole mip chain, decodes every block of the top level back (rgbcx / bc7decomp) and
// prints the error per format. Also checks the bytes written = the game's chain size (BC7Enc.ChainBlocks x block size),
// a 1 x 1 picture, an odd size and the refusals (bad format, empty buffers).
// Build + run: build.cmd test

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"
#include "bc7decomp.h"

typedef int(__cdecl* EncodeFn)(const uint8_t*, int, int, uint8_t*, int, int, int, int);

static int ChainBlocks(int width, int height)
{
	int blocks = 0;
	for (int level = 0; level < 31; level++)
	{
		blocks += (width + 3) / 4 * ((height + 3) / 4);
		if (width == 1 && height == 1)
		{
			break;
		}
		width = width > 1 ? width >> 1 : 1;
		height = height > 1 ? height >> 1 : 1;
	}
	return blocks;
}

// a picture with smooth gradients, hard edges and a soft alpha ramp
static std::vector<uint8_t> TestPicture(int width, int height)
{
	std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			uint8_t* p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
			p[0] = static_cast<uint8_t>(x * 255 / (width > 1 ? width - 1 : 1));
			p[1] = static_cast<uint8_t>(y * 255 / (height > 1 ? height - 1 : 1));
			p[2] = static_cast<uint8_t>(((x / 16 + y / 16) % 2) ? 220 : 30);
			p[3] = static_cast<uint8_t>((x + y) * 255 / (width + height));
		}
	}
	return pixels;
}

// PSNR of the decoded top level (alpha counted for DXT5 / BC7 only)
static double TopLevelPsnr(const std::vector<uint8_t>& source, const uint8_t* encoded, int width, int height, int format)
{
	const int blockBytes = format == 10 ? 8 : 16;
	const int channels = format == 10 ? 3 : 4;
	double squared = 0;
	long count = 0;
	for (int by = 0; by < (height + 3) / 4; by++)
	{
		for (int bx = 0; bx < (width + 3) / 4; bx++)
		{
			const uint8_t* block = encoded + (static_cast<size_t>(by) * ((width + 3) / 4) + bx) * blockBytes;
			uint8_t decoded[64];
			if (format == 10)
			{
				rgbcx::unpack_bc1(block, decoded, true);
			}
			else if (format == 12)
			{
				rgbcx::unpack_bc3(block, decoded);
			}
			else
			{
				bc7decomp::unpack_bc7(block, reinterpret_cast<bc7decomp::color_rgba*>(decoded));
			}
			for (int y = 0; y < 4; y++)
			{
				for (int x = 0; x < 4; x++)
				{
					int px = bx * 4 + x;
					int py = by * 4 + y;
					if (px >= width || py >= height)
					{
						continue;
					}
					for (int c = 0; c < channels; c++)
					{
						double d = static_cast<double>(decoded[(y * 4 + x) * 4 + c]) - source[(static_cast<size_t>(py) * width + px) * 4 + c];
						squared += d * d;
						count++;
					}
				}
			}
		}
	}
	double mse = squared / count;
	return mse <= 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

int main(int argc, char** argv)
{
	const char* dllPath = argc > 1 ? argv[1] : "BC7Enc.dll";
	HMODULE dll = LoadLibraryA(dllPath);
	if (!dll)
	{
		std::printf("FAIL: cannot load %s\n", dllPath);
		return 1;
	}
	EncodeFn encode = reinterpret_cast<EncodeFn>(GetProcAddress(dll, "encode"));
	if (!encode)
	{
		std::printf("FAIL: no 'encode' export\n");
		return 1;
	}
	int failures = 0;
	struct Case { const char* name; int format; int level; double minPsnr; };
	const Case cases[] = { { "DXT1 level 3", 10, 3, 30.0 }, { "DXT1 level 7", 10, 7, 30.0 },
		{ "DXT5 level 3", 12, 3, 30.0 }, { "DXT5 level 7", 12, 7, 30.0 }, { "BC7 level 3", 25, 3, 35.0 } };
	const int sizes[][2] = { { 256, 128 }, { 37, 21 }, { 1, 1 } };
	for (const auto& size : sizes)
	{
		int width = size[0];
		int height = size[1];
		std::vector<uint8_t> picture = TestPicture(width, height);
		for (const Case& test : cases)
		{
			int blockBytes = test.format == 10 ? 8 : 16;
			bool mips = width > 1 || height > 1;
			std::vector<uint8_t> out(static_cast<size_t>(ChainBlocks(width, height)) * blockBytes + 64, 0xCD);
			DWORD start = GetTickCount();
			int written = encode(picture.data(), width, height, out.data(), test.format, test.level, mips ? 1 : 0, 1);
			DWORD ms = GetTickCount() - start;
			int expected = ChainBlocks(width, height) * blockBytes;
			bool sizeOk = written == expected;
			// the bytes past the chain must be untouched
			bool guardOk = true;
			for (size_t i = static_cast<size_t>(expected); i < out.size(); i++)
			{
				guardOk = guardOk && out[i] == 0xCD;
			}
			double psnr = sizeOk ? TopLevelPsnr(picture, out.data(), width, height, test.format) : 0;
			bool ok = sizeOk && guardOk && psnr >= test.minPsnr;
			failures += ok ? 0 : 1;
			std::printf("%s  %3dx%-3d %-13s written %7d (chain %7d) guard %s  top-level PSNR %5.1f dB  %lu ms\n",
				ok ? "ok  " : "FAIL", width, height, test.name, written, expected, guardOk ? "ok" : "HIT", psnr, ms);
		}
	}
	uint8_t pixel[4] = { 1, 2, 3, 4 };
	uint8_t sink[16];
	bool refusals = encode(pixel, 1, 1, sink, 4, 3, 0, 1) == 0 && encode(nullptr, 1, 1, sink, 10, 3, 0, 1) == 0
		&& encode(pixel, 1, 1, nullptr, 10, 3, 0, 1) == 0 && encode(pixel, 0, 1, sink, 10, 3, 0, 1) == 0;
	failures += refusals ? 0 : 1;
	std::printf("%s  refusals (unknown format, null buffers, zero size) answer 0\n", refusals ? "ok  " : "FAIL");
	if (failures == 0)
	{
		std::printf("ALL OK\n");
	}
	else
	{
		std::printf("%d FAILED\n", failures);
	}
	return failures == 0 ? 0 : 1;
}
