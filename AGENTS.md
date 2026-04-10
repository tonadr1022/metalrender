# Agent Instructions

## Verify (single command)

From root:

```bash
./scripts/agent_verify.sh
```

Configures CMake, builds app, runs `teng-shaderc --all`.
Add `--format` to format.

### Target names

vktest
metalrender

### Run

```bash
./build/Debug/bin/<target_name>
```

### Shader compiler (single files or ad hoc)

`./build/Debug/bin/teng-shaderc (--all | <path/to/file.comp.hlsl> [...])`  
`--all` compiles every entry-point `*.vert|frag|comp|mesh|task.hlsl` under `resources/shaders/hlsl`.

### Validating HLSL changes

Run `teng-shaderc` on shaders you change; after editing a shared `.hlsli` / header include, use `--all` (as `agent_verify.sh` does) so dependents stay in sync.
