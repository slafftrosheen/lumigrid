// Host behavioral tests: compile the production renderer, not a model of it.
#include "EffectRenderer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using namespace lg;

constexpr uint8_t kExpectedCount = 24;
constexpr uint16_t kPixels = 64;
constexpr std::array<const char*, kExpectedCount> kIds = {{
    "solid", "breathe", "chase", "sweep", "reveal", "sparkle",
    "gradient", "strobe", "fire", "aurora", "colorwave", "staticpulse",
    "rainbow", "colorfade", "scanner", "theater", "twinkle", "candle",
    "heartbeat", "ripple", "dissolve", "plasma", "lightning", "confetti"
}};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

LayerConfig layer_for(uint8_t effect) {
    LayerConfig layer;
    layer.effect = effect;
    layer.primary_color = {237, 83, 19};
    layer.secondary_color = {11, 67, 233};
    layer.speed_ms = 2000;
    layer.fx_param = signage_effect_metadata(static_cast<SignageEffect>(effect)).parameter_default;
    layer.enabled = true;
    return layer;
}

FixtureFrame render(const LayerConfig& layer, uint32_t time, uint16_t count = kPixels) {
    FixtureFrame frame;
    EffectRenderer::render_layer(frame, layer, count, time);
    return frame;
}

bool same_pixel(const LightValue& a, const LightValue& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.w == b.w;
}

bool same_color(const LightValue& a, const BrandColor& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.w == 0;
}

uint64_t hash_frame(const FixtureFrame& frame, uint16_t count = kPixels) {
    uint64_t hash = 1469598103934665603ULL;
    auto add = [&](uint8_t value) { hash = (hash ^ value) * 1099511628211ULL; };
    for (uint16_t px = 0; px < count; ++px) {
        const auto& color = frame.pixels[px];
        add(color.r); add(color.g); add(color.b); add(color.w);
    }
    add(static_cast<uint8_t>(frame.pwm));
    add(static_cast<uint8_t>(frame.pwm >> 8));
    return hash;
}

bool is_lit(const LightValue& p) { return p.r || p.g || p.b || p.w; }

bool differs_over_time(const LayerConfig& a, const LayerConfig& b) {
    for (uint32_t time : {0U, 73U, 239U, 571U, 913U, 1217U, 1771U, 2251U, 3137U}) {
        if (hash_frame(render(a, time)) != hash_frame(render(b, time))) return true;
    }
    return false;
}

void catalogue() {
    require(kSignageEffectCount == kExpectedCount, "catalogue must expose all 24 stable IDs");
    std::set<std::string> names;
    for (uint8_t id = 0; id < kExpectedCount; ++id) {
        const auto effect = static_cast<SignageEffect>(id);
        const auto& metadata = signage_effect_metadata(effect);
        require(std::strcmp(metadata.id, kIds[id]) == 0, "wrong stable ID at " + std::to_string(id));
        SignageEffect parsed = SignageEffect::Solid;
        require(parse_signage_effect(kIds[id], parsed) && static_cast<uint8_t>(parsed) == id,
                std::string("round-trip parse failed for ") + kIds[id]);
        require(names.insert(metadata.id).second, "duplicate effect identifier");
        require(metadata.label[0] && metadata.description[0], "missing effect presentation metadata");
        require(!metadata.has_parameter ||
                (metadata.parameter_min <= metadata.parameter_default && metadata.parameter_default <= metadata.parameter_max),
                "parameter default outside range");
    }
    SignageEffect unchanged = SignageEffect::Chase;
    require(!parse_signage_effect(nullptr, unchanged) && !parse_signage_effect("bogus", unchanged), "invalid ID accepted");
    require(unchanged == SignageEffect::Chase, "failed parse changed its output");
}

void new_effects() {
    std::set<uint64_t> distinct_effects;
    for (uint8_t id = 12; id < kExpectedCount; ++id) {
        auto layer = layer_for(id);
        std::set<uint64_t> frames;
        bool has_light = false;
        bool has_spatial_variation = false;
        uint64_t signature = 0;
        for (uint32_t time = 0; time < 4000; time += 37) {
            auto frame = render(layer, time);
            frames.insert(hash_frame(frame));
            signature = signature * 131 + hash_frame(frame);
            require(frame.pwm <= 4095, std::string(kIds[id]) + " exceeds 12-bit PWM");
            for (uint16_t px = 0; px < kPixels; ++px) {
                has_light |= is_lit(frame.pixels[px]);
                has_spatial_variation |= !same_pixel(frame.pixels[0], frame.pixels[px]);
                require(frame.pixels[px].w == 0, "renderer wrote an RGBW extraction channel");
            }
        }
        require(has_light, std::string(kIds[id]) + " renders only black");
        require(frames.size() > 8, std::string(kIds[id]) + " does not animate");
        const bool spatial = signage_effect_metadata(static_cast<SignageEffect>(id)).spatial;
        require(has_spatial_variation == spatial, std::string(kIds[id]) + " spatial behavior disagrees with metadata");
        require(distinct_effects.insert(signature).second, std::string(kIds[id]) + " duplicates another effect");
    }
}

void deterministic() {
    for (uint8_t id = 0; id < kExpectedCount; ++id) {
        auto layer = layer_for(id);
        const auto expected = render(layer, 123456789U);
        FixtureFrame reused;
        for (auto& px : reused.pixels) px = {17, 29, 191, 255};
        reused.pwm = 4095;
        for (uint8_t other = 0; other < kExpectedCount; ++other) {
            EffectRenderer::render_layer(reused, layer_for(other), kPixels, 4000U - other * 37U);
        }
        EffectRenderer::render_layer(reused, layer, kPixels, 123456789U);
        require(hash_frame(reused) == hash_frame(expected), std::string(kIds[id]) + " depends on frame history/order");
        layer.opacity = 13;
        layer.blend_mode = BlendMode::Multiply;
        require(hash_frame(render(layer, 123456789U)) == hash_frame(expected), "renderer applies downstream opacity/blending");
    }
}

void controls() {
    for (uint8_t id = 12; id < kExpectedCount; ++id) {
        const auto original = layer_for(id);
        auto changed = original;
        changed.speed_ms = 731;
        require(differs_over_time(original, changed), std::string(kIds[id]) + " ignores speed");
        const auto& metadata = signage_effect_metadata(static_cast<SignageEffect>(id));
        changed = original;
        changed.secondary_color = {59, 251, 3};
        require(differs_over_time(original, changed) == metadata.supports_secondary_color,
                std::string(kIds[id]) + " secondary-color control disagrees with metadata");
        changed = original;
        changed.palette_id = 2;
        require(differs_over_time(original, changed) == metadata.supports_palette,
                std::string(kIds[id]) + " palette control disagrees with metadata");
        changed = original;
        changed.primary_color = {9, 243, 147};
        require(differs_over_time(original, changed) == (id != 12), std::string(kIds[id]) + " primary-color behavior is wrong");
        auto minimum = original;
        auto maximum = original;
        minimum.fx_param = metadata.parameter_min;
        maximum.fx_param = metadata.parameter_max;
        require(differs_over_time(minimum, maximum), std::string(kIds[id]) + " ignores its parameter");
        changed = original;
        changed.fx_param = 255;
        require(!differs_over_time(original, changed), std::string(kIds[id]) + " invalid parameter does not use default");
        if (metadata.parameter_min > 0) {
            changed.fx_param = 0;
            require(!differs_over_time(original, changed), std::string(kIds[id]) + " low invalid parameter does not use default");
        }
    }
}

LightValue palette_sample(uint8_t palette, uint8_t index) {
    auto layer = layer_for(6); // Gradient at zero contrast exposes exact palette samples.
    layer.fx_param = 0;
    layer.palette_id = palette;
    layer.speed_ms = 255;
    auto frame = render(layer, index == 255 ? 128 : index, 2);
    return frame.pixels[index == 255 ? 1 : 0];
}

void palettes() {
    require(kSignagePaletteCount == 14, "catalogue must expose all 14 stable palette IDs");
    const std::array<BrandColor, 6> legacy_start = {{{237,83,19}, {0,255,0}, {0,100,155}, {127,0,0}, {0,128,0}, {255,0,0}}};
    const std::array<BrandColor, 6> legacy_middle = {{{123,74,126}, {126,0,129}, {0,177,205}, {255,127,0}, {64,192,32}, {255,255,128}}};
    const std::array<BrandColor, 6> legacy_end = {{{11,67,233}, {0,255,0}, {0,255,255}, {255,254,127}, {127,255,63}, {128,255,255}}};
    for (uint8_t palette = 0; palette < 6; ++palette) {
        require(same_color(palette_sample(palette, 0), legacy_start[palette]), "legacy palette start changed");
        require(same_color(palette_sample(palette, 128), legacy_middle[palette]), "legacy palette middle changed: " + std::to_string(palette));
        require(same_color(palette_sample(palette, 255), legacy_end[palette]), "legacy palette end changed");
    }
    std::set<uint64_t> signatures;
    for (uint8_t palette = 6; palette < 14; ++palette) {
        const auto& metadata = signage_palette_metadata(palette);
        require(metadata.id == palette && metadata.name[0], "palette metadata is missing or misnumbered");
        for (uint8_t stop = 0; stop < 4; ++stop) {
            require(same_color(palette_sample(palette, stop * 85), metadata.colors[stop]),
                    "new palette does not reach its advertised color stops");
        }
        uint64_t signature = 0;
        bool varies = false;
        const auto start = palette_sample(palette, 0);
        for (uint16_t index = 0; index <= 255; index += 17) {
            auto sample = palette_sample(palette, static_cast<uint8_t>(index));
            varies |= !same_pixel(start, sample);
            signature = signature * 131 + sample.r * 65536U + sample.g * 256U + sample.b;
        }
        require(varies, "new palette renders a constant/fallback color: " + std::to_string(palette));
        require(signatures.insert(signature).second, "duplicate new palettes");
    }
    for (uint8_t invalid : {uint8_t(14), uint8_t(255)}) {
        require(same_color(palette_sample(invalid, 71), layer_for(6).primary_color), "invalid palette must use primary color");
    }
}

void legacy_snapshots() {
    // Captured from the original renderer before the new effects were added.
    constexpr std::array<uint64_t, 12> expected = {{
        4710742421614589625ULL, 10313384124402797007ULL, 3854453065657108086ULL,
        1495075462636139561ULL, 17792103433844559601ULL, 17327621182897800293ULL,
        5375116650615471219ULL, 14663315500357416679ULL, 17614244523608299231ULL,
        9419463745743019507ULL, 4770301935703812294ULL, 15080061719287994601ULL
    }};
    for (uint8_t id = 0; id < 12; ++id) {
        uint64_t signature = 0;
        for (uint32_t time : {0U, 73U, 571U, 1771U, 50123U}) {
            signature = signature * 131 + hash_frame(render(layer_for(id), time));
        }
        require(signature == expected[id], std::string(kIds[id]) + " changed the saved-scene rendering baseline");
    }
}

void smooth_motion() {
    for (uint8_t id : {uint8_t(14), uint8_t(15), uint8_t(19)}) {
        auto layer = layer_for(id);
        layer.fx_param = id == 15 ? 6 : 3;
        require(hash_frame(render(layer, 333, 8), 8) != hash_frame(render(layer, 349, 8), 8),
                std::string(kIds[id]) + " loses fractional motion on short strips");
    }
    auto ripple = layer_for(19);
    for (uint16_t count : {uint16_t(1), uint16_t(8), uint16_t(63), uint16_t(64)}) {
        auto frame = render(ripple, 513, count);
        for (uint16_t px = 0; px < count; ++px) {
            require(same_pixel(frame.pixels[px], frame.pixels[count - 1 - px]), "ripple is not centered/symmetric");
        }
    }
}

void temporal_shapes() {
    auto fade = layer_for(13);
    require(same_color(render(fade, 0).pixels[0], fade.primary_color), "colorfade must reach primary");
    require(same_color(render(fade, 1000).pixels[0], fade.secondary_color), "colorfade must reach secondary");
    require(same_color(render(fade, 100).pixels[0], fade.primary_color), "colorfade endpoint hold is missing");
    require(same_color(render(fade, 1100).pixels[0], fade.secondary_color), "colorfade secondary hold is missing");

    auto candle = layer_for(17);
    candle.fx_param = 0;
    require(render(candle, 43).pwm == 4095 && render(candle, 974).pwm == 4095, "zero-depth candle should be steady");
    candle.fx_param = 35;
    for (uint32_t time = 0; time < 5000; time += 17) {
        require(render(candle, time).pwm >= 4095U * 64 / 100, "candle flicker exceeds selected depth");
    }

    auto heartbeat = layer_for(18);
    int pulses = 0;
    bool above = false;
    for (uint32_t time = 0; time < heartbeat.speed_ms; time += 2) {
        bool next = render(heartbeat, time, 0).pwm > 1600;
        if (next && !above) ++pulses;
        above = next;
    }
    require(pulses == 2, "heartbeat must have two distinct pulses");
    require(render(heartbeat, 1500, 0).pwm < 300, "heartbeat must rest between beats");

    auto lightning = layer_for(22);
    for (uint8_t flashes : {uint8_t(1), uint8_t(3), uint8_t(5)}) {
        lightning.fx_param = flashes;
        int observed = 0;
        bool lit = false;
        for (uint32_t time = 0; time < lightning.speed_ms; ++time) {
            bool next = render(lightning, time, 0).pwm > 800;
            if (next && !lit) ++observed;
            lit = next;
        }
        require(observed == flashes, "lightning flash-count parameter disagrees with pulse train");
        require(render(lightning, 1700, 0).pwm == 0, "lightning needs a quiet interval between bursts");
    }

    auto dissolve = layer_for(20);
    auto start = render(dissolve, 0);
    auto end = render(dissolve, 1000);
    for (uint16_t px = 0; px < kPixels; ++px) {
        require(same_color(start.pixels[px], dissolve.primary_color), "dissolve does not begin at primary color");
        require(same_color(end.pixels[px], dissolve.secondary_color), "dissolve does not reach secondary color");
    }
}

void pwm() {
    for (uint8_t id : {uint8_t(17), uint8_t(18), uint8_t(22)}) {
        auto layer = layer_for(id);
        auto black = layer;
        black.primary_color = {0, 0, 0};
        std::set<uint16_t> duties;
        for (uint32_t time = 0; time < 4000; time += 7) {
            auto frame = render(layer, time, 0);
            duties.insert(frame.pwm);
            require(frame.pwm == render(black, time, 0).pwm, "PWM intensity depends on RGB color");
            require(frame.pwm == render(layer, time).pwm, "PWM intensity depends on addressable pixel count");
        }
        require(duties.size() > 10, std::string(kIds[id]) + " does not animate on PWM output");
    }
}

void density() {
    for (uint8_t id : {uint8_t(16), uint8_t(23)}) {
        auto low = layer_for(id);
        auto high = low;
        low.fx_param = 5;
        high.fx_param = 100;
        size_t low_lit = 0, high_lit = 0;
        for (uint32_t time = 0; time < 6000; time += 53) {
            auto a = render(low, time), b = render(high, time);
            for (uint16_t px = 0; px < kPixels; ++px) {
                low_lit += is_lit(a.pixels[px]);
                high_lit += is_lit(b.pixels[px]);
            }
        }
        require(low_lit > 0 && high_lit > low_lit * 4, std::string(kIds[id]) + " density does not control sparse lights on black");
    }
}

uint8_t reference_sine(uint8_t phase) {
    return static_cast<uint8_t>(127.5f - 127.5f * std::cos(phase * (3.14159265359f / 128.0f)));
}

void uptime() {
    for (uint8_t id : {uint8_t(9), uint8_t(10)}) {
        auto layer = layer_for(id);
        layer.primary_color = {255, 255, 255};
        layer.secondary_color = layer.primary_color;
        layer.fx_param = id == 9 ? 255 : 50;
        for (uint32_t time : {16843008U, 16843011U, 86400123U, 4294967280U}) {
            const uint8_t phase = static_cast<uint8_t>((uint64_t(time) * 255) / layer.speed_ms);
            auto frame = render(layer, time);
            const uint8_t red = reference_sine(phase);
            require(frame.pixels[0].r == red, std::string(kIds[id]) + " overflows elapsed-time phase multiplication");
            require(frame.pwm == uint32_t(4095) * red / 255, "long-uptime PWM phase is wrong");
        }
    }
}

void bounds() {
    const std::array<uint16_t, 6> counts = {{0, 1, 7, MAX_PIXELS, static_cast<uint16_t>(MAX_PIXELS + 1), 65535}};
    for (uint8_t id = 0; id < kExpectedCount; ++id) {
        auto layer = layer_for(id);
        for (uint16_t count : counts) {
            for (uint16_t speed : {uint16_t(0), uint16_t(1), uint16_t(65535)}) {
                layer.speed_ms = speed;
                // Separately allocated object makes any oversized clear visible to ASan.
                auto frame = std::make_unique<FixtureFrame>();
                for (auto& px : frame->pixels) px = {17, 29, 191, 255};
                EffectRenderer::render_layer(*frame, layer, count, std::numeric_limits<uint32_t>::max());
                require(frame->pwm <= 4095, "PWM exceeds bounds");
                const uint16_t used = std::min<uint16_t>(count, MAX_PIXELS);
                for (uint16_t px = 0; px < used; ++px) require(frame->pixels[px].w == 0, "active pixel not cleared");
                for (uint16_t px = used; px < MAX_PIXELS; ++px) {
                    require(same_pixel(frame->pixels[px], LightValue{17, 29, 191, 255}), "renderer writes outside active count");
                }
                if (count > MAX_PIXELS) {
                    require(hash_frame(*frame, MAX_PIXELS) == hash_frame(render(layer, std::numeric_limits<uint32_t>::max(), MAX_PIXELS), MAX_PIXELS),
                            "oversized count does not match a safely clamped count");
                }
            }
        }
    }
    auto invalid = layer_for(255);
    auto frame = render(invalid, 100);
    require(frame.pwm == 0 && !is_lit(frame.pixels[0]), "unknown effect should safely clear output");
}

using Test = std::pair<const char*, void(*)()>;
constexpr std::array<Test, 12> kTests = {{{"catalogue", catalogue}, {"new_effects", new_effects},
    {"deterministic", deterministic}, {"controls", controls}, {"palettes", palettes},
    {"smooth_motion", smooth_motion}, {"temporal_shapes", temporal_shapes}, {"pwm", pwm},
    {"density", density}, {"uptime", uptime}, {"legacy_snapshots", legacy_snapshots}, {"bounds", bounds}}};
} // namespace

int main(int argc, char** argv) {
    int failures = 0, executed = 0;
    for (const auto& test : kTests) {
        bool selected = argc == 1;
        for (int i = 1; i < argc; ++i) selected |= std::strcmp(argv[i], test.first) == 0;
        if (!selected) continue;
        ++executed;
        try {
            test.second();
            std::cout << "PASS " << test.first << std::endl;
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << std::endl;
        }
    }
    if (!executed) {
        std::cerr << "No matching test groups" << std::endl;
        return 2;
    }
    std::cout << executed - failures << "/" << executed << " test groups passed" << std::endl;
    return failures ? 1 : 0;
}
