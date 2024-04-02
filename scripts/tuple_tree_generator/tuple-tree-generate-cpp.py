#!/usr/bin/env python3
#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

import argparse
import logging
import shutil
import subprocess
from pathlib import Path

import yaml

from tuple_tree_generator import Schema, generate_cpp_headers

argparser = argparse.ArgumentParser()
argparser.add_argument("schema", help="YAML schema")
argparser.add_argument("output_dir", help="Output to this directory")
argparser.add_argument("--namespace", required=True, help="Base namespace for generated types")
argparser.add_argument("--root-type", required=True, help="Schema root type")
argparser.add_argument("--scalar-type", action="append", default=[], help="Scalar type")
argparser.add_argument("--include-path-prefix", required=True, help="Prefixed to include paths")
argparser.add_argument(
    "--tracking", action="store_true", help="Emits tracking sequences", default=False
)
argparser.add_argument(
    "--tracking-debug", action="store_true", help="Emits tracking debugging helpers", default=False
)


def is_clang_format_available():
    return shutil.which("clang-format")


def reformat(source: str) -> str:
    """Applies clang-format to the given source"""
    return subprocess.check_output(
        [
            "clang-format",
        ],
        input=source,
        encoding="utf-8",
    )


def main(args):
    with open(args.schema, encoding="utf-8") as f:
        raw_schema = yaml.safe_load(f)

    schema = Schema(raw_schema, args.namespace, args.scalar_type)
    sources = generate_cpp_headers(
        schema, args.root_type, args.include_path_prefix, args.tracking, args.tracking_debug
    )

    if is_clang_format_available():
        sources = {name: reformat(source) for name, source in sources.items()}
    else:
        logging.warning("Warning: clang-format is not available, cannot format source")

    output_dir = Path(args.output_dir)
    print("output_dir: %s", output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    for output_path, output_source in sources.items():
        print("  output_path: %s", output_path)
        full_output_path = output_dir / output_path
        # Ensure the containing directory is created
        full_output_path.parent.mkdir(parents=False, exist_ok=True)
        full_output_path.write_text(output_source)


if __name__ == "__main__":
    parsed_args = argparser.parse_args()
    main(parsed_args)
