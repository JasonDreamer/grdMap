#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>

namespace {

struct Pixel8 {
    unsigned char alpha;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

struct Pixel16 {
    unsigned short alpha;
    unsigned short red;
    unsigned short green;
    unsigned short blue;
};

struct PixelFloat {
    float alpha;
    float red;
    float green;
    float blue;
};

struct FloatPixel {
    float alpha;
    float red;
    float green;
    float blue;
};

struct HslColor {
    float hue;
    float saturation;
    float lightness;
};

__device__ float Clamp01(float value)
{
    return fminf(1.0f, fmaxf(0.0f, value));
}

__device__ HslColor RgbToHsl(FloatPixel color)
{
    const float red = Clamp01(color.red);
    const float green = Clamp01(color.green);
    const float blue = Clamp01(color.blue);
    const float maximum = fmaxf(red, fmaxf(green, blue));
    const float minimum = fminf(red, fminf(green, blue));
    const float delta = maximum - minimum;
    HslColor result{0.0f, 0.0f, (maximum + minimum) * 0.5f};
    if (delta <= 1.0e-6f) return result;

    result.saturation = delta / (1.0f - fabsf(2.0f * result.lightness - 1.0f));
    if (maximum == red) {
        result.hue = fmodf((green - blue) / delta, 6.0f) / 6.0f;
    } else if (maximum == green) {
        result.hue = ((blue - red) / delta + 2.0f) / 6.0f;
    } else {
        result.hue = ((red - green) / delta + 4.0f) / 6.0f;
    }
    if (result.hue < 0.0f) result.hue += 1.0f;
    return result;
}

__device__ FloatPixel HslToRgb(HslColor color)
{
    const float chroma = (1.0f - fabsf(2.0f * color.lightness - 1.0f)) * color.saturation;
    const float sector = color.hue * 6.0f;
    const float secondary = chroma * (1.0f - fabsf(fmodf(sector, 2.0f) - 1.0f));
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    if (sector < 1.0f) {
        red = chroma; green = secondary;
    } else if (sector < 2.0f) {
        red = secondary; green = chroma;
    } else if (sector < 3.0f) {
        green = chroma; blue = secondary;
    } else if (sector < 4.0f) {
        green = secondary; blue = chroma;
    } else if (sector < 5.0f) {
        red = secondary; blue = chroma;
    } else {
        red = chroma; blue = secondary;
    }
    const float match = color.lightness - chroma * 0.5f;
    return FloatPixel{1.0f, red + match, green + match, blue + match};
}

__device__ float SoftLight(float base, float blend)
{
    if (blend <= 0.5f) {
        return base - (1.0f - 2.0f * blend) * base * (1.0f - base);
    }
    const float curve = base <= 0.25f
        ? ((16.0f * base - 12.0f) * base + 4.0f) * base
        : sqrtf(base);
    return base + (2.0f * blend - 1.0f) * (curve - base);
}

__device__ float BlendChannel(float base, float blend, int mode)
{
    switch (mode) {
        case 1: return fminf(base, blend);
        case 2: return base * blend;
        case 3: return fmaxf(base, blend);
        case 4: return 1.0f - (1.0f - base) * (1.0f - blend);
        case 5: return base <= 0.5f
            ? 2.0f * base * blend
            : 1.0f - 2.0f * (1.0f - base) * (1.0f - blend);
        case 6: return SoftLight(base, blend);
        case 7: return blend <= 0.5f
            ? 2.0f * base * blend
            : 1.0f - 2.0f * (1.0f - base) * (1.0f - blend);
        case 8: return fabsf(base - blend);
        default: return blend;
    }
}

__device__ FloatPixel BlendRgb(FloatPixel base, FloatPixel blend, int mode)
{
    base.red = Clamp01(base.red);
    base.green = Clamp01(base.green);
    base.blue = Clamp01(base.blue);
    blend.red = Clamp01(blend.red);
    blend.green = Clamp01(blend.green);
    blend.blue = Clamp01(blend.blue);
    if (mode == 9 || mode == 10) {
        const HslColor base_hsl = RgbToHsl(base);
        const HslColor blend_hsl = RgbToHsl(blend);
        return mode == 9
            ? HslToRgb(HslColor{blend_hsl.hue, blend_hsl.saturation, base_hsl.lightness})
            : HslToRgb(HslColor{base_hsl.hue, base_hsl.saturation, blend_hsl.lightness});
    }
    return FloatPixel{
        1.0f,
        BlendChannel(base.red, blend.red, mode),
        BlendChannel(base.green, blend.green, mode),
        BlendChannel(base.blue, blend.blue, mode)};
}

__device__ float ApplyRepeat(float position, int repeat_count, int repeat_mode)
{
    repeat_count = repeat_count < 1 ? 1 : repeat_count;
    const float scaled = position * static_cast<float>(repeat_count);
    const float phase = scaled - floorf(scaled);
    if (repeat_mode == 1) {
        return 1.0f - fabsf(phase * 2.0f - 1.0f);
    }
    return position >= 1.0f ? 1.0f : phase;
}

__device__ float DitherNoise(int x, int y)
{
    uint32_t value = static_cast<uint32_t>(x) * 0x1f123bb5U ^ static_cast<uint32_t>(y) * 0x5f356495U;
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return (static_cast<float>(value & 0xFFFFU) / 65535.0f) - 0.5f;
}

__device__ FloatPixel SampleLut(const float* lut, int lut_size, float position)
{
    position = Clamp01(position);
    const float scaled = position * static_cast<float>(lut_size - 1);
    const int left_index = static_cast<int>(scaled);
    const int right_index = min(left_index + 1, lut_size - 1);
    const float amount = scaled - static_cast<float>(left_index);
    const float* left = lut + left_index * 4;
    const float* right = lut + right_index * 4;
    FloatPixel result;
    result.red = left[0] + (right[0] - left[0]) * amount;
    result.green = left[1] + (right[1] - left[1]) * amount;
    result.blue = left[2] + (right[2] - left[2]) * amount;
    result.alpha = left[3] + (right[3] - left[3]) * amount;
    return result;
}

__device__ FloatPixel MapPixel(
    FloatPixel input,
    int x,
    int y,
    const float* lut,
    int lut_size,
    int mapping_source,
    int repeat_count,
    int repeat_mode,
    bool reverse,
    bool dither,
    bool use_gradient_alpha,
    int blend_mode,
    float amount,
    float dither_scale)
{
    const float input_alpha = Clamp01(input.alpha);
    float straight_red = 0.0f;
    float straight_green = 0.0f;
    float straight_blue = 0.0f;
    if (input_alpha > 1.0e-6f) {
        const float inverse_alpha = 1.0f / input_alpha;
        straight_red = input.red * inverse_alpha;
        straight_green = input.green * inverse_alpha;
        straight_blue = input.blue * inverse_alpha;
    }

    float position = 0.0f;
    switch (mapping_source) {
        case 1: position = straight_red; break;
        case 2: position = straight_green; break;
        case 3: position = straight_blue; break;
        case 4: position = input_alpha; break;
        default:
            position = 0.2126f * straight_red + 0.7152f * straight_green + 0.0722f * straight_blue;
            break;
    }
    position = Clamp01(position);
    position = ApplyRepeat(position, repeat_count, repeat_mode);
    if (reverse) position = 1.0f - position;

    FloatPixel mapped = SampleLut(lut, lut_size, position);
    if (dither && dither_scale > 0.0f) {
        const float noise = DitherNoise(x, y) * dither_scale;
        mapped.red = Clamp01(mapped.red + noise);
        mapped.green = Clamp01(mapped.green + noise);
        mapped.blue = Clamp01(mapped.blue + noise);
    }
    const float effect_alpha = use_gradient_alpha
        ? input_alpha * Clamp01(mapped.alpha)
        : input_alpha;
    const FloatPixel blended = BlendRgb(
        FloatPixel{input_alpha, straight_red, straight_green, straight_blue}, mapped, blend_mode);
    amount = Clamp01(amount);
    mapped.alpha = input_alpha + (effect_alpha - input_alpha) * amount;
    mapped.red = input.red + (Clamp01(blended.red) * effect_alpha - input.red) * amount;
    mapped.green = input.green + (Clamp01(blended.green) * effect_alpha - input.green) * amount;
    mapped.blue = input.blue + (Clamp01(blended.blue) * effect_alpha - input.blue) * amount;
    return mapped;
}

__global__ void GrdMapKernel8(
    const Pixel8* input,
    size_t input_row_bytes,
    Pixel8* output,
    size_t output_row_bytes,
    int width,
    int height,
    int dither_origin_x,
    int dither_origin_y,
    const float* lut,
    int lut_size,
    int mapping_source,
    int repeat_count,
    int repeat_mode,
    bool reverse,
    bool dither,
    bool use_gradient_alpha,
    int blend_mode,
    float amount)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    const Pixel8* input_row = reinterpret_cast<const Pixel8*>(
        reinterpret_cast<const char*>(input) + static_cast<size_t>(y) * input_row_bytes);
    Pixel8* output_row = reinterpret_cast<Pixel8*>(
        reinterpret_cast<char*>(output) + static_cast<size_t>(y) * output_row_bytes);
    const Pixel8 source = input_row[x];
    const float scale = 1.0f / 255.0f;
    const FloatPixel input_pixel{
        source.alpha * scale, source.red * scale, source.green * scale, source.blue * scale};
    const FloatPixel mapped = MapPixel(
        input_pixel, x + dither_origin_x, y + dither_origin_y,
        lut, lut_size, mapping_source, repeat_count, repeat_mode,
        reverse, dither,
        use_gradient_alpha, blend_mode, amount, scale);
    output_row[x].alpha = static_cast<unsigned char>(Clamp01(mapped.alpha) * 255.0f + 0.5f);
    output_row[x].red = static_cast<unsigned char>(Clamp01(mapped.red) * 255.0f + 0.5f);
    output_row[x].green = static_cast<unsigned char>(Clamp01(mapped.green) * 255.0f + 0.5f);
    output_row[x].blue = static_cast<unsigned char>(Clamp01(mapped.blue) * 255.0f + 0.5f);
}

__global__ void GrdMapKernel16(
    const Pixel16* input,
    size_t input_row_bytes,
    Pixel16* output,
    size_t output_row_bytes,
    int width,
    int height,
    int dither_origin_x,
    int dither_origin_y,
    const float* lut,
    int lut_size,
    int mapping_source,
    int repeat_count,
    int repeat_mode,
    bool reverse,
    bool dither,
    bool use_gradient_alpha,
    int blend_mode,
    float amount)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    const Pixel16* input_row = reinterpret_cast<const Pixel16*>(
        reinterpret_cast<const char*>(input) + static_cast<size_t>(y) * input_row_bytes);
    Pixel16* output_row = reinterpret_cast<Pixel16*>(
        reinterpret_cast<char*>(output) + static_cast<size_t>(y) * output_row_bytes);
    const Pixel16 source = input_row[x];
    const float scale = 1.0f / 32768.0f;
    const FloatPixel input_pixel{
        source.alpha * scale, source.red * scale, source.green * scale, source.blue * scale};
    const FloatPixel mapped = MapPixel(
        input_pixel, x + dither_origin_x, y + dither_origin_y,
        lut, lut_size, mapping_source, repeat_count, repeat_mode,
        reverse, dither,
        use_gradient_alpha, blend_mode, amount, scale);
    output_row[x].alpha = static_cast<unsigned short>(Clamp01(mapped.alpha) * 32768.0f + 0.5f);
    output_row[x].red = static_cast<unsigned short>(Clamp01(mapped.red) * 32768.0f + 0.5f);
    output_row[x].green = static_cast<unsigned short>(Clamp01(mapped.green) * 32768.0f + 0.5f);
    output_row[x].blue = static_cast<unsigned short>(Clamp01(mapped.blue) * 32768.0f + 0.5f);
}

__global__ void GrdMapKernel32(
    const PixelFloat* input,
    size_t input_row_bytes,
    PixelFloat* output,
    size_t output_row_bytes,
    int width,
    int height,
    const float* lut,
    int lut_size,
    int mapping_source,
    int repeat_count,
    int repeat_mode,
    bool reverse,
    bool use_gradient_alpha,
    int blend_mode,
    float amount)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    const PixelFloat* input_row = reinterpret_cast<const PixelFloat*>(
        reinterpret_cast<const char*>(input) + static_cast<size_t>(y) * input_row_bytes);
    PixelFloat* output_row = reinterpret_cast<PixelFloat*>(
        reinterpret_cast<char*>(output) + static_cast<size_t>(y) * output_row_bytes);
    const PixelFloat source = input_row[x];
    const FloatPixel input_pixel{source.alpha, source.red, source.green, source.blue};
    const FloatPixel mapped = MapPixel(
        input_pixel, x, y, lut, lut_size, mapping_source, repeat_count, repeat_mode,
        reverse, false,
        use_gradient_alpha, blend_mode, amount, 0.0f);
    output_row[x].alpha = mapped.alpha;
    output_row[x].red = mapped.red;
    output_row[x].green = mapped.green;
    output_row[x].blue = mapped.blue;
}

} // namespace

extern "C" bool LaunchGrdMapCUDA(
    const void* input_buffer,
    int input_row_bytes,
    void* output_buffer,
    int output_row_bytes,
    int width,
    int height,
    int dither_origin_x,
    int dither_origin_y,
    int bit_depth,
    const float* lut_rgba,
    int lut_size,
    int mapping_source,
    int repeat_count,
    int repeat_mode,
    bool reverse,
    bool dither,
    bool use_gradient_alpha,
    int blend_mode,
    float amount)
{
    if (!input_buffer || !output_buffer || !lut_rgba || input_row_bytes <= 0 || output_row_bytes <= 0 ||
        width <= 0 || height <= 0 || lut_size < 2 ||
        (bit_depth != 8 && bit_depth != 16 && bit_depth != 32)) {
        return false;
    }

    const size_t pixel_bytes = bit_depth == 32 ? sizeof(PixelFloat)
        : bit_depth == 16 ? sizeof(Pixel16) : sizeof(Pixel8);
    const size_t device_row_bytes = static_cast<size_t>(width) * pixel_bytes;
    if (device_row_bytes > static_cast<size_t>(input_row_bytes) ||
        device_row_bytes > static_cast<size_t>(output_row_bytes)) {
        return false;
    }
    const size_t image_bytes = device_row_bytes * static_cast<size_t>(height);
    const size_t lut_bytes = static_cast<size_t>(lut_size) * 4U * sizeof(float);
    void* device_input = nullptr;
    void* device_output = nullptr;
    float* device_lut = nullptr;
    cudaError_t error = cudaSuccess;

    error = cudaMalloc(&device_input, image_bytes);
    if (error == cudaSuccess) error = cudaMalloc(&device_output, image_bytes);
    if (error == cudaSuccess) error = cudaMalloc(reinterpret_cast<void**>(&device_lut), lut_bytes);
    // Host worlds may be cropped views into larger buffers. Never transfer row padding
    // or assume that rowbytes * height bytes are accessible from a view's data pointer.
    if (error == cudaSuccess) {
        error = cudaMemcpy2D(device_input, device_row_bytes, input_buffer,
            static_cast<size_t>(input_row_bytes), device_row_bytes,
            static_cast<size_t>(height), cudaMemcpyHostToDevice);
    }
    if (error == cudaSuccess) error = cudaMemcpy(device_lut, lut_rgba, lut_bytes, cudaMemcpyHostToDevice);

    if (error == cudaSuccess) {
        const dim3 block(16, 16);
        const dim3 grid(
            static_cast<unsigned int>((width + block.x - 1) / block.x),
            static_cast<unsigned int>((height + block.y - 1) / block.y));
        mapping_source = mapping_source < 0 || mapping_source > 4 ? 0 : mapping_source;
        repeat_mode = repeat_mode == 1 ? 1 : 0;
        blend_mode = blend_mode < 0 || blend_mode >= 11 ? 0 : blend_mode;
        if (bit_depth == 8) {
            GrdMapKernel8<<<grid, block>>>(
                static_cast<const Pixel8*>(device_input), device_row_bytes,
                static_cast<Pixel8*>(device_output), device_row_bytes,
                width, height, dither_origin_x, dither_origin_y, device_lut, lut_size, mapping_source,
                repeat_count, repeat_mode, reverse, dither, use_gradient_alpha, blend_mode, amount);
        } else if (bit_depth == 16) {
            GrdMapKernel16<<<grid, block>>>(
                static_cast<const Pixel16*>(device_input), device_row_bytes,
                static_cast<Pixel16*>(device_output), device_row_bytes,
                width, height, dither_origin_x, dither_origin_y, device_lut, lut_size, mapping_source,
                repeat_count, repeat_mode, reverse, dither, use_gradient_alpha, blend_mode, amount);
        } else {
            GrdMapKernel32<<<grid, block>>>(
                static_cast<const PixelFloat*>(device_input), device_row_bytes,
                static_cast<PixelFloat*>(device_output), device_row_bytes,
                width, height, device_lut, lut_size, mapping_source,
                repeat_count, repeat_mode, reverse, use_gradient_alpha, blend_mode, amount);
        }
        error = cudaGetLastError();
    }
    if (error == cudaSuccess) error = cudaDeviceSynchronize();
    if (error == cudaSuccess) {
        error = cudaMemcpy2D(output_buffer, static_cast<size_t>(output_row_bytes),
            device_output, device_row_bytes, device_row_bytes,
            static_cast<size_t>(height), cudaMemcpyDeviceToHost);
    }

    if (device_lut) cudaFree(device_lut);
    if (device_output) cudaFree(device_output);
    if (device_input) cudaFree(device_input);
    return error == cudaSuccess;
}
