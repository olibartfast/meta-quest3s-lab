# Repository Guidelines

## Project Structure & Module Organization

Repository-owned examples live in `apps/`, shared native C++17 components live
in `libs/`, and the preserved legacy baseline lives in `XrPassthrough/`.
Repository setup and device utilities live in `scripts/`. Keep generated
Gradle, CMake, shader, and APK output out of source directories and commits.

Agent-facing specifications live in `specs/`: `mission.md`, `tech-stack.md`, and
`roadmap.md` describe the product, the pinned toolchain, and the outstanding
work, and `specs/milestone-plans/` holds the per-milestone implementation plans.
Read `specs/` before starting a task. `docs/` holds technical reference and
measured results for humans — architecture, coordinate systems, per-feature
guides, and validation reports.

## Build, Test, and Development Commands

Run commands from the repository root unless noted:

- `./scripts/setup_quest_dev_env.sh` installs the expected Android SDK, NDK r29, Java 21, CMake, and Ninja tooling.
- `./scripts/udev_env_setup.sh` configures Linux USB access for Meta Quest devices.
- `cd XrPassthrough/Projects/Android && ./gradlew assembleDebug` builds the ARM64 debug APK.
- `./scripts/build_deploy.sh --app 02-vulkan-stereo-triangle --build-only`
  builds the repository-owned stereo renderer.
- `cd XrPassthrough/Projects/Android && ./gradlew clean` removes generated Android build output.
- `adb install -r XrPassthrough/Projects/Android/build/outputs/apk/debug/XrPassthrough-debug.apk` installs the APK on a connected headset.
- `adb logcat -s XrPassthrough` shows application logs during runtime validation.

Verify device authorization first with `adb devices`.

## Documentation Sources

Use Meta's [Native & OpenXR LLM documentation index](https://developers.meta.com/horizon/llmstxt/documentation/native/llms.txt/) as the primary Meta-specific source for Quest platform integration, lifecycle, deployment, passthrough, debugging, and performance guidance. Use the Khronos OpenXR specification for portable API semantics. Prefer current pages from these sources over remembered SDK behavior or deprecated Oculus/VrApi material.

## Coding Style & Naming Conventions

Match the existing Meta sample style: four-space indentation, opening braces on the same line for functions and control flow, and `#pragma once` in headers. Use `PascalCase` for types and methods, descriptive `camelCase` for local variables and parameters, and uppercase names for constants or macros. Keep platform-specific code behind existing `ANDROID`/OpenXR preprocessor guards. There is no repository-wide formatter; avoid unrelated reformatting and compile with the warning set documented in `.vscode/settings.json`.

## Testing Guidelines

No automated test framework or coverage threshold is currently configured. Every change must at least pass `./gradlew assembleDebug`. For rendering, input, lifecycle, or passthrough changes, install on a Quest 3/3S, exercise the affected behavior, and inspect `adb logcat` for OpenXR or Android errors. Add future tests in a clearly named `tests/` directory and use behavior-focused names such as `PassthroughLifecycleTest`.

## Commit & Pull Request Guidelines

Recent history uses short, descriptive, sentence-style subjects such as `Readme updated`; prefer a more specific imperative subject, for example `Fix passthrough session cleanup`. Keep each commit focused. Pull requests should explain the change and validation performed, link relevant issues, and include headset screenshots or recordings for visual changes. Call out SDK, NDK, manifest, permission, or device compatibility changes explicitly.
