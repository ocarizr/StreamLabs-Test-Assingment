"""Blank-line policy for braced C++ control flow, complementing clang-format.

This lexical rule ignores literals, comments, and preprocessor definitions. It
operates on the project's braced control style; clang-tidy enforces the braces.
"""

from bisect import bisect_right
import re

LITERALS = re.compile(
    r'(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)")'
    r"|//[^\n]*|/\*.*?\*/"
    r'|(?<![\w])(?:u8|u|U|L)?(?:"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')',
    re.DOTALL,
)


def masked_source(source: str) -> str:
    """Preserve source offsets while hiding non-code tokens and directives."""
    masked = LITERALS.sub(lambda match: re.sub(r"[^\n]", " ", match[0]), source)
    lines = masked.splitlines(keepends=True)
    original = source.splitlines(keepends=True)

    # Mask directive continuations while preserving all source offsets.
    continuation = False

    for index, line in enumerate(lines):
        directive = continuation or line.lstrip().startswith("#")
        continuation = directive and original[index].rstrip().endswith("\\")

        if directive:
            lines[index] = re.sub(r"[^\n]", " ", line)

    return "".join(lines)


def missing_control_gaps(source: str) -> list[int]:
    """Return one-based closing lines that need a following empty line."""
    masked = masked_source(source)
    tokens = list(re.finditer(r"[A-Za-z_]\w*|[^\s]", masked))
    values = [token[0] for token in tokens]
    parentheses = []
    pairs = {}
    braces = []
    closed_braces = {}
    endings = []
    controls = {"if", "for", "while", "switch", "catch"}

    # Track delimiter pairs to identify complete control statements.
    for index, value in enumerate(values):
        if value == "(":
            parentheses.append(index)
        elif value == ")" and parentheses:
            opening = parentheses.pop()
            pairs[index] = opening

            if opening >= 2 and values[opening - 1] == "while":
                if (
                    closed_braces.get(opening - 2) == "do"
                    and values[index + 1 : index + 2] == [";"]
                ):
                    endings.append(index + 1)
        elif value == "{":
            previous = values[index - 1] if index else ""
            kind = previous if previous in ("else", "do", "try", "consteval") else ""

            if previous == ")" and index - 1 in pairs:
                opening = pairs[index - 1]
                keyword = values[opening - 1] if opening else ""

                if keyword == "constexpr" and opening >= 2:
                    keyword = values[opening - 2]

                kind = keyword if keyword in controls else ""

            braces.append(kind)
        elif value == "}" and braces:
            kind = braces.pop()
            closed_braces[index] = kind
            following = values[index + 1] if index + 1 < len(values) else ""

            if (
                kind
                and following not in ("else", "catch")
                and not (kind == "do" and following == "while")
            ):
                endings.append(index)

    # Map statement endings to source lines before checking the following gap.
    newlines = [match.start() for match in re.finditer("\n", source)]
    lines = source.splitlines()
    missing = set()

    for index in endings:
        closing_line = bisect_right(newlines, tokens[index].start())

        if closing_line + 1 >= len(lines) or not lines[closing_line + 1].strip():
            continue

        # No gap before an enclosing close, a declaration terminator, or EOF.
        next_line = lines[closing_line + 1].lstrip()

        if next_line.startswith(("}", ");", ";")):
            continue

        missing.add(closing_line + 1)

    return sorted(missing)


def with_control_gaps(source: str) -> str:
    """Insert required gaps without modifying any non-whitespace source text."""
    missing = set(missing_control_gaps(source))
    return "".join(
        line + ("\n" if index in missing else "")
        for index, line in enumerate(source.splitlines(keepends=True), 1)
    )
