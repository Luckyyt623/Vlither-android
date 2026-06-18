# Vlither — Android Port

Slither.io Vulkan client ported to Android.  
Requires **Android 8.0+ (API 26)** with **Vulkan support** (virtually all Android devices since 2017).

## Controls

| Action | Gesture |
|--------|---------|
| Steer snake | Touch & drag anywhere on screen |
| Boost | Touch the right 20% of screen |

## How the port works

The original codebase uses **GLFW** for windowing and input.  
On Android, GLFW is replaced by:

| Original file | Android replacement |
|---|---|
| `thermite/src/framework/twindow.c` | `twindow_android.c` — `ANativeWindow` + `ALooper` |
| `thermite/src/framework/tmouse.c` | stubbed in `tentry_android.c`, touch→mouse redirect |
| `thermite/src/framework/tkeyboard.c` | stubbed (no physical keyboard) |
| `thermite/src/graphics/tcontext.c` | `tcontext_android.c` — `vkCreateAndroidSurfaceKHR` |
| `thermite/src/core/tentry.c` | `tentry_android.c` — `android_main()` entry point |
| `app/src/imgui_setup.c` | `imgui_setup_android.c` — no GLFW ImGui backend |

All game logic, network, and Vulkan rendering code is **unchanged**.

## Building locally

Requirements: Android Studio, NDK 26.x, CMake 3.22+, Slang shader compiler.

```bash
# 1. Compile shaders
python3 build.py 2

# 2. Copy assets
cp app/res/shaders/bin/*.spv android/app/src/main/assets/shaders/
cp app/res/fonts/*.ttf        android/app/src/main/assets/fonts/
cp -r app/res/textures/.      android/app/src/main/assets/textures/

# 3. Build APK
cd android
./gradlew assembleDebug
# APK: android/app/build/outputs/apk/debug/app-debug.apk
```

## Building via GitHub Actions

Push to `master` — the workflow at `.github/workflows/android.yml` will:
1. Install NDK + Slang
2. Compile shaders
3. Copy assets
4. Build debug + release APKs
5. Upload as downloadable artifacts

To create a release with the APK attached, push a tag: `git tag v2.5 && git push --tags`

## Asset path mapping

On desktop, assets are loaded from relative paths (`app/res/...`).  
On Android, they are loaded from the APK's asset bundle via `AAssetManager`.  
The CMake build copies them to `android/app/src/main/assets/`.

The shader loader in `tcontext_android.c` reads from `assets/shaders/<name>`.  
The font loader in `imgui_setup_android.c` reads from `assets/fonts/<name>`.
