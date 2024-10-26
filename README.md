



https://github.com/user-attachments/assets/70311b6e-9f32-4b9e-8007-4b362102b770

# GenoView

GenoView is a really basic example raylib application that can be used to view skeletal animation data in a way that is clear and highlights any artefacts. It uses a simple Deferred Renderer that supports shadow maps and Screen Space Ambient Occlusion, as well as a procedural grid shader as a texture. This makes common artefacts such as foot sliding and penetrations easy to see on a skinned character even on low-end devices, without the complexity of a full rendering engine.

Included are some simple scripts for exporting characters and animation data into a binary format that can be easily loaded by the application. These scripts are made for the Geno character from the following datasets:

* [LaFAN resolved](https://github.com/orangeduck/lafan1-resolved)
* [ZeroEGGS retargeted](https://github.com/orangeduck/zeroeggs-retarget)
* [Motorica retargeted](https://github.com/orangeduck/motorica-retarget)

However they can likely be adapted to new characters, or the normal raylib-supported file formats can be loaded too.

# Getting Started

Here are the steps to viewing any of the animation data linked above in this viewer.

1. Download the BVH files for the animation dataset you want to view.
2. Place any bvh files you want to view in the `resources` folder.
3. Edit the `bvh_files` variable in the `resources/export_animations.py` script to contain the bvh files you want to view - then run the `export_animations.py` script.
4. Edit the line in `genoview.c` where `testAnimation` is loaded to load the animation you want to view instead.
