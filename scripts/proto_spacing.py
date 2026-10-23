"""Separate Protobuf message/enum declarations where clang-format 19 does not.

Mask comments and literals before matching adjacent declarations, then insert
only whitespace. A gap precedes any documentation attached to the next block.
"""

import re

from control_spacing import masked_source


def missing_proto_gaps(source: str) -> list[int]:
    """Return one-based closing lines lacking a gap before the next definition."""
    masked = masked_source(source)
    lines = source.splitlines()
    missing = []

    for match in re.finditer(r"}[ \t]*;?[ \t]*\n\s*(?=(?:message|enum)\b)", masked):
        closing_line = masked.count("\n", 0, match.start()) + 1

        if lines[closing_line].strip():
            missing.append(closing_line)

    return missing


def with_proto_gaps(source: str) -> str:
    """Add definition gaps without changing fields, comments or wire contracts."""
    missing = set(missing_proto_gaps(source))
    return "".join(
        line + ("\n" if index in missing else "")
        for index, line in enumerate(source.splitlines(keepends=True), 1)
    )
