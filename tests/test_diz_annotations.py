"""Contracts that keep disassembly annotations independent of execution."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / "tools/diz_annotations.py"
SPEC = importlib.util.spec_from_file_location("diz_annotations", TOOL)
diz = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(diz)


class DizAnnotationsTests(unittest.TestCase):
    def test_compression_preserves_unreached_and_cpu_widths(self):
        records = list(diz.rows("\nversion:201,compress_groupblocks,compress_table_1\nr 9 U\n+D06\n.CZZ\n; offset comment\n"))
        self.assertEqual(records, [("U00000000", 9), ("+D0600000", 1), (".CC7E0000", 1)])

    def test_copier_header_and_wrong_rom_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "test.smc"
            path.write_bytes(b"h" * 512 + b"r" * 0x8000)
            self.assertEqual(diz.clean_rom(path), b"r" * 0x8000)
        with self.assertRaisesRegex(ValueError, "SHA-1 mismatch"):
            diz.verify_rom(b"wrong ROM", diz.GAMES["x1"][3])

    def test_lorom_aliases_never_turn_wram_into_rom(self):
        self.assertEqual(diz.offset_for(0x808007, 0x180000), 7)
        self.assertEqual(diz.offset_for(0x008007, 0x180000), 7)
        self.assertIsNone(diz.offset_for(0x7E8007, 0x180000))
        self.assertIsNone(diz.offset_for(0xC08007, 0x180000))

    def test_imported_labels_do_not_promote_data_or_override_project_names(self):
        labels = [
            {"address": "808007", "name": "Reset", "kind": "opcode"},
            {"address": "818000", "name": "Player", "kind": "opcode"},
            {"address": "818010", "name": "Unknown", "kind": "unknown"},
            {"address": "818020", "name": "Sprites", "kind": "data8"},
            {"address": "7E0BCF", "name": "Health", "kind": "ram"},
        ]
        overlay = diz.symbol_overlay(labels, {0x008007: "ReviewedReset", 0x808007: "ReviewedReset"})
        self.assertEqual(overlay, ["symbol 018000 diz_Player_mirror01", "symbol 818000 diz_Player"])
        self.assertTrue(all(line.startswith("symbol ") for line in overlay))

    def test_duplicate_names_are_disambiguated_by_address(self):
        labels = [{"address": address, "name": "Actor", "kind": "opcode"}
                  for address in ("818000", "818010")]
        symbols = [line.split()[2] for line in diz.symbol_overlay(labels, {})]
        self.assertEqual(len(symbols), len(set(symbols)))

    def test_reviewed_tools_protect_undeclared_names_and_generated_code_does_not(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for directory in ("src/gen", "tools", "recomp"):
                (root / directory).mkdir(parents=True)
            (root / "recomp/bank00.cfg").write_text("bank = 0\n")
            (root / "tools/apply_overrides.py").write_text('target = "bank_00_DC36_M1X1"\nTARGETS = {0x049b03}\n')
            (root / "src/gen/generated.c").write_text("bank_00_DCDB_M1X1(cpu);\n")
            previous = diz.ROOT
            try:
                diz.ROOT = root
                names = diz.project_names(root / "recomp")
            finally:
                diz.ROOT = previous
            self.assertEqual(names, {0x00DC36: "bank_00_DC36", 0x80DC36: "bank_80_DC36",
                                     0x049B03: "bank_04_9B03", 0x849B03: "bank_84_9B03"})

    def test_instruction_disagreement_is_reported_not_used_as_a_width_override(self):
        table = {0xA9: ("LDA", None, lambda m, x: 2 if m else 3)}
        records = ["+C0000000", ".C0000000", ".C0000000"]
        instructions, mismatches = diz.instruction_rows(records, b"\xa9\x01\x00", table)
        self.assertEqual(mismatches, ["008000"])
        self.assertEqual(instructions[0][-1], 0)

    def test_generated_annotations_are_idempotent_and_preserve_every_c_token(self):
        source = "  cpu_trace_block(cpu, 0x018000);\n  value = cpu_read8(cpu, 0x7E, (uint16)0x0BCF);\n"
        labels = [{"address": "818000", "name": "Player", "kind": "opcode"},
                  {"address": "7E0BCF", "name": "Health", "kind": "ram"}]
        annotated, hits = diz.annotate_text(source, labels)
        self.assertEqual(hits, 2)
        self.assertIn("$018000=Player", annotated)
        self.assertIn("$7E0BCF=Health", annotated)
        self.assertIn("  cpu_trace_block(cpu, 0x018000);\n", annotated)
        self.assertIn("  value = cpu_read8(cpu, 0x7E, (uint16)0x0BCF);\n", annotated)
        self.assertEqual(diz.C_ANNOTATION.sub("", annotated), source)
        self.assertEqual(diz.annotate_text(annotated, labels)[0], annotated)

    def test_overlay_removal_preserves_reviewed_directives(self):
        source = "bank = 0\nfunc Reset 8000\n\n" + diz.BEGIN + "\nsymbol 008000 diz_Reset\n" + diz.END + "\n"
        self.assertEqual(diz.strip_overlay(source), "bank = 0\nfunc Reset 8000\n")
        with self.assertRaisesRegex(ValueError, "Malformed"):
            diz.strip_overlay(source + diz.BEGIN)


if __name__ == "__main__":
    unittest.main()
