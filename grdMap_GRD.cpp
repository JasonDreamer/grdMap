#include "grdMap.h"

#ifdef AE_OS_WIN

#include <CommDlg.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr size_t kMaxGrdFileSize = 128U * 1024U * 1024U;
constexpr size_t kMaxGradientCount = GRDMAP_MAX_GRADIENTS;
constexpr size_t kMaxStopsPerGradient = 4096U;
constexpr size_t kMaxCachedLibraries = 4U;

struct ParsedColorStop {
    double location = 0.0;
    double midpoint = 0.5;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double cyan = 0.0;
    double magenta = 0.0;
    double yellow = 0.0;
    double black = 0.0;
    double hue = 0.0;
    double saturation = 0.0;
    double brightness = 0.0;
    double lab_l = 0.0;
    double lab_a = 0.0;
    double lab_b = 0.0;
    double gray = 0.0;
    std::string color_model;
    std::string stop_type;
    bool has_explicit_color = false;
    bool supported = true;
};

struct ParsedAlphaStop {
    double location = 0.0;
    double midpoint = 0.5;
    double alpha = 1.0;
};

struct ParsedGradient {
    std::wstring name;
    std::vector<ParsedColorStop> colors;
    std::vector<ParsedAlphaStop> alphas;
    bool noise = false;
    bool supported = true;
};

struct GradientLibrary {
    std::wstring path;
    std::vector<ParsedGradient> gradients;
    std::vector<std::unique_ptr<GrdMapData>> built_gradients;
    std::mutex gradient_mutex;
};

std::mutex g_library_cache_mutex;
std::vector<std::shared_ptr<GradientLibrary>> g_library_cache;

double Clamp01(double value)
{
    return (std::max)(0.0, (std::min)(1.0, value));
}

std::string TrimId(std::string id)
{
    while (!id.empty() && (id.back() == ' ' || id.back() == '\0')) {
        id.pop_back();
    }
    return id;
}

void HsvToRgb(double hue, double saturation, double value, double& red, double& green, double& blue)
{
    hue = std::fmod(hue, 360.0);
    if (hue < 0.0) {
        hue += 360.0;
    }
    saturation = Clamp01(saturation);
    value = Clamp01(value);
    const double chroma = value * saturation;
    const double h = hue / 60.0;
    const double x = chroma * (1.0 - std::fabs(std::fmod(h, 2.0) - 1.0));
    double r1 = 0.0;
    double g1 = 0.0;
    double b1 = 0.0;
    if (h < 1.0) {
        r1 = chroma; g1 = x;
    } else if (h < 2.0) {
        r1 = x; g1 = chroma;
    } else if (h < 3.0) {
        g1 = chroma; b1 = x;
    } else if (h < 4.0) {
        g1 = x; b1 = chroma;
    } else if (h < 5.0) {
        r1 = x; b1 = chroma;
    } else {
        r1 = chroma; b1 = x;
    }
    const double m = value - chroma;
    red = r1 + m;
    green = g1 + m;
    blue = b1 + m;
}

double LabPivot(double value)
{
    const double cube = value * value * value;
    return cube > 0.008856451679 ? cube : (value - 16.0 / 116.0) / 7.787037;
}

double LinearToSrgb(double value)
{
    if (value <= 0.0031308) {
        return 12.92 * value;
    }
    return 1.055 * std::pow((std::max)(0.0, value), 1.0 / 2.4) - 0.055;
}

void LabToRgb(double l, double a, double b, double& red, double& green, double& blue)
{
    const double fy = (l + 16.0) / 116.0;
    const double fx = fy + a / 500.0;
    const double fz = fy - b / 200.0;
    const double x50 = 0.96422 * LabPivot(fx);
    const double y50 = 1.00000 * LabPivot(fy);
    const double z50 = 0.82521 * LabPivot(fz);

    const double x65 = 0.9555766 * x50 - 0.0230393 * y50 + 0.0631636 * z50;
    const double y65 = -0.0282895 * x50 + 1.0099416 * y50 + 0.0210077 * z50;
    const double z65 = 0.0122982 * x50 - 0.0204830 * y50 + 1.3299098 * z50;

    red = Clamp01(LinearToSrgb(3.2404542 * x65 - 1.5371385 * y65 - 0.4985314 * z65));
    green = Clamp01(LinearToSrgb(-0.9692660 * x65 + 1.8760108 * y65 + 0.0415560 * z65));
    blue = Clamp01(LinearToSrgb(0.0556434 * x65 - 0.2040259 * y65 + 1.0572252 * z65));
}

void FinalizeColor(ParsedColorStop& stop)
{
    const std::string type = TrimId(stop.stop_type);
    if (!stop.has_explicit_color) {
        if (type == "FrgC" || type == "1") {
            stop.red = stop.green = stop.blue = 0.0;
            return;
        }
        if (type == "BckC" || type == "2") {
            stop.red = stop.green = stop.blue = 1.0;
            return;
        }
        stop.supported = false;
        return;
    }

    const std::string model = TrimId(stop.color_model);
    if (model == "RGBC" || model == "0") {
        stop.red = Clamp01(stop.red / 255.0);
        stop.green = Clamp01(stop.green / 255.0);
        stop.blue = Clamp01(stop.blue / 255.0);
    } else if (model == "CMYC" || model == "2") {
        const double c = Clamp01(stop.cyan / 100.0);
        const double m = Clamp01(stop.magenta / 100.0);
        const double y = Clamp01(stop.yellow / 100.0);
        const double k = Clamp01(stop.black / 100.0);
        stop.red = (1.0 - c) * (1.0 - k);
        stop.green = (1.0 - m) * (1.0 - k);
        stop.blue = (1.0 - y) * (1.0 - k);
    } else if (model == "HSBC" || model == "1") {
        HsvToRgb(stop.hue, stop.saturation / 100.0, stop.brightness / 100.0,
                 stop.red, stop.green, stop.blue);
    } else if (model == "LbCl" || model == "7") {
        LabToRgb(stop.lab_l, stop.lab_a, stop.lab_b, stop.red, stop.green, stop.blue);
    } else if (model == "Grsc" || model == "8") {
        stop.red = stop.green = stop.blue = Clamp01(stop.gray / 100.0);
    } else {
        stop.supported = false;
    }
}

void FinalizeGradient(ParsedGradient& gradient)
{
    gradient.supported = !gradient.noise && gradient.colors.size() >= 2;
    for (ParsedColorStop& stop : gradient.colors) {
        FinalizeColor(stop);
        gradient.supported = gradient.supported && stop.supported;
        stop.location = Clamp01(stop.location);
        stop.midpoint = (std::max)(0.01, (std::min)(0.99, stop.midpoint));
    }
    for (ParsedAlphaStop& stop : gradient.alphas) {
        stop.location = Clamp01(stop.location);
        stop.midpoint = (std::max)(0.01, (std::min)(0.99, stop.midpoint));
        stop.alpha = Clamp01(stop.alpha);
    }
    std::sort(gradient.colors.begin(), gradient.colors.end(), [](const ParsedColorStop& a, const ParsedColorStop& b) {
        return a.location < b.location;
    });
    std::sort(gradient.alphas.begin(), gradient.alphas.end(), [](const ParsedAlphaStop& a, const ParsedAlphaStop& b) {
        return a.location < b.location;
    });
    if (gradient.alphas.empty()) {
        gradient.alphas.push_back({0.0, 0.5, 1.0});
        gradient.alphas.push_back({1.0, 0.5, 1.0});
    }
}

class BigEndianReader {
public:
    explicit BigEndianReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

    size_t Offset() const { return offset_; }
    size_t Remaining() const { return offset_ <= bytes_.size() ? bytes_.size() - offset_ : 0; }

    bool Skip(size_t count)
    {
        if (count > Remaining()) {
            return false;
        }
        offset_ += count;
        return true;
    }

    bool ReadU8(uint8_t& value)
    {
        if (Remaining() < 1) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool ReadU16(uint16_t& value)
    {
        if (Remaining() < 2) return false;
        value = static_cast<uint16_t>((bytes_[offset_] << 8) | bytes_[offset_ + 1]);
        offset_ += 2;
        return true;
    }

    bool ReadU32(uint32_t& value)
    {
        if (Remaining() < 4) return false;
        value = (static_cast<uint32_t>(bytes_[offset_]) << 24) |
                (static_cast<uint32_t>(bytes_[offset_ + 1]) << 16) |
                (static_cast<uint32_t>(bytes_[offset_ + 2]) << 8) |
                static_cast<uint32_t>(bytes_[offset_ + 3]);
        offset_ += 4;
        return true;
    }

    bool ReadI32(int32_t& value)
    {
        uint32_t raw = 0;
        if (!ReadU32(raw)) return false;
        value = static_cast<int32_t>(raw);
        return true;
    }

    bool ReadDouble(double& value)
    {
        if (Remaining() < 8) return false;
        uint64_t raw = 0;
        for (int i = 0; i < 8; ++i) {
            raw = (raw << 8) | bytes_[offset_ + i];
        }
        offset_ += 8;
        std::memcpy(&value, &raw, sizeof(value));
        return std::isfinite(value);
    }

    bool ReadBytes(size_t count, std::string& value)
    {
        if (count > Remaining()) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), count);
        offset_ += count;
        return true;
    }

    bool ReadFourCC(std::string& value)
    {
        return ReadBytes(4, value);
    }

    bool ReadId(std::string& value)
    {
        uint32_t length = 0;
        if (!ReadU32(length)) return false;
        if (length == 0) length = 4;
        if (length > 1024U) return false;
        return ReadBytes(length, value);
    }

    bool ReadUnicode(std::wstring& value)
    {
        uint32_t length = 0;
        if (!ReadU32(length) || length > 1024U * 1024U || Remaining() < static_cast<size_t>(length) * 2U) {
            return false;
        }
        value.clear();
        value.reserve(length);
        for (uint32_t i = 0; i < length; ++i) {
            uint16_t code = 0;
            if (!ReadU16(code)) return false;
            if (code != 0) value.push_back(static_cast<wchar_t>(code));
        }
        return true;
    }

private:
    const std::vector<uint8_t>& bytes_;
    size_t offset_ = 0;
};

enum class ParseRole {
    Ignore,
    GradientWrapper,
    Gradient,
    ColorStop,
    Color,
    AlphaStop
};

struct ParseTarget {
    ParseRole role = ParseRole::Ignore;
    ParsedGradient* gradient = nullptr;
    ParsedColorStop* color = nullptr;
    ParsedAlphaStop* alpha = nullptr;
};

class Version5Parser {
public:
    explicit Version5Parser(const std::vector<uint8_t>& bytes) : reader_(bytes) {}

    bool Parse(std::vector<ParsedGradient>& gradients)
    {
        gradients_ = &gradients;
        std::string signature;
        uint16_t version = 0;
        uint32_t descriptor_version = 0;
        if (!reader_.ReadFourCC(signature) || signature != "8BGR" ||
            !reader_.ReadU16(version) || version != 5 ||
            !reader_.ReadU32(descriptor_version) || descriptor_version != 16) {
            return false;
        }
        ParseTarget root;
        return ParseDescriptor(root) && !gradients.empty();
    }

private:
    bool ParseDescriptor(ParseTarget target)
    {
        std::wstring descriptor_name;
        std::string class_id;
        uint32_t item_count = 0;
        if (!reader_.ReadUnicode(descriptor_name) || !reader_.ReadId(class_id) ||
            !reader_.ReadU32(item_count) || item_count > 100000U) {
            return false;
        }
        if (target.role == ParseRole::Color && target.color) {
            target.color->color_model = TrimId(class_id);
            target.color->has_explicit_color = true;
        }
        for (uint32_t i = 0; i < item_count; ++i) {
            std::string key;
            std::string type;
            if (!reader_.ReadId(key) || !reader_.ReadFourCC(type) || !ParseValue(TrimId(key), type, target)) {
                return false;
            }
        }
        return true;
    }

    bool ParseList(const std::string& key, ParseTarget target)
    {
        uint32_t count = 0;
        if (!reader_.ReadU32(count) || count > 100000U) return false;
        for (uint32_t i = 0; i < count; ++i) {
            std::string type;
            if (!reader_.ReadFourCC(type)) return false;
            if (key == "GrdL") {
                ParsedGradient gradient;
                ParseTarget child{ParseRole::GradientWrapper, &gradient, nullptr, nullptr};
                if (!ParseValue(std::string(), type, child)) return false;
                FinalizeGradient(gradient);
                if (gradient.supported && gradients_->size() < kMaxGradientCount) {
                    if (gradient.name.empty()) gradient.name = L"Gradient";
                    gradients_->push_back(std::move(gradient));
                }
            } else if (key == "Clrs" && target.gradient) {
                ParsedColorStop stop;
                ParseTarget child{ParseRole::ColorStop, target.gradient, &stop, nullptr};
                if (!ParseValue(std::string(), type, child)) return false;
                if (target.gradient->colors.size() < kMaxStopsPerGradient) {
                    target.gradient->colors.push_back(std::move(stop));
                } else {
                    target.gradient->supported = false;
                }
            } else if (key == "Trns" && target.gradient) {
                ParsedAlphaStop stop;
                ParseTarget child{ParseRole::AlphaStop, target.gradient, nullptr, &stop};
                if (!ParseValue(std::string(), type, child)) return false;
                if (target.gradient->alphas.size() < kMaxStopsPerGradient) {
                    target.gradient->alphas.push_back(stop);
                } else {
                    target.gradient->supported = false;
                }
            } else {
                ParseTarget ignored;
                if (!ParseValue(std::string(), type, ignored)) return false;
            }
        }
        return true;
    }

    void AssignNumber(const std::string& key, double value, ParseTarget target)
    {
        if (target.role == ParseRole::Gradient && target.gradient) {
            return;
        }
        if ((target.role == ParseRole::ColorStop || target.role == ParseRole::Color) && target.color) {
            if (key == "Lctn") target.color->location = value / 4096.0;
            else if (key == "Mdpn") target.color->midpoint = value / 100.0;
            else if (key == "Rd") target.color->red = value;
            else if (key == "Grn") target.color->green = value;
            else if (key == "Bl") target.color->blue = value;
            else if (key == "Cyn") target.color->cyan = value;
            else if (key == "Mgnt") target.color->magenta = value;
            else if (key == "Ylw") target.color->yellow = value;
            else if (key == "Blck") target.color->black = value;
            else if (key == "H") target.color->hue = value;
            else if (key == "Strt") target.color->saturation = value;
            else if (key == "Brgh") target.color->brightness = value;
            else if (key == "Lmnc") target.color->lab_l = value;
            else if (key == "A") target.color->lab_a = value;
            else if (key == "B") target.color->lab_b = value;
            else if (key == "Gry") target.color->gray = value;
        } else if (target.role == ParseRole::AlphaStop && target.alpha) {
            if (key == "Lctn") target.alpha->location = value / 4096.0;
            else if (key == "Mdpn") target.alpha->midpoint = value / 100.0;
            else if (key == "Opct") target.alpha->alpha = value / 100.0;
        }
    }

    bool ParseValue(const std::string& key, const std::string& type, ParseTarget target)
    {
        if (type == "Objc" || type == "GlbO") {
            ParseTarget child = target;
            if (key == "Grad" && target.gradient) {
                child.role = ParseRole::Gradient;
            } else if (key == "Clr" && target.color) {
                child.role = ParseRole::Color;
            } else if (target.role != ParseRole::GradientWrapper &&
                       target.role != ParseRole::ColorStop &&
                       target.role != ParseRole::AlphaStop) {
                child = ParseTarget{};
            }
            return ParseDescriptor(child);
        }
        if (type == "VlLs") {
            return ParseList(key, target);
        }
        if (type == "TEXT") {
            std::wstring text;
            if (!reader_.ReadUnicode(text)) return false;
            if (key == "Nm" && target.gradient && target.role == ParseRole::Gradient) {
                target.gradient->name = std::move(text);
            }
            return true;
        }
        if (type == "long") {
            int32_t value = 0;
            if (!reader_.ReadI32(value)) return false;
            AssignNumber(key, static_cast<double>(value), target);
            return true;
        }
        if (type == "doub") {
            double value = 0.0;
            if (!reader_.ReadDouble(value)) return false;
            AssignNumber(key, value, target);
            return true;
        }
        if (type == "UntF") {
            std::string unit;
            double value = 0.0;
            if (!reader_.ReadFourCC(unit) || !reader_.ReadDouble(value)) return false;
            AssignNumber(key, value, target);
            return true;
        }
        if (type == "bool") {
            uint8_t value = 0;
            return reader_.ReadU8(value);
        }
        if (type == "enum") {
            std::string enum_type;
            std::string enum_value;
            if (!reader_.ReadId(enum_type) || !reader_.ReadId(enum_value)) return false;
            enum_value = TrimId(enum_value);
            if (key == "GrdF" && target.gradient) {
                target.gradient->noise = enum_value == "ClNs";
            } else if (key == "Type" && target.color) {
                target.color->stop_type = enum_value;
            }
            return true;
        }
        if (type == "tdta" || type == "alis" || type == "Pth ") {
            uint32_t length = 0;
            return reader_.ReadU32(length) && reader_.Skip(length);
        }
        if (type == "comp") {
            return reader_.Skip(8);
        }
        if (type == "type" || type == "GlbC" || type == "Clss") {
            std::wstring name;
            std::string class_id;
            return reader_.ReadUnicode(name) && reader_.ReadId(class_id);
        }
        if (type == "rele") {
            return reader_.Skip(16);
        }
        if (type == "desc") {
            return reader_.Skip(26);
        }
        if (type == "patt") {
            return true;
        }
        return false;
    }

    BigEndianReader reader_;
    std::vector<ParsedGradient>* gradients_ = nullptr;
};

std::wstring AnsiToWide(const std::string& value)
{
    if (value.empty()) return std::wstring();
    const int count = MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return std::wstring(value.begin(), value.end());
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), &result[0], count);
    return result;
}

bool ParseVersion3(const std::vector<uint8_t>& bytes, std::vector<ParsedGradient>& gradients)
{
    BigEndianReader reader(bytes);
    std::string signature;
    uint16_t version = 0;
    uint16_t gradient_count = 0;
    if (!reader.ReadFourCC(signature) || signature != "8BGR" ||
        !reader.ReadU16(version) || version != 3 || !reader.ReadU16(gradient_count)) {
        return false;
    }
    for (uint16_t gradient_index = 0; gradient_index < gradient_count; ++gradient_index) {
        uint8_t name_length = 0;
        std::string name;
        uint16_t color_count = 0;
        if (!reader.ReadU8(name_length) || !reader.ReadBytes(name_length, name) ||
            !reader.ReadU16(color_count) || color_count > kMaxStopsPerGradient) {
            return false;
        }
        ParsedGradient gradient;
        gradient.name = AnsiToWide(name);
        for (uint16_t i = 0; i < color_count; ++i) {
            uint32_t location = 0;
            uint32_t midpoint = 0;
            uint16_t model = 0;
            uint16_t values[4] = {};
            uint16_t color_type = 0;
            if (!reader.ReadU32(location) || !reader.ReadU32(midpoint) || !reader.ReadU16(model)) return false;
            for (uint16_t& value : values) {
                if (!reader.ReadU16(value)) return false;
            }
            if (!reader.ReadU16(color_type)) return false;

            ParsedColorStop stop;
            stop.location = static_cast<double>(location) / 4096.0;
            stop.midpoint = static_cast<double>(midpoint) / 100.0;
            stop.stop_type = std::to_string(color_type);
            stop.color_model = std::to_string(model);
            stop.has_explicit_color = color_type == 0;
            if (model == 0) {
                stop.red = static_cast<double>(values[0]) * 255.0 / 65535.0;
                stop.green = static_cast<double>(values[1]) * 255.0 / 65535.0;
                stop.blue = static_cast<double>(values[2]) * 255.0 / 65535.0;
            } else if (model == 1) {
                stop.hue = static_cast<double>(values[0]) * 360.0 / 65535.0;
                stop.saturation = static_cast<double>(values[1]) * 100.0 / 65535.0;
                stop.brightness = static_cast<double>(values[2]) * 100.0 / 65535.0;
            } else if (model == 2) {
                stop.cyan = static_cast<double>(values[0]) * 100.0 / 65535.0;
                stop.magenta = static_cast<double>(values[1]) * 100.0 / 65535.0;
                stop.yellow = static_cast<double>(values[2]) * 100.0 / 65535.0;
                stop.black = static_cast<double>(values[3]) * 100.0 / 65535.0;
            } else if (model == 7) {
                stop.lab_l = static_cast<double>(values[0]) * 100.0 / 65535.0;
                stop.lab_a = static_cast<double>(static_cast<int16_t>(values[1])) / 256.0;
                stop.lab_b = static_cast<double>(static_cast<int16_t>(values[2])) / 256.0;
            } else if (model == 8) {
                stop.gray = static_cast<double>(values[0]) * 100.0 / 65535.0;
            }
            gradient.colors.push_back(stop);
        }

        uint16_t alpha_count = 0;
        if (!reader.ReadU16(alpha_count) || alpha_count > kMaxStopsPerGradient) return false;
        for (uint16_t i = 0; i < alpha_count; ++i) {
            uint32_t location = 0;
            uint32_t midpoint = 0;
            uint16_t opacity = 0;
            if (!reader.ReadU32(location) || !reader.ReadU32(midpoint) || !reader.ReadU16(opacity)) return false;
            gradient.alphas.push_back({
                static_cast<double>(location) / 4096.0,
                static_cast<double>(midpoint) / 100.0,
                static_cast<double>(opacity) / 255.0});
        }
        if (!reader.Skip(6)) return false;
        FinalizeGradient(gradient);
        if (gradient.supported && gradients.size() < kMaxGradientCount) {
            if (gradient.name.empty()) gradient.name = L"Gradient";
            gradients.push_back(std::move(gradient));
        }
    }
    return !gradients.empty();
}

double ApplyMidpoint(double value, double midpoint)
{
    midpoint = (std::max)(0.01, (std::min)(0.99, midpoint));
    if (value <= midpoint) {
        return 0.5 * value / midpoint;
    }
    return 0.5 + 0.5 * (value - midpoint) / (1.0 - midpoint);
}

void SampleColor(const ParsedGradient& gradient, double position, double& red, double& green, double& blue)
{
    if (position <= gradient.colors.front().location) {
        red = gradient.colors.front().red;
        green = gradient.colors.front().green;
        blue = gradient.colors.front().blue;
        return;
    }
    if (position >= gradient.colors.back().location) {
        red = gradient.colors.back().red;
        green = gradient.colors.back().green;
        blue = gradient.colors.back().blue;
        return;
    }
    auto right = std::upper_bound(gradient.colors.begin(), gradient.colors.end(), position,
        [](double value, const ParsedColorStop& stop) { return value < stop.location; });
    const ParsedColorStop& left = *(right - 1);
    const double span = right->location - left.location;
    const double linear = span > 0.0 ? (position - left.location) / span : 0.0;
    const double amount = ApplyMidpoint(linear, left.midpoint);
    red = left.red + (right->red - left.red) * amount;
    green = left.green + (right->green - left.green) * amount;
    blue = left.blue + (right->blue - left.blue) * amount;
}

double SampleAlpha(const ParsedGradient& gradient, double position)
{
    if (position <= gradient.alphas.front().location) return gradient.alphas.front().alpha;
    if (position >= gradient.alphas.back().location) return gradient.alphas.back().alpha;
    auto right = std::upper_bound(gradient.alphas.begin(), gradient.alphas.end(), position,
        [](double value, const ParsedAlphaStop& stop) { return value < stop.location; });
    const ParsedAlphaStop& left = *(right - 1);
    const double span = right->location - left.location;
    const double linear = span > 0.0 ? (position - left.location) / span : 0.0;
    const double amount = ApplyMidpoint(linear, left.midpoint);
    return left.alpha + (right->alpha - left.alpha) * amount;
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) return std::string();
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return std::string();
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &result[0], count, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) return std::wstring();
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return std::wstring();
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), &result[0], count);
    return result;
}

void CopyText(A_char* destination, size_t capacity, const std::string& text)
{
    if (!destination || capacity == 0) return;
    const size_t copy_size = (std::min)(capacity - 1, text.size());
    std::memcpy(destination, text.data(), copy_size);
    destination[copy_size] = '\0';
}

void BuildGradientData(
    const ParsedGradient& gradient,
    const std::wstring& path,
    size_t gradient_index,
    size_t gradient_count,
    GrdMapData& data)
{
    std::memset(&data, 0, sizeof(data));
    data.magic = GRDMAP_ARB_MAGIC;
    data.version = GRDMAP_ARB_VERSION;
    CopyText(data.preset_name, GRDMAP_TEXT_CAPACITY, WideToUtf8(gradient.name));
    const size_t slash = path.find_last_of(L"\\/");
    CopyText(data.file_name, GRDMAP_TEXT_CAPACITY, WideToUtf8(path.substr(slash == std::wstring::npos ? 0 : slash + 1)));
    CopyText(data.file_path, GRDMAP_PATH_CAPACITY, WideToUtf8(path));
    data.gradient_index = static_cast<A_long>(gradient_index);
    data.gradient_count = static_cast<A_long>(gradient_count);

    data.color_stop_count = static_cast<A_long>((std::min)(gradient.colors.size(), static_cast<size_t>(GRDMAP_MAX_COLOR_STOPS)));
    for (A_long i = 0; i < data.color_stop_count; ++i) {
        const ParsedColorStop& source = gradient.colors[static_cast<size_t>(i)];
        data.color_stops[i] = {
            static_cast<float>(source.location), static_cast<float>(source.midpoint),
            static_cast<float>(source.red), static_cast<float>(source.green), static_cast<float>(source.blue)};
    }
    data.alpha_stop_count = static_cast<A_long>((std::min)(gradient.alphas.size(), static_cast<size_t>(GRDMAP_MAX_ALPHA_STOPS)));
    for (A_long i = 0; i < data.alpha_stop_count; ++i) {
        const ParsedAlphaStop& source = gradient.alphas[static_cast<size_t>(i)];
        data.alpha_stops[i] = {
            static_cast<float>(source.location), static_cast<float>(source.midpoint), static_cast<float>(source.alpha)};
    }
    for (int i = 0; i < GRDMAP_LUT_SIZE; ++i) {
        const double position = static_cast<double>(i) / static_cast<double>(GRDMAP_LUT_SIZE - 1);
        double red = 0.0;
        double green = 0.0;
        double blue = 0.0;
        SampleColor(gradient, position, red, green, blue);
        data.lut[i][0] = static_cast<float>(Clamp01(red));
        data.lut[i][1] = static_cast<float>(Clamp01(green));
        data.lut[i][2] = static_cast<float>(Clamp01(blue));
        data.lut[i][3] = static_cast<float>(Clamp01(SampleAlpha(gradient, position)));
    }
}

bool ReadFileBytes(const wchar_t* path, std::vector<uint8_t>& bytes)
{
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"rb") != 0 || !file) return false;
    _fseeki64(file, 0, SEEK_END);
    const __int64 size = _ftelli64(file);
    _fseeki64(file, 0, SEEK_SET);
    if (size <= 0 || static_cast<unsigned __int64>(size) > kMaxGrdFileSize) {
        fclose(file);
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    const bool ok = fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    return ok;
}

bool PathsEqual(const std::wstring& first, const std::wstring& second)
{
    return _wcsicmp(first.c_str(), second.c_str()) == 0;
}

bool ParseGradientFile(
    const std::wstring& path,
    std::vector<ParsedGradient>& gradients,
    A_char* error_message,
    size_t error_message_size)
{
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path.c_str(), bytes)) {
        CopyText(error_message, error_message_size, "The selected .grd file could not be read or is too large.");
        return false;
    }
    bool parsed = false;
    if (bytes.size() >= 6 && bytes[4] == 0 && bytes[5] == 5) {
        Version5Parser parser(bytes);
        parsed = parser.Parse(gradients);
    } else if (bytes.size() >= 6 && bytes[4] == 0 && bytes[5] == 3) {
        parsed = ParseVersion3(bytes, gradients);
    }
    if (!parsed || gradients.empty()) {
        CopyText(error_message, error_message_size,
                 "No supported solid gradient was found. grdmap supports Photoshop GRD version 3 and 5 solid gradients.");
        return false;
    }
    return true;
}

std::shared_ptr<GradientLibrary> LoadGradientLibrary(
    const std::wstring& path,
    bool force_reload,
    A_char* error_message,
    size_t error_message_size)
{
    if (!force_reload) {
        std::lock_guard<std::mutex> lock(g_library_cache_mutex);
        for (const auto& library : g_library_cache) {
            if (PathsEqual(library->path, path)) return library;
        }
    }

    std::vector<ParsedGradient> gradients;
    if (!ParseGradientFile(path, gradients, error_message, error_message_size)) return nullptr;
    auto library = std::make_shared<GradientLibrary>();
    library->path = path;
    library->gradients = std::move(gradients);
    library->built_gradients.resize(library->gradients.size());

    std::lock_guard<std::mutex> lock(g_library_cache_mutex);
    for (auto it = g_library_cache.begin(); it != g_library_cache.end(); ++it) {
        if (PathsEqual((*it)->path, path)) {
            if (!force_reload) return *it;
            g_library_cache.erase(it);
            break;
        }
    }
    g_library_cache.push_back(library);
    if (g_library_cache.size() > kMaxCachedLibraries) g_library_cache.erase(g_library_cache.begin());
    return library;
}

bool CopyCachedGradient(
    const std::shared_ptr<GradientLibrary>& library,
    size_t gradient_index,
    GrdMapData& destination,
    A_char* error_message,
    size_t error_message_size)
{
    if (!library || gradient_index >= library->gradients.size()) {
        CopyText(error_message, error_message_size, "The gradient index is outside the imported GRD library.");
        return false;
    }
    std::lock_guard<std::mutex> lock(library->gradient_mutex);
    if (!library->built_gradients[gradient_index]) {
        auto data = std::make_unique<GrdMapData>();
        BuildGradientData(library->gradients[gradient_index], library->path, gradient_index,
                          library->gradients.size(), *data);
        library->built_gradients[gradient_index] = std::move(data);
    }
    destination = *library->built_gradients[gradient_index];
    return true;
}

} // namespace

bool ImportGradientFromDialog(
    PF_InData* in_data,
    GrdMapData* destination,
    bool* cancelled,
    A_char* error_message,
    size_t error_message_size)
{
    if (!destination || !cancelled || !error_message || error_message_size == 0) return false;
    *cancelled = false;
    error_message[0] = '\0';

    HWND parent = nullptr;
    PF_GET_PLATFORM_DATA(PF_PlatData_MAIN_WND, &parent);
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = parent;
    dialog.lpstrFilter = L"Photoshop Gradient (*.grd)\0*.grd\0All Files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"grd";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) {
        *cancelled = CommDlgExtendedError() == 0;
        if (!*cancelled) CopyText(error_message, error_message_size, "The .grd file dialog could not be opened.");
        return false;
    }

    if (WideToUtf8(path.data()).size() >= GRDMAP_PATH_CAPACITY) {
        CopyText(error_message, error_message_size, "The selected .grd path is too long to store in the project.");
        return false;
    }

    const auto library = LoadGradientLibrary(path.data(), true, error_message, error_message_size);
    return CopyCachedGradient(library, 0, *destination, error_message, error_message_size);
}

bool SelectGradientFromCache(
    const GrdMapData* current,
    A_long gradient_index,
    GrdMapData* destination,
    A_char* error_message,
    size_t error_message_size)
{
    if (!current || !destination || !error_message || error_message_size == 0) return false;
    error_message[0] = '\0';
    const std::wstring path = Utf8ToWide(current->file_path);
    if (path.empty()) {
        CopyText(error_message, error_message_size, "Import a GRD file before changing Gradient Index.");
        return false;
    }
    const auto library = LoadGradientLibrary(path, false, error_message, error_message_size);
    if (!library) return false;
    const A_long count = static_cast<A_long>(library->gradients.size());
    const A_long clamped_index = (std::max)(
        static_cast<A_long>(0),
        (std::min)(gradient_index, count - 1));
    return CopyCachedGradient(library, static_cast<size_t>(clamped_index), *destination,
                              error_message, error_message_size);
}

#endif
