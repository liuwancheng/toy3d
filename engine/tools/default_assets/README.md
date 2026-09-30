# Engine default asset sources

`Toy3dDefaultAssets` imports the checked-in source images into seven Engine assets:
`T_White`, `T_Black`, `T_RedBrick`, `M_Default`, `M_White`, `M_Black`, and
`M_Brick`. The output is the ordinary `.asset`/`.meta` format, with fixed IDs
so material references stay stable. Run it against an empty staging directory
and copy the validated output to `engine/asset/`.

```powershell
cmake --build build --config Debug --target Toy3dDefaultAssets
build/engine/tools/default_assets/Debug/Toy3dDefaultAssets.exe engine/tools/default_assets/source build/default-assets-stage
```

The images were sourced, not painted by the asset tool. The two solid-color
PNG files are 4×4 raster copies of existing public-domain squares; the source
files provided by Wikimedia were rasterized to 20×20 by its thumbnail service
and resized to 4×4 without changing their colors. The brick image is an
unmodified 1K diffuse JPEG from Poly Haven.

| Input | Original source and license | SHA-256 of checked-in input |
| --- | --- | --- |
| `source/T_White.png` | [Solid white square](https://commons.wikimedia.org/wiki/File:White_square.svg), public domain | `D0FB55B22CF2CA0488497F2B25E99AE529ABA7FA8C57B3D7615EFD7B5C24746A` |
| `source/T_Black.png` | [Solid black](https://commons.wikimedia.org/wiki/File:Black_square.svg), public domain | `F75C8AF4AD31EEE3F6689C81126C676487BABB8722B361A7EB4EAFF5A2C3C1CD` |
| `source/T_RedBrick.jpg` | [Red Brick](https://polyhaven.com/a/red_brick) by Rob Tuytel, CC0 | `6DEA3A2B5A3185FF48D299D182C405A9F787129D79677792DB6DE4AFD81C47F6` |

The current Texture2D pipeline stores RGBA8 sRGB mipmaps. Only the brick
diffuse map is imported; normal and roughness maps are outside the current
runtime material capabilities.
