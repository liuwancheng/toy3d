#pragma once

#include "asset_pipeline/skeletal_mesh_builder.h"
#include "animation/animation_sequence.h"

#include <cstdlib>
#include <iostream>

namespace toy3d::tests
{
    struct SkeletalMeshFixture
    {
        std::shared_ptr<const AnimationBoneLayout> layout;
        SkeletalMeshAsset mesh;
        std::shared_ptr<const AnimationSequence> sequence;
    };

    inline SkeletalMeshFixture make_skeletal_fixture(std::uint32_t influences, bool multiple_sections = false)
    {
        AssetId id;
        if (!AssetId::parse("12345678901234567890123456789012", id))
        {
            std::abort();
        }
        SkeletonAssetData skeleton;
        skeleton.bones.push_back({"root", -1, {}});
        for (int bone = 1; bone < 8; ++bone)
        {
            skeleton.bones.push_back({"bone_" + std::to_string(bone), 0, {}});
        }
        SkeletalMeshBuildInput input;
        input.mesh.vertices = {{Vector3(-3, -1, 2), Vector3(0, 0, -1), {}},
                               {Vector3(-1, -1, 2), Vector3(0, 0, -1), {}},
                               {Vector3(-2, 1, 2), Vector3(0, 0, -1), {}}};
        input.mesh.indices = {0, 1, 2};
        input.mesh.sections = {{0, 3, 0}};
        input.mesh.material_slots = {"Test"};
        input.inverse_bind_matrices.assign(8, Matrix4::identity());
        if (influences == 8)
        {
            input.influences.assign(3, {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}});
        }
        else
        {
            input.influences.assign(3, {{1, 1}});
        }
        if (multiple_sections)
        {
            // A second section uses an unanimated bone with a different draw-local map.
            // Its right-hand triangle must stay put when the first section moves.
            for (std::size_t vertex = 0; vertex < 3; ++vertex)
            {
                auto point = input.mesh.vertices[vertex];
                point.position.x += 3;
                input.mesh.vertices.push_back(point);
                input.influences.push_back({{0, 1}});
            }
            input.mesh.indices.insert(input.mesh.indices.end(), {3, 4, 5});
            input.mesh.sections.push_back({3, 3, 0});
        }
        const auto built = build_skeletal_mesh(input, id, skeleton);
        if (!built.succeeded())
        {
            std::cerr << built.status().message << '\n';
            std::abort();
        }
        SkeletalMeshFixture fixture;
        fixture.layout = std::make_shared<const AnimationBoneLayout>(id, skeleton);
        fixture.mesh = built.value().mesh;
        AnimationSequenceAsset clip;
        clip.data.skeleton = fixture.mesh.data.skeleton;
        clip.data.skeleton_reference_hash = fixture.mesh.data.skeleton_reference_hash;
        clip.data.duration = 1;
        clip.data.sample_rate = 30;
        clip.data.sample_count = 31;
        for (std::uint32_t bone = influences == 8 ? 4u : 1u; bone < (influences == 8 ? 8u : 2u); ++bone)
        {
            AnimationTrack track;
            track.bone_index = bone;
            for (std::uint32_t sample = 0; sample <= 30; ++sample)
            {
                Transform transform;
                transform.translation.x = (influences == 8 ? 4.0f : 2.0f) * static_cast<float>(sample) / 30.0f;
                track.samples.push_back(transform);
            }
            clip.tracks.push_back(std::move(track));
        }
        clip.data.track_count = static_cast<std::uint32_t>(clip.tracks.size());
        fixture.sequence = std::make_shared<const AnimationSequence>(fixture.layout, clip);
        if (!fixture.sequence->status().succeeded())
        {
            std::cerr << fixture.sequence->status().message << '\n';
            std::abort();
        }
        return fixture;
    }
} // namespace toy3d::tests
