# Kolom Khela — Android APK build

The original game source (`src/`) and root `CMakeLists.txt` are retained. Android packaging and GitHub Actions files were added.

## Build APK on GitHub

1. Upload/commit this whole folder's contents to the root of your `Kolom_Khela` GitHub repository.
2. Push the commit to the `main` branch.
3. Open the repository's **Actions** tab and select **Build Android APK**.
4. Open the completed workflow run and download the `KolomKhela-debug-apk` artifact.
5. Extract the downloaded ZIP; it contains `app-debug.apk`.

You can also start the workflow manually using **Actions → Build Android APK → Run workflow**.

## Notes

- This workflow currently builds `arm64-v8a` only, for most modern Android phones.
- It produces a debug APK for testing, not a Play Store-signed release.
- The game source code has not intentionally been edited.
- Android builds can still reveal toolchain or raylib/NDK compatibility issues; if the workflow fails, inspect the failed step's logs and adjust the Android CMake configuration as needed.
