"""Behavioral examples of the extra control-block whitespace policy."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from control_spacing import missing_control_gaps, with_control_gaps


class ControlSpacingTests(unittest.TestCase):
    def test_if_else_chain_stays_together_and_final_block_gets_gap(self):
        text = 'if (ready) {\n    act();\n}\nelse {\n    wait();\n}\nnext();\n'
        self.assertEqual(missing_control_gaps(text), [6])
        self.assertIn('}\nelse', with_control_gaps(text))
        self.assertTrue(with_control_gaps(text).endswith('}\n\nnext();\n'))

    def test_for_while_and_if_constexpr_before_comments(self):
        for header in ('for (auto value : values)', 'while (ready)', 'if constexpr (enabled)'):
            with self.subTest(header=header):
                text = header + ' {\n    act();\n}\n// explanation\nnext();\n'
                self.assertEqual(missing_control_gaps(text), [3])

    def test_no_gap_before_enclosing_close(self):
        self.assertEqual(missing_control_gaps('void f()\n{\n    if (ready) {\n        act();\n    }\n}\n'), [])

    def test_do_while_gap_follows_the_condition(self):
        self.assertEqual(missing_control_gaps('do {\n    act();\n}\nwhile (ready);\nnext();\n'), [4])

    def test_try_catch_continuation_is_preserved(self):
        self.assertEqual(missing_control_gaps('try {\n    act();\n}\ncatch (const error&) {\n    report();\n}\nnext();\n'), [6])

    def test_comments_literals_raw_strings_and_macros_do_not_create_blocks(self):
        text = '#define ACTION() do { act(); } while (0)\n' + 'auto text = R"tag(if (x) {\n}\nnext();)tag";\n' + '// if (x) {\n// }\n' + 'const auto count = 1\'000;\n'
        self.assertEqual(missing_control_gaps(text), [])

    def test_existing_gap_is_preserved_and_fix_is_idempotent(self):
        text = 'if (ready) {\n    act();\n}\n\nnext();\n'
        self.assertEqual(with_control_gaps(text), text)
        self.assertEqual(with_control_gaps(with_control_gaps(text)), text)


if __name__ == '__main__':
    unittest.main()
