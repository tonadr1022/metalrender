import os
import re

SRC_DIR = "src"
CONFIG_INCLUDE = '#include "core/Config.hpp"'
NAMESPACE_START = "namespace TENG_NAMESPACE {"
NAMESPACE_END = "} // namespace TENG_NAMESPACE"
EXCLUDE_FILE = "src/core/Config.hpp"


def process_file(filepath):
    if filepath.endswith(EXCLUDE_FILE):
        return

    with open(filepath, "r") as f:
        lines = f.readlines()

    # Check if already processed
    content = "".join(lines)
    if "namespace TENG_NAMESPACE" in content:
        print(f"Skipping {filepath}: Already has namespace")
        return

    # Find insertion point (after last include)
    last_include_idx = -1
    for i, line in enumerate(lines):
        if re.match(r"\s*#\s*include", line):
            last_include_idx = i

    # Check if config needs to be added
    has_config = False
    for line in lines:
        if '"core/Config.hpp"' in line:
            has_config = True
            break

    new_lines = []

    # If no includes, try to put after pragma once
    insert_idx = last_include_idx + 1

    if last_include_idx == -1:
        # Check for pragma once
        for i, line in enumerate(lines):
            if re.match(r"\s*#\s*pragma\s+once", line):
                insert_idx = i + 1
                break
        else:
            insert_idx = 0

    # Construct new content
    # 1. Header part (up to insert_idx)
    new_lines.extend(lines[:insert_idx])

    # 2. Add Config include if missing and strictly BEFORE namespace
    if not has_config:
        if new_lines and not new_lines[-1].strip() == "":
            new_lines.append("\n")
        new_lines.append(f"{CONFIG_INCLUDE}\n")

    # 3. Add whitespace separation
    if new_lines and not new_lines[-1].strip() == "":
        new_lines.append("\n")

    # 4. Start Namespace
    new_lines.append(f"{NAMESPACE_START}\n")

    # 5. Rest of file
    new_lines.extend(lines[insert_idx:])

    # 6. End Namespace (ensure newline before if needed)
    if lines and not lines[-1].endswith("\n"):
        new_lines.append("\n")
    new_lines.append(f"\n{NAMESPACE_END}\n")

    with open(filepath, "w") as f:
        f.writelines(new_lines)
    print(f"Processed {filepath}")


def main():
    for root, dirs, files in os.walk(SRC_DIR):
        for file in files:
            if file.endswith((".hpp", ".cpp", ".mm")):
                full_path = os.path.join(root, file)
                # Ensure we don't process the config file itself
                if os.path.abspath(full_path) == os.path.abspath(EXCLUDE_FILE):
                    continue
                process_file(full_path)


if __name__ == "__main__":
    main()
