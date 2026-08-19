#include "renderscene/resources/render_resource_cache.h"

#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::MeshRenderResourceVersionRef make_mesh(
        std::uint64_t id,
        std::uint64_t revision,
        float x_offset = 0.0f)
    {
        auto version = std::make_shared<toy3d::MeshRenderResourceVersion>();
        version->resource_id = toy3d::MeshRenderResourceId(id);
        version->revision = toy3d::RenderResourceRevision(revision);
        version->vertices = {
            {{-1.0f + x_offset, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{1.0f + x_offset, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{x_offset, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}};
        version->indices = std::vector<std::uint16_t>{0, 1, 2};
        version->sections.push_back({0, 3, 0});
        return version;
    }

    toy3d::MaterialRenderResourceVersionRef make_material(
        std::uint64_t id,
        std::uint64_t revision,
        std::string shader_name)
    {
        auto version = std::make_shared<toy3d::MaterialRenderResourceVersion>();
        version->resource_id = toy3d::MaterialRenderResourceId(id);
        version->revision = toy3d::RenderResourceRevision(revision);
        version->material.shader_name = std::move(shader_name);
        return version;
    }

    toy3d::TextureRenderResourceVersionRef make_texture(
        std::uint64_t id,
        std::uint64_t revision,
        toy3d::TextureColorSemantic semantic,
        std::uint8_t value)
    {
        auto version = std::make_shared<toy3d::TextureRenderResourceVersion>();
        version->resource_id = toy3d::TextureRenderResourceId(id);
        version->revision = toy3d::RenderResourceRevision(revision);
        version->width = 1;
        version->height = 1;
        version->color_semantic = semantic;
        version->rgba8_pixels = {value, value, value, 255};
        return version;
    }

    toy3d::RenderResourcePlaceholders make_placeholders()
    {
        toy3d::RenderResourcePlaceholders placeholders;
        placeholders.error_material = make_material(900, 1, "Builtin/Error");
        placeholders.checkerboard_texture = make_texture(
            901, 1, toy3d::TextureColorSemantic::Color, 255);
        placeholders.white_texture = make_texture(
            902, 1, toy3d::TextureColorSemantic::Linear, 255);
        placeholders.normal_texture = make_texture(
            903, 1, toy3d::TextureColorSemantic::Normal, 128);
        return placeholders;
    }
}

int main()
{
    using namespace toy3d;

    static_assert(!std::is_convertible<std::uint64_t, RenderResourceRevision>::value,
        "Resource revisions must not silently accept untyped integers");
    static_assert(!std::is_same<MeshRenderResourceId, MaterialRenderResourceId>::value,
        "Resource domains must keep distinct identities");

    RenderResourceCache invalid_cache({});
    check(!invalid_cache.is_valid(),
        "A cache without the mandatory placeholder set must be invalid");
    MeshRenderResourceUpdate invalid_cache_update;
    invalid_cache_update.resource_id = MeshRenderResourceId(1);
    invalid_cache_update.version = make_mesh(1, 1);
    check(invalid_cache.apply_updates({invalid_cache_update}).rejected_count == 1,
        "An invalid cache must reject Apply without retaining resource state");

    const RenderResourcePlaceholders placeholders = make_placeholders();
    RenderResourceCache cache(placeholders);
    check(cache.is_valid(),
        "A complete, semantically typed placeholder set must initialize the cache");

    const MeshRenderResourceVersionRef mesh_v1 = make_mesh(1, 1);
    const MaterialRenderResourceVersionRef material_v1 =
        make_material(2, 1, "Builtin/Surface/Phong");
    const TextureRenderResourceVersionRef texture_v1 =
        make_texture(3, 1, TextureColorSemantic::Color, 64);
    MeshRenderResourceUpdate mesh_update;
    mesh_update.resource_id = mesh_v1->resource_id;
    mesh_update.version = mesh_v1;
    MaterialRenderResourceUpdate material_update;
    material_update.resource_id = material_v1->resource_id;
    material_update.version = material_v1;
    TextureRenderResourceUpdate texture_update;
    texture_update.resource_id = texture_v1->resource_id;
    texture_update.version = texture_v1;

    const RenderResourceApplyResult initial_result = cache.apply_updates(
        {mesh_update, material_update, texture_update});
    check(initial_result.succeeded() && initial_result.applied_count == 3 &&
        cache.mesh_count() == 1 && cache.material_count() == 1 &&
        cache.texture_count() == 1,
        "A valid ordered update stream must publish all three immutable resource domains");
    const auto resolved_mesh = cache.resolve_mesh(MeshRenderResourceId(1));
    const auto resolved_material = cache.resolve_material(MaterialRenderResourceId(2));
    const auto resolved_texture = cache.resolve_texture(
        TextureRenderResourceId(3), TextureColorSemantic::Color);
    check(resolved_mesh.state == RenderResourceResolveState::Found &&
        resolved_mesh.version == mesh_v1 &&
        resolved_material.state == RenderResourceResolveState::Found &&
        resolved_material.version == material_v1 &&
        resolved_texture.state == RenderResourceResolveState::Found &&
        resolved_texture.version == texture_v1,
        "Resolve must return the exact immutable version retained by Apply");

    const RenderResourceApplyResult duplicate_result =
        cache.apply_updates({mesh_update});
    check(duplicate_result.succeeded() && duplicate_result.unchanged_count == 1 &&
        cache.resolve_mesh(MeshRenderResourceId(1)).version == mesh_v1,
        "The same revision and content must be an idempotent no-change update");

    MeshRenderResourceUpdate conflicting_update = mesh_update;
    conflicting_update.version = make_mesh(1, 1, 5.0f);
    const RenderResourceApplyResult conflict_result =
        cache.apply_updates({conflicting_update});
    check(!conflict_result.succeeded() && conflict_result.rejected_count == 1 &&
        conflict_result.diagnostics[0].error ==
            RenderResourceApplyError::RevisionConflict &&
        cache.resolve_mesh(MeshRenderResourceId(1)).version == mesh_v1,
        "Equal revisions with different content must be rejected atomically");

    const MeshRenderResourceVersionRef mesh_v2 = make_mesh(1, 2, 2.0f);
    MeshRenderResourceUpdate mesh_v2_update;
    mesh_v2_update.resource_id = mesh_v2->resource_id;
    mesh_v2_update.version = mesh_v2;
    check(cache.apply_updates({mesh_v2_update}).applied_count == 1 &&
        cache.resolve_mesh(MeshRenderResourceId(1)).version == mesh_v2,
        "A newer revision must replace latest without mutating the old version");
    check(mesh_v1->revision == RenderResourceRevision(1) &&
        mesh_v1->vertices[0].position.x == -1.0f,
        "Replacing latest must leave previously held immutable versions unchanged");

    const RenderResourceApplyResult stale_result = cache.apply_updates({mesh_update});
    check(!stale_result.succeeded() && stale_result.rejected_count == 1 &&
        stale_result.diagnostics[0].error == RenderResourceApplyError::StaleRevision &&
        cache.resolve_mesh(MeshRenderResourceId(1)).version == mesh_v2,
        "An older revision must be diagnosed and ignored");

    MaterialRenderResourceUpdate invalid_material_update;
    invalid_material_update.resource_id = MaterialRenderResourceId(4);
    invalid_material_update.version = make_material(5, 1, "Builtin/Other");
    TextureRenderResourceUpdate continuing_texture_update;
    continuing_texture_update.resource_id = TextureRenderResourceId(6);
    continuing_texture_update.version = make_texture(
        6, 1, TextureColorSemantic::Linear, 32);
    const RenderResourceApplyResult partial_result = cache.apply_updates(
        {invalid_material_update, continuing_texture_update});
    check(partial_result.rejected_count == 1 && partial_result.applied_count == 1 &&
        cache.resolve_texture(TextureRenderResourceId(6),
            TextureColorSemantic::Linear).state == RenderResourceResolveState::Found,
        "An invalid object update must not stop later resources in the same stream");

    MeshRenderResourceUpdate release_mesh;
    release_mesh.operation = RenderResourceUpdateOperation::Release;
    release_mesh.resource_id = MeshRenderResourceId(1);
    const MeshRenderResourceVersionRef held_mesh =
        cache.resolve_mesh(MeshRenderResourceId(1)).version;
    const RenderResourceApplyResult release_result =
        cache.apply_updates({release_mesh});
    check(release_result.succeeded() && release_result.released_count == 1 &&
        cache.resolve_mesh(MeshRenderResourceId(1)).state ==
            RenderResourceResolveState::Missing && held_mesh == mesh_v2,
        "Release must remove only latest while external prepared-frame references remain valid");
    const RenderResourceApplyResult repeated_release_result =
        cache.apply_updates({release_mesh});
    check(repeated_release_result.rejected_count == 1 &&
        repeated_release_result.diagnostics[0].error ==
            RenderResourceApplyError::UnknownRelease,
        "Releasing an unknown latest entry must be a protocol diagnostic");

    const auto missing_material = cache.resolve_material(MaterialRenderResourceId(999));
    const auto missing_color = cache.resolve_texture(
        TextureRenderResourceId(999), TextureColorSemantic::Color);
    const auto missing_linear = cache.resolve_texture(
        TextureRenderResourceId(999), TextureColorSemantic::Linear);
    const auto missing_normal = cache.resolve_texture(
        TextureRenderResourceId(999), TextureColorSemantic::Normal);
    check(missing_material.state == RenderResourceResolveState::Placeholder &&
        missing_material.version == placeholders.error_material &&
        missing_color.version == placeholders.checkerboard_texture &&
        missing_linear.version == placeholders.white_texture &&
        missing_normal.version == placeholders.normal_texture,
        "Material and Texture misses must select the confirmed semantic placeholders");
    check(cache.resolve_mesh(MeshRenderResourceId(999)).state ==
        RenderResourceResolveState::Missing,
        "A Mesh miss must remain empty so Prepare can skip and diagnose the Primitive");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " render resource cache check(s) failed.\n";
        return 1;
    }
    std::cout << "Render resource cache checks passed.\n";
    return 0;
}
