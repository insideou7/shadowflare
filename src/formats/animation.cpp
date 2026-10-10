#include "formats/animation.h"

#include <string_view>

#include "formats/binary_reader.h"

namespace sf {

AnimationSet AnimationSet::parse(std::span<const std::uint8_t> data) {
    BinaryReader in(data);
    const std::string_view magic = in.chars(16);
    if (!magic.starts_with("CHRAnimation")) throw FormatError("not an animation file");
    AnimationSet set;
    set.version = (magic[12] - '0') * 100 + (magic[13] - '0') * 10 + (magic[14] - '0');

    const std::uint32_t chartCount = in.u32();
    set.charts.resize(chartCount);
    for (auto& chart : set.charts) {
        chart.status = in.i16();
        chart.directions.resize(kDirections);
        for (auto& dir : chart.directions) {
            const std::uint32_t layerCount = in.u32();
            dir.frameCount = in.i16();
            if (layerCount > 256) throw FormatError("too many animation layers");
            dir.layers.resize(layerCount);
            for (auto& layer : dir.layers) {
                const std::uint32_t frames = in.u32();
                if (frames > 4096) throw FormatError("too many animation frames");
                layer.resize(frames);
                for (auto& cell : layer) {
                    cell.status = in.i16();
                    cell.trans = in.i16();
                    cell.pattern = set.version <= 1 ? in.i16() : in.i32();
                    cell.priority = in.i16();
                }
            }
        }
    }
    // Versions 1+ end with two more values, not used yet.
    return set;
}

}  // namespace sf
