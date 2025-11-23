from pathlib import Path
import os
from typing import List
import subprocess
from argparse import ArgumentParser
from concurrent.futures import ProcessPoolExecutor

parser = ArgumentParser("compile_hlsl")
parser.add_argument(
    "-v", "--verbose", action="store_true", help="Enable verbose output."
)
args = parser.parse_args()

VERBOSE = args.verbose

OUT_SHADER_DIR = Path("resources/shader_out/metal")
if not OUT_SHADER_DIR.exists():
    os.makedirs(OUT_SHADER_DIR, exist_ok=True)

SRC_DIR = Path("resources/shaders/hlsl")


def run_command(commands: List[str]):
    """Executes shell commands and returns its output."""
    for command in commands:
        try:
            subprocess.run(
                command,
                shell=False,
                stdout=subprocess.PIPE,
            )
        except subprocess.CalledProcessError as e:
            return f"Error executing command: {e.stderr.strip()}"
    return commands


def run_cmds(commands: List[List[str]]):
    with ProcessPoolExecutor() as executor:
        executor.map(run_command, commands)
        if VERBOSE:
            for cmds in commands:
                for c in cmds:
                    print(" ".join(c))


def get_files_walk(path: Path, endswith: str = "") -> List[Path]:
    result = []
    for root, _, files in path.walk():
        for file in files:
            if endswith and file.endswith(endswith):
                result.append(Path(os.path.join(root, file)))
    return result


def shader_model_from_hlsl_path(path: Path):
    shader_model_prefix = ""
    shader_type = path.suffixes[0][1:]
    if shader_type == "vert":
        shader_model_prefix = "vs"
    if shader_type == "frag":
        shader_model_prefix = "ps"
    if shader_type == "mesh":
        shader_model_prefix = "ms"
    if shader_type == "task":
        shader_model_prefix = "as"
    return shader_model_prefix + "_6_7"


def get_args_forcompile_hlsl_to_dxil_or_spirv(
    path: Path, shader_model: str, is_spirv=False
) -> List[str]:
    extension = ".dxil" if not is_spirv else ".spirv"
    out_filepath = (OUT_SHADER_DIR / path.stem).with_suffix(
        path.suffixes[0] + extension
    )
    args = [
        "dxc",
        str(path),
        "-Fo",
        str(out_filepath),
        "-T",
        shader_model,
        "-E",
        "main",
        "-Zi",
        "-Qembed_debug",
        "-Qsource_in_debug_module",
    ]
    if is_spirv:
        args.append("-spirv")
    return args


def get_argscompile_dxil_to_metallib(path: Path, output_reflection=False):
    metallib_path = path.with_suffix(path.suffixes[0] + ".metallib")
    args = [
        "metal-shaderconverter",
        str(path),
        "-o",
        str(metallib_path),
    ]
    if output_reflection:
        args.append("--output-reflection-file")
        args.append(str(metallib_path.with_suffix(metallib_path.suffixes[0] + ".json")))
    return args


def main():
    files = get_files_walk(SRC_DIR, "hlsl")

    cmds = []
    for file in files:
        shader_model = shader_model_from_hlsl_path(file)
        cmds.append(
            [get_args_forcompile_hlsl_to_dxil_or_spirv(file, shader_model, False)]
        )
        cmds[-1].append(get_argscompile_dxil_to_metallib(Path(cmds[-1][0][3])))
        cmds.append(
            [get_args_forcompile_hlsl_to_dxil_or_spirv(file, shader_model, True)]
        )
    run_cmds(cmds)


if __name__ == "__main__":
    main()
