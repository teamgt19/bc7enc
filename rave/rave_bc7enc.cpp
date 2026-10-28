// BC7Enc.dll - the block encoder behind RAVE's texture cache (BC7Enc.cs).
//
// The game reads a render texture back as RGBA32 and hands it to encode(): I build the mip chain (a 2 x 2 box filter
// down to 1 x 1) and compress every 4 x 4 block of every level into one buffer, level after level, the layout the
// game's .dds cache files and Texture2D.LoadRawTextureData expect.
//   DXT1 (BC1) and DXT5 (BC3): rgbcx
//   BC7: bc7enc (modes 1 / 6, perceptual weights - the cached pictures are colour pictures)
// Both encoders are by Rich Geldreich (bc7enc / rgbcx, https://github.com/richgel999/bc7enc), MIT licence or public
// domain - the licence text is in bc7enc/LICENSE next to this file.
//
// The export keeps the call the game has always made, so BC7Enc.cs and every caller stay as they are:
//   int encode(src, width, height, out, format, level, genMips, threads)  ->  bytes written (0 = nothing done)
// format = Unity's TextureFormat number, level = the game's quality number (0 - 18), genMips = a Windows BOOL.
//
// Build: RaveNative/BC7Enc/build.cmd (MSYS2 mingw64 g++, everything linked in - no runtime DLLs needed).

// (rgbcx.h uses memcpy / fabs without including their headers itself)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"
#include "bc7enc.h"

namespace
{
	// Unity's TextureFormat numbers
	const int FormatDXT1 = 10;
	const int FormatDXT5 = 12;
	const int FormatBC7 = 25;

	// the game's chain is at most 31 levels (BC7Enc.ChainBlocks)
	const int MaxLevels = 31;

	std::once_flag tablesReady;

	// both encoders build their lookup tables once, before the first block
	void PrepareTables()
	{
		std::call_once(tablesReady, []
		{
			rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
			bc7enc_compress_block_init();
		});
	}

	// one 4 x 4 block of RGBA pixels from a level, edge pixels repeated where the block runs past it
	void TakeBlock(const uint8_t* pixels, int width, int height, int blockX, int blockY, uint8_t* block)
	{
		for (int y = 0; y < 4; y++)
		{
			int row = std::min(blockY * 4 + y, height - 1);
			for (int x = 0; x < 4; x++)
			{
				int column = std::min(blockX * 4 + x, width - 1);
				std::memcpy(block + (y * 4 + x) * 4, pixels + (static_cast<size_t>(row) * width + column) * 4, 4);
			}
		}
	}

	// the next level down: each pixel is the average of the 2 x 2 above it (a side already 1 pixel wide repeats its pixel)
	std::vector<uint8_t> HalfSize(const std::vector<uint8_t>& level, int width, int height, int& halfWidth, int& halfHeight)
	{
		halfWidth = std::max(1, width >> 1);
		halfHeight = std::max(1, height >> 1);
		std::vector<uint8_t> half(static_cast<size_t>(halfWidth) * halfHeight * 4);
		for (int y = 0; y < halfHeight; y++)
		{
			int y0 = std::min(y * 2, height - 1);
			int y1 = std::min(y * 2 + 1, height - 1);
			for (int x = 0; x < halfWidth; x++)
			{
				int x0 = std::min(x * 2, width - 1);
				int x1 = std::min(x * 2 + 1, width - 1);
				const uint8_t* a = &level[(static_cast<size_t>(y0) * width + x0) * 4];
				const uint8_t* b = &level[(static_cast<size_t>(y0) * width + x1) * 4];
				const uint8_t* c = &level[(static_cast<size_t>(y1) * width + x0) * 4];
				const uint8_t* d = &level[(static_cast<size_t>(y1) * width + x1) * 4];
				uint8_t* to = &half[(static_cast<size_t>(y) * halfWidth + x) * 4];
				for (int channel = 0; channel < 4; channel++)
				{
					to[channel] = static_cast<uint8_t>((a[channel] + b[channel] + c[channel] + d[channel] + 2) / 4);
				}
			}
		}
		return half;
	}

	// compresses one level into out, returns the bytes written
	size_t EncodeLevel(const uint8_t* pixels, int width, int height, uint8_t* out, int format, int level, const bc7enc_compress_block_params& bc7)
	{
		const size_t blockBytes = format == FormatDXT1 ? 8 : 16;
		const int blocksWide = (width + 3) / 4;
		const int blocksHigh = (height + 3) / 4;
		const uint32_t rgbcxLevel = static_cast<uint32_t>(std::clamp(level, static_cast<int>(rgbcx::MIN_LEVEL), static_cast<int>(rgbcx::MAX_LEVEL)));
		uint8_t block[64];
		uint8_t* to = out;
		for (int by = 0; by < blocksHigh; by++)
		{
			for (int bx = 0; bx < blocksWide; bx++)
			{
				TakeBlock(pixels, width, height, bx, by, block);
				if (format == FormatDXT1)
				{
					// (no transparent black: the game's shaders sample DXT1 alpha as 1)
					rgbcx::encode_bc1(rgbcxLevel, to, block, true, false);
				}
				else if (format == FormatDXT5)
				{
					rgbcx::encode_bc3(rgbcxLevel, to, block);
				}
				else
				{
					bc7enc_compress_block(to, block, &bc7);
				}
				to += blockBytes;
			}
		}
		return static_cast<size_t>(to - out);
	}
}

extern "C" __declspec(dllexport) int __cdecl encode(const uint8_t* src, int width, int height, uint8_t* out, int format, int level, int genMips, int threads)
{
	// (threads: the game always asks for one - every encode already runs on its own worker)
	(void)threads;
	if (src == nullptr || out == nullptr || width <= 0 || height <= 0)
	{
		return 0;
	}
	if (format != FormatDXT1 && format != FormatDXT5 && format != FormatBC7)
	{
		return 0;
	}
	PrepareTables();

	// BC7: the game's quality numbers over bc7enc's uber levels 0 - 4 (0 - 3 -> 0, 7 -> 1, 16 and up -> 4); a higher
	// uber level gained 0.2 dB on the test picture at twice the time
	bc7enc_compress_block_params bc7;
	bc7enc_compress_block_params_init(&bc7);
	bc7.m_uber_level = static_cast<uint32_t>(std::clamp(level / 4, 0, BC7ENC_MAX_UBER_LEVEL));

	std::vector<uint8_t> current(src, src + static_cast<size_t>(width) * height * 4);
	size_t written = 0;
	int levelWidth = width;
	int levelHeight = height;
	for (int levelIndex = 0; levelIndex < MaxLevels; levelIndex++)
	{
		written += EncodeLevel(current.data(), levelWidth, levelHeight, out + written, format, level, bc7);
		if (!genMips || (levelWidth == 1 && levelHeight == 1))
		{
			break;
		}
		int halfWidth;
		int halfHeight;
		current = HalfSize(current, levelWidth, levelHeight, halfWidth, halfHeight);
		levelWidth = halfWidth;
		levelHeight = halfHeight;
	}
	return static_cast<int>(written);
}
