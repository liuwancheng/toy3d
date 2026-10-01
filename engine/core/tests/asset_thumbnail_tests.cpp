#include "asset/thumbnail/asset_thumbnail.h"
#include "image/png_codec.h"

#include <algorithm>
#include <iostream>

namespace
{
    int failures = 0;
    void check(bool result, const char* message)
    {
        if (!result) { std::cerr << message << '\n'; ++failures; }
    }
}

int main()
{
    using namespace toy3d;
    check(sha256_to_hex(sha256("abc")) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA migration preserves the standard vector");
    Rgba8Image image{2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 17, 83, 151, 255}};
    std::vector<std::uint8_t> png;
    check(encode_png(image, png).succeeded(), "encode asymmetric color image");
    Rgba8Image decoded;
    check(decode_png(png, decoded).succeeded() && decoded.pixels == image.pixels && decoded.width == 2 &&
        decoded.height == 2, "PNG preserves row order, channels and color bytes");
    ImageLimits small;
    small.max_dimension = 1;
    check(!decode_png(png, decoded, small).succeeded() && decoded.pixels == image.pixels,
        "oversize PNG rejects without replacing output");
    check(!decode_png({1, 2, 3}, decoded).succeeded(), "corrupt PNG rejected");
    auto trailing_png = png;
    trailing_png.push_back(0);
    check(!decode_png(trailing_png, decoded).succeeded(), "PNG trailing bytes rejected");
    auto corrupt_png = png;
    corrupt_png[29] ^= 1;
    check(!decode_png(corrupt_png, decoded).succeeded(), "PNG chunk CRC mismatch rejected");

    AssetId id;
    check(AssetId::try_generate(id), "generate fixture identity");
    AssetFileIndex index;
    index.asset_id = id;
    index.root_type = "toy3d.StaticMeshAssetData";
    index.schema_version = 1;
    const auto original = encode_asset_file(index,
        {{"type_data", 1, true, {9, 8}}, {"render_geometry", 2, true, {7, 6}},
         {"future_optional", 47, false, {5, 4, 3}}, {"import_data", 2, false, {1, 2}}});
    check(original.succeeded(), "encode original opaque asset");
    if (!original.succeeded()) return 1;
    const auto source = calculate_static_mesh_thumbnail_source(original.value());
    check(source.succeeded(), "calculate source signature");
    if (!source.succeeded()) return 1;
    AssetThumbnailData thumbnail{source.value(), thumbnail_generator_version, 2, 2, png};
    const auto encoded = encode_asset_thumbnail(thumbnail);
    check(encoded.succeeded(), "encode thumbnail");
    if (!encoded.succeeded()) return 1;
    const auto loaded = decode_asset_thumbnail(encoded.value().bytes);
    check(loaded.succeeded() && loaded.value().png == png && loaded.value().source.content_hash == source.value().content_hash,
        "thumbnail round trip");
    auto truncated = encoded.value().bytes;
    truncated.pop_back();
    check(!decode_asset_thumbnail(truncated).succeeded(), "truncated thumbnail rejected");
    const auto source_segment = encode_thumbnail_source(source.value());
    check(source_segment.succeeded(), "encode source signature");
    if (!source_segment.succeeded()) return 1;
    const auto replaced = replace_asset_segments(original.value(), {encoded.value(), source_segment.value()});
    check(replaced.succeeded(), "preserving candidate accepts opaque optional segments");
    if (!replaced.succeeded()) return 1;
    const auto after = inspect_asset_bytes(replaced.value());
    check(after.succeeded() && after.value().asset_id == id, "identity retained");
    if (!after.succeeded()) return 1;
    for (const AssetSegment& segment : after.value().segments)
    {
        if (segment.name != "future_optional") continue;
        check(segment.kind == 47 && !segment.required && segment.length == 3 &&
            replaced.value()[static_cast<std::size_t>(segment.offset)] == 5 &&
            replaced.value()[static_cast<std::size_t>(segment.offset + 2)] == 3, "unknown segment preserved exactly");
    }
    const auto unchanged_source = calculate_static_mesh_thumbnail_source(replaced.value());
    check(unchanged_source.succeeded() && unchanged_source.value().content_hash == source.value().content_hash,
        "thumbnail changes do not invalidate their own source signature");
    const auto changed = replace_asset_segments(replaced.value(), {{"render_geometry", 2, true, {7, 99}}});
    check(changed.succeeded(), "replace render bytes");
    if (changed.succeeded())
    {
        const auto changed_source = calculate_static_mesh_thumbnail_source(changed.value());
        check(changed_source.succeeded() && changed_source.value().content_hash != source.value().content_hash,
            "geometry changes invalidate signature");
    }
    check(!replace_asset_segments(original.value(), {encoded.value(), encoded.value()}).succeeded(),
        "duplicate replacement segments rejected");
    return failures ? 1 : 0;
}
