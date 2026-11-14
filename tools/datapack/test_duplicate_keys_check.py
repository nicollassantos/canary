"""Tests for duplicate_keys_check.py.

    python3 -I tools/datapack/test_duplicate_keys_check.py
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import duplicate_keys_check  # noqa: E402


def check(text):
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        return duplicate_keys_check.duplicates(path)
    finally:
        os.unlink(path)


class DuplicateKeysTest(unittest.TestCase):
    def test_repeated_key_is_reported_with_both_lines(self):
        found = check("T = {\n\t[12107] = { itemId = 2772 },\n\t[12107] = { itemId = 1662 },\n}\n")
        self.assertEqual(found, [(3, "12107", 2)])

    def test_same_key_in_different_tables_is_fine(self):
        self.assertEqual(check("A = { [1] = 1 }\nB = { [1] = 2 }\nC = { x = { [1] = 1 }, y = { [1] = 2 } }\n"), [])

    def test_nested_repeat_is_reported(self):
        self.assertEqual(check("T = {\n\t[5] = {\n\t\t[1] = 1,\n\t\t[1] = 2,\n\t},\n}\n"), [(4, "1", 3)])

    def test_comments_and_strings_are_ignored(self):
        text = "T = {\n\t--[[\n\t[5000] = {},]]\n\t[5000] = {}, -- [5000] = {}\n\ts = \"[5000] = {}\",\n}\n"
        self.assertEqual(check(text), [])

    def test_indexing_assignments_are_not_table_keys(self):
        self.assertEqual(check("local t = {}\nt[1] = 1\nt[1] = 2\nif t[1] == 2 then end\n"), [])


if __name__ == "__main__":
    unittest.main()
