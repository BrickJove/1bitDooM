# 1bitDooM

Windows builds: see the **Actions** tab → latest "Windows build" run → Artifacts. The original UZDoom readme is kept as `README_UZDOOM.md`.

An **unofficial fork of [UZDoom](https://github.com/UZDoom/UZDoom)** (itself derived from GZDoom/ZDoom), made for 1-bit style mods.
It is **not** the official UZDoom and is not affiliated with or endorsed by the UZDoom team.

## What is different

- **MODELDEF `Outline <width> [r g b]`** – draws an automatic inverted-hull outline around 3D models
  (world and HUD models), so mods no longer need to ship a second, enlarged, inverted black copy of every model.
  Supported on Vulkan and OpenGL; no effect on GLES.

```
Model test
{
    Path "models"
    Model 0 "test.md3"
    Skin 0 "BGW.png"
    FORCECULLBACKFACES
    Outline 0.5          // black, 0.5 map units; optional colour: Outline 0.5 255 0 0
    FrameIndex JPHO A 0 0
}
```

## AI disclosure

The code changes in this fork were written with the help of an AI assistant (Claude, by Anthropic) and reviewed/tested by the maintainer.
Because UZDoom does not accept AI-generated contributions, this work is **not** submitted upstream.

## License

GPL-3.0 like UZDoom; see the original license files in this repository. Source for any distributed binary is this repository.
Original credits go to the UZDoom, GZDoom and ZDoom authors.

Building: see the [UZDoom build instructions](https://github.com/UZDoom/UZDoom/wiki).
