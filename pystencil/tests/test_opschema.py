"""The registry-driven schema engine's own semantics (a port of opSchema.js).

The fixture corpus (test_fixture_conformance.py) proves the verdicts; these pin the
engine rules the corpus cannot see from outside — null-as-absent, 3.0 as an integer,
the surface resolution (surfaceKeys / bulletVariants / surfaces) and the helpers.
"""

from __future__ import annotations

import unittest

from pystencil._opschema import Schema, SchemaError, load_registry, schema

S = schema("pystencil")


class EngineSemanticsTest(unittest.TestCase):
    def test_null_is_absent_but_still_an_unknown_key(self):
        entry = S.ops["rotate"]
        # null "times" = absent → the default applies.
        out = S.normalize(S.validate_action({"op": "rotate", "dir": "left", "times": None}, entry), entry)
        self.assertEqual(out, {"op": "rotate", "dir": "left", "times": 1})
        with self.assertRaises(SchemaError):
            S.validate_action({"op": "rotate", "dir": "left", "angle": None}, entry)

    def test_integer_means_no_fractional_part_and_never_a_bool(self):
        entry = S.ops["rotate"]
        S.validate_action({"op": "rotate", "dir": "left", "times": 3.0}, entry)
        for bad in (2.5, True, "2", float("inf")):
            with self.assertRaises(SchemaError):
                S.validate_action({"op": "rotate", "dir": "left", "times": bad}, entry)

    def test_messages_carry_the_surface_prefix_and_the_path(self):
        with self.assertRaises(SchemaError) as cm:
            S.validate_action({"op": "crop", "spec": {"x1": "10pt"}}, S.ops["crop"])
        self.assertTrue(str(cm.exception).startswith('invalid "crop" action: "x1" in spec must be'))
        with self.assertRaises(SchemaError) as cm:
            S.validate_ask({"question": "q", "options": [{"label": "a", "image": {"scanIndex": -1}}, {"label": "b"}]})
        self.assertIn('"scanIndex" in ask.options[0].image must be an integer >= 0', str(cm.exception))

    def test_crop_aspect_fold_is_validated_and_normalized(self):
        entry = S.ops["crop"]
        v = S.validate_action({"op": "crop", "spec": {"x1": "10%"}, "aspect": "3:4"}, entry)
        self.assertEqual(S.normalize(v, entry), {"op": "crop", "spec": {"x1": "10%", "aspect": "3:4"}})
        with self.assertRaises(SchemaError):
            S.validate_action({"op": "crop", "spec": {"aspect": "3:4"}, "aspect": "4:3"}, entry)

    def test_regex_dollar_anchors_at_the_very_end(self):
        # JS "$" never matches before a trailing newline; the port must not either.
        with self.assertRaises(SchemaError):
            S.validate_action({"op": "page", "format": "a4\n"}, S.ops["page"])

    def test_envelope(self):
        S.check_envelope([{"label": "x", "actions": []}], "variants")
        S.check_envelope([{"label": "x", "seed": 1}], "variants")  # allowUnknown on a variant
        for bad in ([{"label": 7}], [{"actions": [{}] * 17}], ["x"], [{}] * 9):
            with self.assertRaises(SchemaError):
                S.check_envelope(bad, "variants")

    def test_limits_resolve_dotted_names(self):
        self.assertEqual(S.limit("ask.label"), 80)
        self.assertEqual(S.limit(7), 7)
        with self.assertRaises(ValueError):
            S.limit("no.such")

    def test_opset_entry(self):
        self.assertEqual(S.opset_entry("extensionOpen", "page")["keys"]["format"]["required"], True)
        self.assertEqual(S.opset_entry("extensionOpen", "rotate")["id"], "rotate")
        self.assertEqual(S.opset_entry("extensionOpen", "blank"), "fail")
        self.assertIsNone(S.opset_entry("extensionOpen", "theme"))


class SurfaceResolutionTest(unittest.TestCase):
    def test_pystencil_membership_and_order(self):
        names = [e["name"] for e in S.entries]
        self.assertEqual(names, [
            "crop", "rotate", "filter", "layout", "formula", "page", "blank", "undo", "redo",
            "reset", "frame", "image", "save", "connect", "disconnect", "delete", "openUrl",
            "clear", "clearChat",
        ])
        self.assertIn("paste", S.forbidden)

    def test_surface_keys_bullets_and_flags_resolve_per_surface(self):
        cli = Schema(load_registry(), "cli")
        # Both consoles take crop.spec.album (surfaceKeys.cli / .pystencil); the editor doesn't.
        self.assertIn("album", cli.ops["crop"]["keys"]["spec"]["fields"])
        self.assertIn("album", S.ops["crop"]["keys"]["spec"]["fields"])
        self.assertNotIn("album", Schema(load_registry(), "browser").ops["crop"]["keys"]["spec"]["fields"])
        self.assertEqual(cli.ops["copy"]["keys"], {})
        # bulletVariants: a string replaces the bullet, an {addendum} rides beside it.
        self.assertIn("(the\n  console's /reset)", S.ops["reset"]["bullet"])
        self.assertIn('"album": true', S.ops["crop"]["addendum"])
        self.assertEqual(S.ops["redo"]["bullet"], None)
        ext = Schema(load_registry(), "extension")
        self.assertTrue(ext.ops["theme"]["flags"]["panelSettings"])
        self.assertEqual(ext.ops["theme"]["keys"]["mode"]["enum"], ["light", "dark", "system"])
        with self.assertRaises(ValueError):
            Schema(load_registry(), "toaster")


if __name__ == "__main__":
    unittest.main()
