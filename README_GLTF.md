# NifSkope glTF 2.0 Exporter v1.1

# glTF Import

glTF Import into NifSkope will be implemented in a subsequent release.

# glTF Export

glTF export is currently supported on static and skinned Starfield meshes. 

To view and export Starfield meshes, you must first:

1. Enable and add the path to your Starfield installation in Settings > Resources.
2. Add the Meshes archives or extracted folders containing `geometries` to Archives or Paths in Settings > Resources, under Starfield.

## Skinned meshes

### Pre-Export

If you do not desire the full posable skeleton, you may skip these steps.

If you do not paste in the COM skeleton, a flat skeleton will be reconstructed for you.
**Please note:** You will receive a warning after export that the skeleton has been reconstructed for you. This is fine. 

If you desire the full posable skeleton, and the mesh does not have a skeleton with a `COM` or `COM_Twin` NiNode in its NIF:

1. Open the skeleton.nif for that skinned mesh (e.g. `meshes\actors\human\characterassets\skeleton.nif`)
2. Copy (Ctrl-C) the `COM` NiNode in skeleton.nif
3. Paste (Ctrl-V) the entire `COM` branch onto the mesh NIF's root NiNode (0)
4. Export to glTF

### Pre-Blender Import

As of Exporter v1.1 you should **no longer need to uncheck "Guess Original Bind Pose"**. 

## Blender scripts

Blender scripts are provided for use with glTF exports. They may be opened and run from the Scripting tab inside Blender.

1. `gltf_lod_blender_script.py` is included in the `scripts` folder for managing LOD visibility in exported glTF.
