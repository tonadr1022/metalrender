# Agent Instructions

## Build and Run

To build and run the project, use the following commands.
These apps are in the apps directory. The vktest app uses the Vulkan backend, while the metalrender app uses the Metal backend. Any file in the apps/vktest directory is for vktest, and any file in the apps/metalrender directory is for metalrender.

### Configure

Use the `Debug` preset from `CMakePresets.json`:

```bash
cmake --preset Debug
```

### Build

Build the `Debug` configuration for the test Vulkan app:

```bash
cmake --build build/Debug --target vktest
```

Build the `Debug` configuration for the test Metal app:

```bash
cmake --build build/Debug --target metalrender
```

### Run

Run vktest app:

```bash
./build/Debug/bin/vktest
```

Run metalrender app:

```bash
./build/Debug/bin/metalrender
```
