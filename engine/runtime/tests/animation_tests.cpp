#include "animation/animation_player.h"
#include "gamescene/actor/skeletal_mesh_actor.h"
#include "gamescene/world/world.h"
#include "skeletal_mesh_test_utils.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "asset_pipeline/skeletal_mesh_builder.h"
#include "rendercore/geometry/skeletal_mesh_deformation.h"

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }

    toy3d::AssetId id(const char* text)
    {
        toy3d::AssetId result;
        check(toy3d::AssetId::parse(text, result), "fixture identity");
        return result;
    }

    toy3d::AssetResult<toy3d::SkeletalMeshDeformationData> deform(const toy3d::AnimationPose& local,
                                                                  const toy3d::SkeletalMeshAsset& mesh)
    {
        const auto component = toy3d::build_component_space_pose(local);
        check(component.succeeded(), "component pose");
        toy3d::SkeletalMeshDeformer deformer;
        check(deformer.set_mesh(local.bone_layout, mesh).succeeded(), "deformer binding");
        return deformer.evaluate({1, local, component.value()});
    }

    toy3d::AssetPair pair_from_bytes(const toy3d::TypeRegistry& types, const toy3d::AssetPairBytes& bytes)
    {
        const auto description = toy3d::decode_asset_yaml(types, bytes.asset);
        check(description.succeeded(), "description decode");
        toy3d::AssetPair pair;
        pair.description = description.value();
        if (bytes.has_meta)
        {
            const auto meta = toy3d::decode_asset_meta(bytes.meta);
            check(meta.succeeded(), "meta decode");
            pair.meta = meta.value();
        }
        return pair;
    }
    class SeekingActor final : public toy3d::Actor
    {
      public:
        explicit SeekingActor(toy3d::World& world) : Actor(world)
        {
            mesh = &create_component<toy3d::SkeletalMeshComponent>();
            set_root_component(mesh);
            set_tick_enabled(true);
        }
        toy3d::SkeletalMeshComponent* mesh = nullptr;
        bool seek_on_tick = false;
        toy3d::Actor* destroy_on_tick = nullptr;

      private:
        void tick(const toy3d::WorldTickContext&) override
        {
            if (seek_on_tick)
            {
                check(mesh->seek(0.5).succeeded(), "Actor tick seek");
            }
            if (destroy_on_tick)
            {
                check(world().destroy_actor(*destroy_on_tick), "Actor tick destruction");
            }
        }
    };

    void test_skeletal_components()
    {
        using namespace toy3d;
        const auto fixture = tests::make_skeletal_fixture(8);
        MaterialDesc desc;
        desc.shader_name = "ComponentCPUFixture";
        const auto material = MaterialInstance::create(Material::create(std::move(desc)));
        auto created = SkeletalMesh::create(fixture.layout, fixture.mesh, {material});
        check(created.succeeded(), "Runtime skeletal mesh candidate");
        SkeletalMeshDeformer shared_deformer;
        auto owned = SkeletalMesh::create(fixture.layout, fixture.mesh, {material});
        auto owner = std::move(owned).value();
        std::weak_ptr<const SkeletalMesh> lifetime = owner;
        check(shared_deformer.set_mesh(owner).succeeded(), "Validated immutable mesh binding");
        owner.reset();
        check(!lifetime.expired() && !shared_deformer.set_mesh(SkeletalMeshRef{}).succeeded(),
              "Deformer lost immutable mesh lifetime or accepted null binding");
        check(!SkeletalMesh::create(fixture.layout, fixture.mesh, {}).succeeded(), "Missing material rejected");
        World world;
        auto& first = world.spawn_actor<SeekingActor>();
        auto& second = world.spawn_actor<SkeletalMeshActor>();
        auto& a = *first.mesh;
        auto& b = second.skeletal_mesh_component();
        check(a.set_assets(created.value(), fixture.sequence).succeeded() &&
                  b.set_assets(created.value(), fixture.sequence).succeeded(),
              "Two components share immutable assets");
        check(!a.has_render_state() && a.animation_evaluation()->revision == a.deformation()->pose_revision,
              "Unbound component has CPU pose and matching bounds only");
        check(shared_deformer.evaluate(*a.animation_evaluation()).succeeded(),
              "Rejected replacement must preserve shared mesh binding");
        AnimationPlaybackSettings settings;
        settings.rate = 2;
        check(b.set_playback_settings(settings).succeeded(), "Independent playback rate");
        check(!world.tick(0.1), "World cannot advance animation before play");
        world.begin_play();
        const auto revision = world.content_revision();
        check(world.tick(0.1), "World animation stage");
        check(std::abs(a.playback_state()->time() - 0.1) < 1e-9 && std::abs(b.playback_state()->time() - 0.2) < 1e-9,
              "World advances each instance, including Actor with disabled gameplay tick");
        check(world.content_revision() == revision, "Playback must not dirty scene content");
        first.seek_on_tick = true;
        check(world.tick(0.1) && std::abs(a.playback_state()->time() - 0.6) < 1e-9,
              "Animation evaluates after Actor tick seek");
        first.seek_on_tick = false;
        check(a.set_playing(false).succeeded(), "Pause component");
        check(world.tick(0.1) && std::abs(a.playback_state()->time() - 0.6) < 1e-9,
              "Pause freezes time while other components play");
        check(a.is_tick_enabled() && a.set_playing(true).succeeded(), "Skeletal component opts into generic tick");
        a.set_tick_enabled(false);
        check(world.tick(0.1) && std::abs(a.playback_state()->time() - 0.6) < 1e-9,
              "Component tick opt-out stops automatic animation while playback remains enabled");
        a.set_tick_enabled(true);
        check(world.tick(0.1) && std::abs(a.playback_state()->time() - 0.7) < 1e-9,
              "Component tick opt-in resumes animation without catching up disabled time");
        const auto old_output = a.animation_evaluation();
        check(!a.seek(std::numeric_limits<double>::quiet_NaN()).succeeded() && a.animation_evaluation() == old_output,
              "Failed seek preserves pose and clock");
        settings.rate = std::numeric_limits<double>::infinity();
        check(!a.set_playback_settings(settings).succeeded() && a.animation_evaluation() == old_output,
              "Invalid settings preserve output");
        AssetId other_id;
        check(AssetId::parse("ffffffffffffffffffffffffffffffff", other_id), "Foreign skeleton ID");
        const auto foreign = std::make_shared<const AnimationBoneLayout>(other_id, fixture.layout->skeleton());
        AnimationSequenceAsset empty;
        empty.data.skeleton.asset_id = other_id;
        empty.data.skeleton_reference_hash = foreign->reference_hash();
        const auto wrong_sequence = std::make_shared<const AnimationSequence>(foreign, empty);
        check(!a.set_animation(wrong_sequence).succeeded() && a.animation_evaluation() == old_output,
              "Incompatible sequence cannot replace component animation");
        check(a.set_animation({}).succeeded() && !a.playback_state() &&
                  a.animation_evaluation()->local_pose.local_transforms[4].translation == Vector3(),
              "Reference-only component uses reference transforms");
        check(a.set_animation(fixture.sequence).succeeded() && a.seek(1).succeeded(),
              "Sequence replacement and end seek");
        check(a.deformation()->bounds_maximum.x > 1 && a.world_bounds().maximum.x > 1,
              "Final pose publishes conservative dynamic component bounds");
        const auto retained = a.animation_evaluation();
        check(a.seek(0).succeeded() && retained->local_pose.local_transforms[4].translation.x == 4,
              "Published evaluation survives subsequent mutation");
        check(a.set_assets({}, fixture.sequence).succeeded() == false && a.skeletal_mesh(),
              "Missing mesh cannot admit a sequence");
        first.destroy_on_tick = &second;
        const auto second_id = second.actor_id();
        check(world.tick(0.1) && !world.find_actor_by_id(second_id), "Pending destroy skips animation and unregisters");
        world.end_play();
        check(!world.tick(0.1), "End play stops World animation stage");
        check(a.set_assets({}).succeeded() && !a.skeletal_mesh() && !a.deformation(), "Explicit mesh clear");
    }

} // namespace

int main()
{
    using namespace toy3d;
    const auto skeleton_id = id("11111111111111111111111111111111");
    SkeletonAssetData skeleton;
    skeleton.bones = {{"root", -1, {}}, {"tip", 0, {Vector3(0, 1, 0), {}, Vector3(1, 1, 1)}}};
    check(validate_skeleton(skeleton).succeeded(), "two-bone skeleton");
    auto invalid = skeleton;
    invalid.bones[1].parent_index = 1;
    check(!validate_skeleton(invalid).succeeded(), "self-parent accepted");
    invalid = skeleton;
    invalid.bones[1].name = "root";
    check(!validate_skeleton(invalid).succeeded(), "duplicate name accepted");
    invalid = skeleton;
    invalid.bones[1].reference_local_transform.scale.x = -1;
    check(!validate_skeleton(invalid).succeeded(), "negative scale accepted");
    invalid = skeleton;
    invalid.bones[1].reference_local_transform.translation.y = 2;
    check(!validate_skeleton_compatibility(skeleton, invalid).succeeded(), "different proportions accepted");

    SkeletalMeshBuildInput input;
    input.mesh.vertices = {{Vector3(0, 0, 0), Vector3(0, 0, 1), {}},
                           {Vector3(1, 0, 0), Vector3(0, 0, 1), {}},
                           {Vector3(0, 2, 0), Vector3(0, 0, 1), {}}};
    input.mesh.indices = {0, 1, 2};
    input.mesh.sections = {{0, 3, 0}};
    input.mesh.material_slots = {"Test"};
    input.influences = {{{0, 1}}, {{0, 0.25f}, {0, 0.25f}, {1, 0.5f}}, {{1, 1}}};
    Matrix4 inverse_tip;
    check(try_inverse(to_matrix(skeleton.bones[1].reference_local_transform), inverse_tip), "inverse tip bind");
    input.inverse_bind_matrices = {Matrix4::identity(), inverse_tip};
    const auto built = build_skeletal_mesh(input, skeleton_id, skeleton);
    check(built.succeeded(), "build two-bone mesh");
    const auto& mesh = built.value().mesh;
    check(mesh.geometry.skin_weights[1].weights[0] + mesh.geometry.skin_weights[1].weights[1] == 255,
          "weight quantization");
    auto bad_mesh = mesh;
    bad_mesh.geometry.bone_local_bounds[1].maximum.y = -1;
    check(!validate_skeletal_mesh(bad_mesh).succeeded(), "nonconservative bounds accepted");
    bad_mesh = mesh;
    bad_mesh.geometry.inverse_bind_matrices[1] = Matrix4::identity();
    check(!validate_skeletal_mesh_compatibility(bad_mesh, skeleton_id, skeleton).succeeded(), "wrong bind accepted");

    AnimationSequenceAsset clip;
    clip.data.skeleton = mesh.data.skeleton;
    clip.data.skeleton_reference_hash = mesh.data.skeleton_reference_hash;
    clip.data.duration = 0.045;
    clip.data.sample_count = 3;
    clip.data.track_count = 1;
    AnimationTrack track;
    track.bone_index = 1;
    for (const float translation : {0.0f, 10.0f, 20.0f})
    {
        Transform transform = skeleton.bones[1].reference_local_transform;
        transform.translation.x = translation;
        track.samples.push_back(transform);
    }
    track.samples[1].rotation = -track.samples[0].rotation;
    clip.tracks = {track};
    const auto layout = std::make_shared<const AnimationBoneLayout>(skeleton_id, skeleton);
    const auto sampled = sample_animation_pose(layout, clip, (1.0 / 30.0 + 0.045) / 2);
    check(sampled.succeeded() && std::abs(sampled.value().local_transforms[1].translation.x - 15.0f) < 0.001f,
          "short final sample interval");
    check(sampled.value().local_transforms[0].translation == Vector3(), "missing track reference fallback");
    check(!sample_animation_pose(layout, clip, std::numeric_limits<double>::quiet_NaN()).succeeded(),
          "NaN time accepted");
    const auto end = sample_animation_pose(layout, clip, clip.data.duration);
    check(end.succeeded() && end.value().local_transforms[1].translation.x == 20, "exact clip end");
    const auto pose = deform(end.value(), mesh);
    check(pose.succeeded(), "evaluate component pose");
    const auto vertex = transform_position(pose.value().skin_matrices[1], Vector3(0, 2, 0));
    check(is_nearly_equal(vertex, Vector3(20, 2, 0)), "known tip skinned vertex");
    check(pose.value().bounds_maximum.x >= vertex.x && pose.value().bounds_maximum.y >= vertex.y, "dynamic bounds");
    const auto rows = build_bone_matrix_rows(pose.value(), {1, 0});
    check(rows.succeeded() && rows.value().size() == 12 && rows.value()[0].w == 20, "explicit section matrix rows");
    auto scaled_local = end.value();
    scaled_local.local_transforms[1].scale = Vector3(2, 3, 4);
    const auto scaled_pose = deform(scaled_local, mesh);
    check(scaled_pose.succeeded() && std::abs(scaled_pose.value().normal_matrices[1].at(0, 0) - 0.5f) < 0.001f,
          "nonuniform normal transform");

    AnimationPlayer player;
    auto shared_skeleton = std::make_shared<const SkeletonAssetData>(skeleton);
    auto shared_clip = std::make_shared<const AnimationSequenceAsset>(clip);
    check(player.set_assets(skeleton_id, shared_skeleton, shared_clip).succeeded(), "player assets");
    AnimationPlayer other;
    check(other.set_assets(skeleton_id, shared_skeleton, shared_clip).succeeded(), "second player assets");
    check(player.seek(clip.data.duration).succeeded() &&
              player.evaluate().value()->local_pose.local_transforms[1].translation.x == 20,
          "seek displays last frame");
    check(player.advance(0.01).succeeded() && std::abs(player.time() - 0.01) < 1e-10 && other.time() == 0,
          "independent loop clocks");
    player.pause();
    check(player.advance(1).succeeded() && std::abs(player.time() - 0.01) < 1e-10, "paused clock");
    check(player.set_settings({false, true, 1}).succeeded(), "nonloop settings");
    player.play();
    check(player.advance(1).succeeded() && player.time() == clip.data.duration && !player.playing(),
          "hold final nonloop pose");
    check(!player.set_settings({true, true, -1}).succeeded() && !player.settings().loop,
          "settings candidate preservation");
    auto incompatible = clip;
    incompatible.data.skeleton_reference_hash = std::string(64, '0');
    check(!player.set_assets(skeleton_id, shared_skeleton, std::make_shared<const AnimationSequenceAsset>(incompatible))
                  .succeeded() &&
              player.time() == clip.data.duration,
          "failed asset replacement preserves clock");
    auto zero = clip;
    zero.data.duration = 0;
    zero.data.sample_count = 1;
    zero.tracks[0].samples.resize(1);
    check(player.set_assets(skeleton_id, shared_skeleton, std::make_shared<const AnimationSequenceAsset>(zero))
                  .succeeded() &&
              !player.playing() && player.evaluate().succeeded(),
          "zero-duration clip");

    // Pose domains use identity and ordered reference data, not array length.
    check(layout->status().succeeded(), "valid bone domain");
    auto first = make_reference_pose(layout).value();
    auto second = end.value();
    second.local_transforms[1].rotation = -first.local_transforms[1].rotation;
    auto blended = blend_animation_poses(first, second, 0.25);
    check(blended.succeeded() && std::abs(blended.value().local_transforms[1].translation.x - 5) < 0.001f &&
              is_nearly_same_rotation(blended.value().local_transforms[1].rotation, Quaternion()),
          "crossfade and opposite quaternion representations");
    check(blend_animation_poses(first, second, 0).value().local_transforms[1].translation.x == 0 &&
              blend_animation_poses(first, second, 1).value().local_transforms[1].translation.x == 20,
          "crossfade endpoints");
    const auto three = blend_animation_poses(layout, {{&first, 1}, {&second, 1}, {&second, 2}});
    check(three.succeeded() && std::abs(three.value().local_transforms[1].translation.x - 15) < 0.001f,
          "N-way normalized blend");
    const auto huge_weights = blend_animation_poses(layout, {{&first, 1e308}, {&second, 1e308}});
    check(huge_weights.succeeded() && huge_weights.value().local_transforms[1].translation.x == 10,
          "large finite blend weights do not overflow");
    check(blend_animation_poses(layout, {{&second, 0}}).value().local_transforms[1].translation.x == 0 &&
              blend_animation_poses(layout, {}).value().local_transforms.size() == 2,
          "empty or zero contribution returns reference pose");
    check(!blend_animation_poses(layout, {{&first, -1}}).succeeded() &&
              !blend_animation_poses(first, second, std::numeric_limits<double>::quiet_NaN()).succeeded(),
          "invalid blend weights rejected");
    auto foreign = first;
    foreign.bone_layout = std::make_shared<const AnimationBoneLayout>(id("55555555555555555555555555555555"), skeleton);
    check(!blend_animation_poses(first, foreign, 0).succeeded(),
          "same count different asset rejected even at zero weight");
    auto different_reference = skeleton;
    different_reference.bones[1].reference_local_transform.translation.y = 2;
    foreign.bone_layout = std::make_shared<const AnimationBoneLayout>(skeleton_id, different_reference);
    check(!blend_animation_poses(first, foreign, 0.5).succeeded(), "same identity different reference rejected");
    foreign.bone_layout = std::make_shared<const AnimationBoneLayout>(skeleton_id, skeleton);
    check(blend_animation_poses(first, foreign, 0.5).succeeded(), "equivalent independently created layout accepted");
    foreign.local_transforms.clear();
    check(!build_component_space_pose(foreign).succeeded() && reset_to_reference_pose(foreign).succeeded() &&
              foreign.local_transforms.size() == 2,
          "reference reset repairs local array");
    auto mutable_skeleton = skeleton;
    auto detached_layout = std::make_shared<const AnimationBoneLayout>(skeleton_id, mutable_skeleton);
    mutable_skeleton.bones[1].reference_local_transform.translation.y = 99;
    check(detached_layout->skeleton().bones[1].reference_local_transform.translation.y == 1,
          "layout owns reference snapshot");
    invalid.bones[1].parent_index = 1;
    check(!make_reference_pose(std::make_shared<const AnimationBoneLayout>(skeleton_id, invalid)).succeeded(),
          "invalid layout rejected");

    // Weighted rotation is normalized linear blending; sample interpolation remains slerp.
    auto rotating = first;
    check(try_make_quaternion_from_axis_angle(Vector3(0, 0, 1), Radians(2.0f), rotating.local_transforms[1].rotation),
          "rotation fixture");
    const auto rotated = blend_animation_poses(first, rotating, 0.25);
    Quaternion expected_rotation;
    const auto& rq = rotating.local_transforms[1].rotation;
    check(
        try_normalize(Quaternion(rq.x * 0.25f, rq.y * 0.25f, rq.z * 0.25f, 0.75f + rq.w * 0.25f), expected_rotation) &&
            rotated.succeeded() &&
            is_nearly_same_rotation(expected_rotation, rotated.value().local_transforms[1].rotation),
        "weighted quaternion normalization");
    auto hierarchy = first;
    hierarchy.local_transforms[0].scale = Vector3(2, 3, 4);
    hierarchy.local_transforms[1].rotation = rq;
    const auto hierarchy_component = build_component_space_pose(hierarchy);
    check(hierarchy_component.succeeded() &&
              is_nearly_equal(hierarchy_component.value().bone_matrices[1],
                              to_matrix(hierarchy.local_transforms[0]) * to_matrix(hierarchy.local_transforms[1])),
          "nonuniform parent hierarchy preserves affine matrix");

    SequencePlaybackState clock;
    check(clock.initialize(0.5, {true, true, 2}).succeeded() && clock.advance(1.125).succeeded() &&
              std::abs(clock.time() - 0.25) < 1e-12 && clock.interval().loops_crossed == 4 &&
              clock.interval().advanced_time == 2.25 && !clock.interval().seeked,
          "multiple loop interval preserves elapsed time");
    check(clock.seek(0.5).succeeded() && clock.interval().seeked && clock.interval().advanced_time == 0 &&
              clock.interval().loops_crossed == 0 && clock.advance(0).succeeded() && clock.time() == 0.5 &&
              clock.advance(0.125).succeeded() && clock.time() == 0.25 && clock.interval().loops_crossed == 1,
          "seek end frame distinct from continuous wrap");
    const auto before_overflow = clock.time();
    check(!clock.advance(1e308).succeeded() && clock.time() == before_overflow, "clock overflow preserves state");
    clock.pause();
    check(clock.advance(1).succeeded() && clock.interval().previous_time == clock.interval().current_time &&
              clock.interval().advanced_time == 0,
          "paused interval is stationary");
    check(clock.initialize(0.5, {false, true, 2}).succeeded() && clock.advance(1).succeeded() && clock.time() == 0.5 &&
              clock.interval().advanced_time == 0.5 && !clock.playing() && clock.remaining_time() == 0,
          "nonloop interval clamps to duration");

    check(clock.initialize(0.1, {true, true, 1}).succeeded() && clock.seek(0.05).succeeded() &&
              clock.advance(1).succeeded() && clock.interval().loops_crossed == 10 &&
              std::abs(clock.time() - 0.05) < 1e-12,
          "decimal duration loop quotient agrees with remainder");
    const auto tiny_step = std::numeric_limits<double>::denorm_min();
    check(clock.initialize(0.045, {true, true, tiny_step}).succeeded() && clock.seek(0.045).succeeded() &&
              clock.advance(tiny_step).succeeded() && clock.time() == 0.045 && clock.playing() &&
              clock.interval().loops_crossed == 0 && clock.interval().advanced_time == 0 && !clock.interval().seeked,
          "underflowed playback increment preserves sought end frame");

    AnimationInstance instance;
    auto mutable_clip = std::make_shared<AnimationSequenceAsset>(clip);
    const auto bound_sequence = std::make_shared<const AnimationSequence>(layout, *mutable_clip);
    check(instance.set_sources(layout, {{bound_sequence, {false, false, 1}}, {bound_sequence, {false, false, 1}}})
              .succeeded(),
          "two nodes bind same clip");
    mutable_clip->tracks[0].samples.back().translation.x = 999;
    check(instance.seek(1, clip.data.duration).succeeded() && instance.update({0, {0.5, 0.5}, false}).succeeded(),
          "independent source clock selection");
    const auto snapshot = instance.evaluate();
    check(snapshot.succeeded() && snapshot.value()->local_pose.local_transforms[1].translation.x == 10 &&
              instance.playback_state(0)->time() == 0 && instance.playback_state(1)->time() == clip.data.duration,
          "sources use immutable copies and independent clocks");
    check(instance.evaluate().value() == snapshot.value() && instance.playback_state(1)->time() == clip.data.duration,
          "repeat evaluate reuses snapshot without advancing");
    SkeletalMeshDeformer bound_mesh;
    check(bound_mesh.set_mesh(layout, mesh).succeeded(), "mesh binding for final blended pose");
    const auto blended_mesh = bound_mesh.evaluate(*snapshot.value());
    check(blended_mesh.succeeded() && blended_mesh.value().pose_revision == snapshot.value()->revision &&
              blended_mesh.value().skin_matrices[1].at(3, 0) == 10 && blended_mesh.value().bounds_maximum.x >= 10,
          "skin and bounds use the same final blended revision");
    check(!bound_mesh.set_mesh(layout, bad_mesh).succeeded() && bound_mesh.evaluate(*snapshot.value()).succeeded(),
          "failed mesh replacement preserves binding");
    check(!instance.update({0.01, {0.5, -1}, false}).succeeded() && instance.evaluate().value() == snapshot.value() &&
              !instance.seek(5, 0).succeeded(),
          "invalid update and source preserve output");
    check(!instance.set_sources(layout, {{std::make_shared<const AnimationSequence>(layout, incompatible), {}}})
                  .succeeded() &&
              instance.evaluate().value() == snapshot.value(),
          "failed source replacement preserves cache");
    const auto foreign_layout =
        std::make_shared<const AnimationBoneLayout>(id("55555555555555555555555555555555"), skeleton);
    auto foreign_clip = clip;
    foreign_clip.data.skeleton.asset_id = foreign_layout->skeleton_id();
    const auto foreign_sequence = std::make_shared<const AnimationSequence>(foreign_layout, foreign_clip);
    check(foreign_sequence->status().succeeded() &&
              !instance.set_sources(layout, {{foreign_sequence, {}}}).succeeded() &&
              instance.evaluate().value() == snapshot.value(),
          "valid sequence from another skeleton cannot replace instance sources");
    const auto foreign_component = build_component_space_pose(make_reference_pose(foreign_layout).value());
    check(foreign_component.succeeded() && !bound_mesh.evaluate({1, {}, foreign_component.value()}).succeeded(),
          "deformer rejects foreign component pose with the same array size");
    check(instance.seek(1, 0).succeeded() && instance.evaluate().value()->revision > snapshot.value()->revision &&
              instance.evaluate().value()->local_pose.local_transforms[1].translation.x == 0 &&
              snapshot.value()->local_pose.local_transforms[1].translation.x == 10,
          "seek invalidates cache and preserves previously published snapshot");
    AnimationInstance independent_instance;
    check(independent_instance.set_sources(layout, {{bound_sequence, {false, true, 1}}}).succeeded() &&
              independent_instance.update({0.01, {1}, false}).succeeded() && instance.playback_state(0)->time() == 0,
          "instances share immutable sequence but not clocks");
    const auto before_failed_update = instance.evaluate().value();
    check(instance.set_playing(0, true).succeeded() && instance.set_playing(1, true).succeeded() &&
              instance.set_playback_settings(1, {false, true, 1e308}).succeeded() &&
              !instance.update({2, {0.5, 0.5}, false}).succeeded() && instance.playback_state(0)->time() == 0 &&
              instance.playback_state(1)->time() == 0 && instance.evaluate().value() == before_failed_update,
          "later source clock failure rolls back the complete update");
    const auto interrupted = blend_animation_poses(snapshot.value()->local_pose, first, 0.5);
    check(interrupted.succeeded() && interrupted.value().local_transforms[1].translation.x == 5,
          "retained mixed output supports interrupted transition blending");

    check(instance.set_sources(detached_layout, {}).succeeded() && instance.evaluate().succeeded() &&
              instance.evaluate().value()->local_pose.local_transforms[1].translation.y == 1,
          "reference-only instance and layout replacement");
    auto root_clip = clip;
    root_clip.tracks[0].bone_index = 0;
    for (auto& sample : root_clip.tracks[0].samples)
    {
        sample.translation.y = 0;
        sample.scale = Vector3(2, 2, 2);
    }
    check(instance.set_sources(layout,
                               {{std::make_shared<const AnimationSequence>(layout, root_clip), {false, false, 1}}})
                  .succeeded() &&
              instance.seek(0, root_clip.data.duration).succeeded() && instance.update({0, {1}, true}).succeeded(),
          "preview root policy after update");
    const auto locked = instance.evaluate().value();
    check(locked->local_pose.local_transforms[0].translation == Vector3() &&
              locked->local_pose.local_transforms[0].scale == Vector3(2, 2, 2) &&
              transform_position(locked->component_pose.bone_matrices[1], Vector3()).y == 2,
          "post-blend root lock preserves scale and precedes hierarchy");
    check(instance.update({0, {1}, false}).succeeded() &&
              instance.evaluate().value()->local_pose.local_transforms[0].translation.x == 20,
          "root lock does not modify sequence data");
    check(player.set_assets(skeleton_id, shared_skeleton, nullptr).succeeded() && player.evaluate().succeeded() &&
              !player.playing(),
          "single player reference-only mode");

    TypeRegistry types;
    check(register_animation_asset_types(types).succeeded() && types.freeze().succeeded(), "animation reflection");
    const auto skeleton_bytes = encode_skeleton_asset_pair(types, skeleton_id, skeleton);
    check(skeleton_bytes.succeeded() && !skeleton_bytes.value().has_meta, "skeleton YAML");
    check(decode_skeleton_asset_pair(pair_from_bytes(types, skeleton_bytes.value())).succeeded(), "skeleton roundtrip");
    const auto clip_bytes = encode_animation_sequence_asset_pair(types, id("22222222222222222222222222222222"), clip);
    if (!clip_bytes.succeeded())
    {
        std::cerr << clip_bytes.status().message << " path=" << clip_bytes.status().property_path << '\n';
    }
    check(clip_bytes.succeeded(), "clip encode");
    auto clip_pair = pair_from_bytes(types, clip_bytes.value());
    check(clip_pair.description.index.dependencies.size() == 1 &&
              decode_animation_sequence_asset_pair(clip_pair).succeeded(),
          "clip dependency and roundtrip");
    clip_pair.meta.segments[0].bytes.pop_back();
    check(!decode_animation_sequence_asset_pair(clip_pair).succeeded(), "truncated clip accepted");
    const auto mesh_bytes = encode_skeletal_mesh_asset_pair(types, id("33333333333333333333333333333333"), mesh);
    check(mesh_bytes.succeeded() &&
              decode_skeletal_mesh_asset_pair(pair_from_bytes(types, mesh_bytes.value())).succeeded(),
          "mesh pair roundtrip");

    // 257 one-bone triangles force a second draw without reducing the skeleton.
    SkeletonAssetData large_skeleton;
    large_skeleton.bones.push_back({"root", -1, {}});
    SkeletalMeshBuildInput large;
    large.mesh.material_slots = {"Test"};
    for (std::uint32_t bone = 0; bone < 257; ++bone)
    {
        if (bone != 0)
        {
            large_skeleton.bones.push_back({"bone" + std::to_string(bone), 0, {}});
        }
        large.inverse_bind_matrices.push_back(Matrix4::identity());
        const auto base = static_cast<std::uint32_t>(large.mesh.vertices.size());
        large.mesh.vertices.push_back({Vector3(0, 0, 0), Vector3(0, 0, 1), {}});
        large.mesh.vertices.push_back({Vector3(1, 0, 0), Vector3(0, 0, 1), {}});
        large.mesh.vertices.push_back({Vector3(0, 1, 0), Vector3(0, 0, 1), {}});
        for (std::uint32_t corner = 0; corner < 3; ++corner)
        {
            large.mesh.indices.push_back(base + corner);
            large.influences.push_back({{bone, 1}});
        }
    }
    large.mesh.sections = {{0, static_cast<std::uint32_t>(large.mesh.indices.size()), 0}};
    const auto split = build_skeletal_mesh(large, skeleton_id, large_skeleton);
    check(split.succeeded() && split.value().mesh.geometry.section_bone_maps.size() == 2 &&
              split.value().mesh.geometry.section_bone_maps[0].size() == 256,
          "256 to 257 section split");
    auto eight_skeleton = large_skeleton;
    eight_skeleton.bones.resize(9);
    auto eight_input = input;
    eight_input.inverse_bind_matrices.assign(9, Matrix4::identity());
    for (const std::uint32_t count : {4u, 5u, 8u})
    {
        eight_input.influences.assign(3, {});
        for (auto& influences : eight_input.influences)
        {
            for (std::uint32_t bone = 0; bone < count; ++bone)
            {
                influences.push_back({bone, 1.0f});
            }
        }
        const auto result = build_skeletal_mesh(eight_input, skeleton_id, eight_skeleton);
        check(result.succeeded(), "four/five/eight influences build without reduction");
        const auto& geometry = result.value().mesh.geometry;
        check(geometry.num_bone_influences == (count <= 4 ? 4u : 8u) && geometry.mesh.sections.size() == 1 &&
                  result.value().reduced_vertex_count == 0,
              "LOD chooses compact storage without adding draws");
        std::uint32_t sum = 0;
        for (const auto weight : geometry.skin_weights[0].weights)
        {
            sum += weight;
        }
        check(sum == 255 && geometry.skin_weights[0].weights[count - 1] != 0,
              "all retained influences contribute to exact quantization");
        const auto encoded = encode_skeletal_mesh_geometry(geometry);
        check(encoded.succeeded(), "four/eight payload encode");
        const auto decoded = decode_skeletal_mesh_geometry(encoded.value());
        check(decoded.succeeded() && decoded.value().num_bone_influences == geometry.num_bone_influences &&
                  decoded.value().skin_weights[0].weights == geometry.skin_weights[0].weights &&
                  decoded.value().skin_weights[0].bone_indices == geometry.skin_weights[0].bone_indices,
              "compact payload preserves both groups");
        auto corrupt = encoded.value();
        corrupt[4] = 5;
        check(!decode_skeletal_mesh_geometry(corrupt).succeeded(), "invalid storage width accepted");
        corrupt = encoded.value();
        corrupt[0] = 99;
        check(!decode_skeletal_mesh_geometry(corrupt).succeeded(), "unknown payload version accepted");
        corrupt = encoded.value();
        corrupt.resize(16);
        check(!decode_skeletal_mesh_geometry(corrupt).succeeded(), "truncated payload accepted");
        if (count == 4)
        {
            for (const auto old_version : {1u, 2u})
            {
                auto old = encoded.value();
                old[0] = static_cast<std::uint8_t>(old_version);
                check(!decode_skeletal_mesh_geometry(old).succeeded(), "old skeletal payload accepted");
            }
            auto tail = geometry;
            tail.skin_weights[0].weights[4] = 1;
            check(!validate_skeletal_mesh_geometry(tail).succeeded(), "nonzero inactive slot accepted");
        }
        if (count == 8)
        {
            auto bad_extra = geometry;
            bad_extra.skin_weights[0].bone_indices[7] = 255;
            check(!validate_skeletal_mesh_geometry(bad_extra).succeeded(), "invalid eighth index accepted");
            const auto asset_bytes =
                encode_skeletal_mesh_asset_pair(types, id("44444444444444444444444444444444"), result.value().mesh);
            check(asset_bytes.succeeded() &&
                      decode_skeletal_mesh_asset_pair(pair_from_bytes(types, asset_bytes.value()))
                              .value()
                              .geometry.num_bone_influences == 8,
                  "eight-slot mesh pair roundtrip");
            std::vector<Transform> local(9);
            local[7].translation.x = 100;
            const auto eight_layout = std::make_shared<const AnimationBoneLayout>(skeleton_id, eight_skeleton);
            const auto evaluated = deform({eight_layout, local}, result.value().mesh);
            check(evaluated.succeeded() && evaluated.value().bounds_maximum.x >= 100 &&
                      geometry.bone_local_bounds[7].influenced,
                  "eighth influence participates in conservative animated bounds");
        }
    }
    eight_input.influences.assign(3, {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1e-8f}});
    const auto tiny = build_skeletal_mesh(eight_input, skeleton_id, eight_skeleton);
    check(tiny.succeeded() && tiny.value().mesh.geometry.num_bone_influences == 4 &&
              tiny.value().mesh.geometry.skin_weights[0].weights[4] == 0 &&
              tiny.value().mesh.geometry.skin_weights[0].bone_indices[4] == 0,
          "quantized zero influences do not force eight-slot storage");
    large.influences[0] = {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1}};
    check(!build_skeletal_mesh(large, skeleton_id, large_skeleton).succeeded(),
          "implicit influence reduction accepted");
    const auto reduced = build_skeletal_mesh(large, skeleton_id, large_skeleton, {true});
    check(reduced.succeeded() && reduced.value().reduced_vertex_count == 1 &&
              std::abs(reduced.value().maximum_discarded_weight - 1.0f / 9.0f) < 0.001f,
          "explicit reduction diagnostic");
    test_skeletal_components();
    std::cout << "Animation asset, skin build, pose and clock tests passed\n";
    return 0;
}
