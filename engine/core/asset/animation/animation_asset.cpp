#include "animation_asset.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "misc/sha256.h"
#include "misc/utf8.h"
#include "asset/mesh/mesh_materials.h"
#include "serialization/math_value_codec.h"

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }

        bool valid_root(const AssetPair& pair, const char* type, bool meta)
        {
            return pair.description.index.root_type == type && pair.description.index.schema_version == 1 &&
                   pair.description.has_meta == meta;
        }
    } // namespace

    ReflectionStatus register_animation_asset_types(TypeRegistry& types)
    {
        const auto registered = register_animation_asset_schema(types);
        return registered.succeeded() ? register_mesh_material_migration(types, "toy3d.SkeletalMeshAssetData", 2)
                                      : registered;
    }

    AssetStatus validate_animation_transform(const Transform& transform)
    {
        if (!is_finite(transform.translation) || !is_finite(transform.scale) || transform.scale.x <= 0 ||
            transform.scale.y <= 0 || transform.scale.z <= 0 || !is_finite(transform.rotation) ||
            std::abs(length_squared(transform.rotation) - 1.0f) > 0.001f)
        {
            return invalid("animation transform requires finite positive scale and a unit quaternion");
        }
        return AssetStatus::success();
    }

    AssetStatus validate_skeleton(const SkeletonAssetData& skeleton)
    {
        if (skeleton.bones.empty() || skeleton.bones.size() > max_skeleton_bones)
        {
            return invalid("skeleton bone count is outside the format budget");
        }
        std::set<std::string> names;
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
        {
            const SkeletonBone& bone = skeleton.bones[i];
            if (bone.name.empty() || bone.name.size() > 256 || !is_valid_utf8(bone.name) ||
                !names.insert(bone.name).second ||
                (i == 0 ? bone.parent_index != -1
                        : bone.parent_index < 0 || static_cast<std::size_t>(bone.parent_index) >= i) ||
                !validate_animation_transform(bone.reference_local_transform).succeeded())
            {
                return invalid("invalid skeleton name, parent order or reference transform");
            }
        }
        return AssetStatus::success();
    }

    AssetStatus validate_skeleton_compatibility(const SkeletonAssetData& expected, const SkeletonAssetData& source)
    {
        if (!validate_skeleton(expected).succeeded() || !validate_skeleton(source).succeeded() ||
            expected.bones.size() != source.bones.size())
        {
            return invalid("incompatible skeleton bone count");
        }
        for (std::size_t i = 0; i < expected.bones.size(); ++i)
        {
            const SkeletonBone& a = expected.bones[i];
            const auto found = std::find_if(source.bones.begin(), source.bones.end(),
                                            [&a](const SkeletonBone& b)
                                            {
                                                return a.name == b.name;
                                            });
            if (found == source.bones.end())
            {
                return invalid("missing skeleton bone name");
            }
            const std::string parent_a = a.parent_index < 0 ? "" : expected.bones[a.parent_index].name;
            const std::string parent_b = found->parent_index < 0 ? "" : source.bones[found->parent_index].name;
            if (parent_a != parent_b ||
                !is_nearly_equal(to_matrix(a.reference_local_transform), to_matrix(found->reference_local_transform),
                                 skeleton_reference_tolerance))
            {
                return invalid("incompatible skeleton parent or reference pose");
            }
        }
        return AssetStatus::success();
    }

    AssetResult<std::string> skeleton_reference_hash(const SkeletonAssetData& skeleton)
    {
        const AssetStatus valid = validate_skeleton(skeleton);
        if (!valid.succeeded())
        {
            return AssetResult<std::string>(valid);
        }
        ValueWriter writer;
        if (!encode_value(writer, skeleton).succeeded())
        {
            return AssetResult<std::string>(invalid("skeleton reference encoding failed"));
        }
        return AssetResult<std::string>(sha256_to_hex(sha256(writer.bytes())));
    }

    AssetStatus validate_skeleton_reference(const AssetRef& reference, const std::string& hash)
    {
        if (!reference.asset_id.valid() || reference.subresource_id.valid() ||
            reference.expected_type != "toy3d.SkeletonAssetData" || reference.strength != AssetRefStrength::Strong ||
            !sha256_from_hex(hash).has_value())
        {
            return invalid("invalid skeleton asset reference or reference hash");
        }
        return AssetStatus::success();
    }

    std::uint32_t animation_sample_count(double duration, std::uint32_t sample_rate)
    {
        if (!std::isfinite(duration) || duration < 0 || duration > max_animation_duration ||
            (sample_rate != 30 && sample_rate != 60))
        {
            return 0;
        }
        return static_cast<std::uint32_t>(std::ceil(duration * sample_rate)) + 1;
    }

    AssetStatus validate_animation_sequence(const AnimationSequenceAsset& sequence)
    {
        const auto& data = sequence.data;
        if (!validate_skeleton_reference(data.skeleton, data.skeleton_reference_hash).succeeded() ||
            data.sample_count == 0 || data.sample_count != animation_sample_count(data.duration, data.sample_rate) ||
            data.track_count != sequence.tracks.size() || sequence.tracks.size() > max_skeleton_bones ||
            data.tracks_segment != "animation_tracks" ||
            sequence.tracks.size() > max_animation_samples / data.sample_count)
        {
            return invalid("invalid or oversized animation metadata");
        }
        std::set<std::uint32_t> bones;
        for (const AnimationTrack& track : sequence.tracks)
        {
            if (track.bone_index >= max_skeleton_bones || !bones.insert(track.bone_index).second ||
                track.samples.size() != data.sample_count)
            {
                return invalid("invalid animation bone mapping or sample count");
            }
            for (const Transform& sample : track.samples)
            {
                if (!validate_animation_transform(sample).succeeded())
                {
                    return invalid("invalid animation sample transform");
                }
            }
        }
        return AssetStatus::success();
    }

    AssetStatus validate_animation_compatibility(const AnimationSequenceAsset& sequence, const AssetId& skeleton_id,
                                                 const SkeletonAssetData& skeleton)
    {
        const auto hash = skeleton_reference_hash(skeleton);
        if (!validate_animation_sequence(sequence).succeeded() || !hash.succeeded() ||
            !(sequence.data.skeleton.asset_id == skeleton_id) || sequence.data.skeleton_reference_hash != hash.value())
        {
            return invalid("animation skeleton identity or reference contract mismatch");
        }
        for (const AnimationTrack& track : sequence.tracks)
        {
            if (track.bone_index >= skeleton.bones.size())
            {
                return invalid("animation track is outside skeleton");
            }
        }
        return AssetStatus::success();
    }

    AssetResult<std::vector<std::uint8_t>> encode_animation_tracks(const AnimationSequenceAsset& sequence)
    {
        const auto valid = validate_animation_sequence(sequence);
        if (!valid.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(valid);
        }
        ValueWriter writer;
        if (!writer.write_uint32(1).succeeded() || !writer.write_array_length(sequence.data.track_count).succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(invalid("animation header encoding failed"));
        }
        for (const AnimationTrack& track : sequence.tracks)
        {
            if (!writer.write_uint32(track.bone_index).succeeded() ||
                !writer.write_array_length(sequence.data.sample_count).succeeded())
            {
                return AssetResult<std::vector<std::uint8_t>>(invalid("animation track encoding failed"));
            }
            for (const Transform& sample : track.samples)
            {
                if (!encode_value(writer, sample).succeeded())
                {
                    return AssetResult<std::vector<std::uint8_t>>(invalid("animation sample encoding failed"));
                }
            }
        }
        return AssetResult<std::vector<std::uint8_t>>(writer.bytes());
    }

    AssetResult<AnimationSequenceAsset> decode_animation_tracks(const AnimationSequenceAssetData& data,
                                                                const std::vector<std::uint8_t>& bytes)
    {
        ValueReader reader(bytes);
        std::uint32_t version = 0;
        std::uint32_t count = 0;
        if (bytes.size() > ValueLimits{}.max_bytes || !reader.read_uint32(version).succeeded() || version != 1 ||
            !reader.read_array_length(count).succeeded() || count > max_skeleton_bones || count != data.track_count ||
            data.sample_count == 0 || data.sample_count != animation_sample_count(data.duration, data.sample_rate) ||
            count > max_animation_samples / data.sample_count)
        {
            return AssetResult<AnimationSequenceAsset>(invalid("invalid animation track header"));
        }
        AnimationSequenceAsset sequence;
        sequence.data = data;
        sequence.tracks.resize(count);
        for (AnimationTrack& track : sequence.tracks)
        {
            if (!reader.read_uint32(track.bone_index).succeeded() || !reader.read_array_length(count).succeeded() ||
                count != data.sample_count || count > (bytes.size() - reader.offset()) / (10 * sizeof(float)))
            {
                return AssetResult<AnimationSequenceAsset>(invalid("truncated animation track"));
            }
            track.samples.resize(count);
            for (Transform& sample : track.samples)
            {
                if (!decode_value(reader, sample).succeeded())
                {
                    return AssetResult<AnimationSequenceAsset>(invalid("invalid animation sample"));
                }
            }
        }
        if (!reader.at_end())
        {
            return AssetResult<AnimationSequenceAsset>(invalid("trailing animation data"));
        }
        const auto valid = validate_animation_sequence(sequence);
        return valid.succeeded() ? AssetResult<AnimationSequenceAsset>(std::move(sequence))
                                 : AssetResult<AnimationSequenceAsset>(valid);
    }

    AssetResult<AssetPairBytes> encode_skeleton_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                           const SkeletonAssetData& skeleton)
    {
        const auto valid = validate_skeleton(skeleton);
        if (!valid.succeeded())
        {
            return AssetResult<AssetPairBytes>(valid);
        }
        ValueWriter writer;
        if (!encode_value(writer, skeleton).succeeded())
        {
            return AssetResult<AssetPairBytes>(invalid("skeleton encoding failed"));
        }
        AssetFileIndex index;
        index.asset_id = id;
        index.root_type = "toy3d.SkeletonAssetData";
        index.schema_version = 1;
        return encode_asset_pair(types, std::move(index), writer.bytes(), {});
    }

    AssetResult<AssetPairBytes> encode_animation_sequence_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                                     const AnimationSequenceAsset& sequence)
    {
        const auto tracks = encode_animation_tracks(sequence);
        if (!tracks.succeeded())
        {
            return AssetResult<AssetPairBytes>(tracks.status());
        }
        ValueWriter writer;
        if (!encode_value(writer, sequence.data).succeeded())
        {
            return AssetResult<AssetPairBytes>(invalid("animation metadata encoding failed"));
        }
        AssetFileIndex index;
        index.asset_id = id;
        index.root_type = "toy3d.AnimationSequenceAssetData";
        index.schema_version = 1;
        index.dependencies.push_back(sequence.data.skeleton);
        return encode_asset_pair(types, std::move(index), writer.bytes(),
                                 {{"animation_tracks", 2, true, tracks.value()}});
    }

    AssetResult<SkeletonAssetData> decode_skeleton_asset_pair(const AssetPair& pair)
    {
        if (!valid_root(pair, "toy3d.SkeletonAssetData", false))
        {
            return AssetResult<SkeletonAssetData>(invalid("invalid skeleton asset type or schema"));
        }
        ValueReader reader(pair.description.type_data);
        SkeletonAssetData skeleton;
        if (!decode_value(reader, skeleton).succeeded() || !reader.at_end())
        {
            return AssetResult<SkeletonAssetData>(invalid("invalid skeleton data"));
        }
        const auto valid = validate_skeleton(skeleton);
        return valid.succeeded() ? AssetResult<SkeletonAssetData>(std::move(skeleton))
                                 : AssetResult<SkeletonAssetData>(valid);
    }

    AssetResult<AnimationSequenceAsset> decode_animation_sequence_asset_pair(const AssetPair& pair)
    {
        if (!valid_root(pair, "toy3d.AnimationSequenceAssetData", true))
        {
            return AssetResult<AnimationSequenceAsset>(invalid("invalid animation asset type or schema"));
        }
        ValueReader reader(pair.description.type_data);
        AnimationSequenceAssetData data;
        if (!decode_value(reader, data).succeeded() || !reader.at_end())
        {
            return AssetResult<AnimationSequenceAsset>(invalid("invalid animation metadata"));
        }
        const AssetSegmentData* tracks = nullptr;
        for (const auto& segment : pair.meta.segments)
        {
            if (segment.name == "animation_tracks" && segment.kind == 2 && segment.required)
            {
                tracks = &segment;
            }
            else if (segment.required)
            {
                return AssetResult<AnimationSequenceAsset>(invalid("unknown required animation segment"));
            }
        }
        if (!tracks)
        {
            return AssetResult<AnimationSequenceAsset>(invalid("missing animation tracks"));
        }
        return decode_animation_tracks(data, tracks->bytes);
    }
} // namespace toy3d
