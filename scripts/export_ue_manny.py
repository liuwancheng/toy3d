"""Run inside the installed UE editor through export-ue-manny.ps1; not system Python."""

import hashlib
import json
import os
from pathlib import Path

import unreal


OUTPUT = Path(os.environ["TOY3D_MANNY_EXPORT_OUTPUT"])
SOURCE = Path(os.environ["TOY3D_MANNY_EXPORT_SOURCE"])
PACKAGE = "/Game/Characters/Mannequins"
CLIPS = (
    "MM_Idle", "MM_Walk_Fwd", "MM_Walk_InPlace", "MM_Run_Fwd",
    "MM_Jump", "MM_Fall_Loop", "MM_Land", "MM_T_Pose",
)


def load(path, expected_type):
    asset = unreal.load_asset(path)
    if not isinstance(asset, expected_type):
        raise RuntimeError("Missing or wrong UE asset type: " + path)
    return asset


def transform_data(transform):
    t = transform.translation
    r = transform.rotation
    s = transform.scale3d
    return {"translation": [t.x, t.y, t.z], "rotation_xyzw": [r.x, r.y, r.z, r.w],
            "scale": [s.x, s.y, s.z]}


def write_json(name, data):
    (OUTPUT / name).write_text(json.dumps(data, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def export_fbx(asset, name, preview_mesh=False):
    options = unreal.FbxExportOption()
    options.set_editor_property("fbx_export_compatibility", unreal.FbxExportCompatibility.FBX_2013)
    options.set_editor_property("ascii", False)
    options.set_editor_property("force_front_x_axis", False)
    options.set_editor_property("level_of_detail", False)
    options.set_editor_property("collision", False)
    options.set_editor_property("vertex_color", True)
    options.set_editor_property("export_morph_targets", False)
    options.set_editor_property("export_preview_mesh", preview_mesh)
    options.set_editor_property("map_skeletal_motion_to_root", False)
    options.set_editor_property("bake_material_inputs", unreal.FbxMaterialBakeMode.DISABLED)
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(OUTPUT / (name + ".fbx"))
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.options = options
    task.exporter = unreal.AnimSequenceExporterFBX() if preview_mesh else unreal.SkeletalMeshExporterFBX()
    if not unreal.Exporter.run_asset_export_task(task):
        raise RuntimeError("FBX export failed: " + name + ": " + str(list(task.errors)))
    path = Path(task.filename)
    if not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError("Empty FBX export: " + name)
    unreal.log("MANNY_EXPORT_FILE " + name + " bytes=" + str(path.stat().st_size))
    return {"file": path.name, "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "ue_asset": asset.get_path_name()}


def main():
    mesh = load(PACKAGE + "/Meshes/SKM_Manny", unreal.SkeletalMesh)
    simple_mesh = load(PACKAGE + "/Meshes/SKM_Manny_Simple", unreal.SkeletalMesh)
    skeleton = mesh.get_editor_property("skeleton")
    reference = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    names = [str(name) for name in unreal.AnimPoseExtensions.get_bone_names(reference)]
    component = unreal.SkeletalMeshComponent()
    component.set_skeletal_mesh_asset(mesh)
    name_indices = {name: index for index, name in enumerate(names)}
    parents = []
    for name in names:
        parent = str(component.get_parent_bone(name))
        if parent == "None":
            if name != names[0]:
                raise RuntimeError("Missing parent for UE bone: " + name)
            parents.append(-1)
        else:
            parents.append(name_indices[parent])
    if not names or len(names) != len(parents):
        raise RuntimeError("Invalid UE reference skeleton")
    bones = [{"name": name, "parent_index": parents[index],
              "reference_local": transform_data(unreal.AnimPoseExtensions.get_bone_pose(
                  reference, name, unreal.AnimPoseSpaces.LOCAL))} for index, name in enumerate(names)]
    write_json("SK_Mannequin.reference.json", {
        "ue_asset": skeleton.get_path_name(), "coordinate_system": "UE left-handed, Z-up, centimeters",
        "space": "parent-local", "contains_virtual_bones": any(name.startswith("VB ") for name in names),
        "bones": bones,
    })
    records = [export_fbx(mesh, "SKM_Manny"), export_fbx(simple_mesh, "SKM_Manny_Simple")]
    clip_records = []
    for name in CLIPS:
        clip = load(PACKAGE + "/Animations/Manny/" + name, unreal.AnimSequence)
        # The temporary copy is only changed in memory; keep each FBX on the same mesh/bind skeleton.
        clip.set_preview_skeletal_mesh(mesh)
        record = export_fbx(clip, name, preview_mesh=True)
        duration = unreal.AnimationLibrary.get_sequence_length(clip)
        evaluation = unreal.AnimPoseEvaluationOptions()
        evaluation.set_editor_property("evaluation_type", unreal.AnimDataEvalType.SOURCE)
        evaluation.set_editor_property("should_retarget", False)
        evaluation.set_editor_property("extract_root_motion", False)
        evaluation.set_editor_property("incorporate_root_motion_into_pose", True)
        evaluation.set_editor_property("optional_skeletal_mesh", mesh)
        snapshots = []
        for time in [0.0, duration * 0.5, duration]:
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, time, evaluation)
            snapshots.append({"time": time, "local_transforms": [
                transform_data(unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL))
                for bone in names]})
        write_json(name + ".samples.json", {
            "ue_asset": clip.get_path_name(), "duration_seconds": duration,
            "evaluation": "UE source pose, no retarget, no root motion extraction",
            "bone_names": names, "snapshots": snapshots,
        })
        record["duration_seconds"] = duration
        record["ue_frame_count"] = unreal.AnimationLibrary.get_num_frames(clip)
        record["preview_mesh"] = mesh.get_path_name()
        clip_records.append(record)
    write_json("manifest.json", {
        "ue_version": unreal.SystemLibrary.get_engine_version(),
        "source_template": "Templates/TemplateResources/High/Characters/Content/Mannequins",
        "copyright": "Epic Games; exported for this repository owner's personal local tests",
        "fbx": {"version": "2013", "binary": True, "lod": 0,
                "force_front_x_axis": False, "morph_targets": False, "cloth": False,
                "animation_preview_mesh": "SKM_Manny", "material_baking": "Disabled"},
        "reference_bone_count": len(bones), "meshes": records, "animations": clip_records,
        "source_uasset_sha256": {
            str(path.relative_to(SOURCE)).replace("\\", "/"): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(SOURCE.rglob("*.uasset"))
            if path.stem in (*CLIPS, "SKM_Manny", "SKM_Manny_Simple", "SK_Mannequin")},
    })
    unreal.log("MANNY_EXPORT_SUCCESS bones=" + str(len(bones)) + " clips=" + str(len(clip_records)))


main()
