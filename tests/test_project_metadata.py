from pathlib import Path
import re
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ProjectMetadataTest(unittest.TestCase):
    def test_version_is_semver(self):
        version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")

    def test_requirements_describe_bridge_architecture(self):
        requirements = (ROOT / "REQUIREMENTS.md").read_text(encoding="utf-8")
        self.assertIn("AmiChatGPT -> ChatGPT64 bridge -> OpenAI API", requirements)
        self.assertIn("Workbench 3.0 or 3.1", requirements)

    def test_makefile_exposes_expected_targets(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        for target in ("host-test", "test", "amiga", "package", "adf"):
            self.assertRegex(makefile, rf"(^|\n){re.escape(target)}:")

    def test_workbench_icon_is_packaged(self):
        icon = ROOT / "packaging" / "AmiChatGPT.info"
        self.assertTrue(icon.exists())
        self.assertGreater(icon.stat().st_size, 0)

        data = icon.read_bytes()
        magic, version = struct.unpack_from(">HH", data, 0)
        width, height = struct.unpack_from(">HH", data, 12)
        icon_type = data[48]
        drawer_data = struct.unpack_from(">I", data, 66)[0]
        tool_window = struct.unpack_from(">I", data, 70)[0]
        stack_size = struct.unpack_from(">I", data, 74)[0]

        self.assertEqual(magic, 0xE310)
        self.assertEqual(version, 1)
        self.assertEqual((width, height), (62, 56))
        self.assertEqual(icon_type, 3)
        self.assertEqual(drawer_data, 0)
        self.assertEqual(tool_window, 0)
        self.assertGreaterEqual(stack_size, 32768)

        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("packaging/$(APP_NAME).info", makefile)
        self.assertIn("$(PACKAGE_DIR)/$(APP_NAME).info", makefile)

    def test_amiga_build_opens_visible_workbench_console(self):
        source = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("AMIGA_BUILD", makefile)
        self.assertIn("CON:40/40/560/145/AmiChatGPT/CLOSE/WAIT", source)

    def test_ci_build_script_sets_toolchain_path(self):
        script = (ROOT / "scripts" / "ci" / "build-amiga-package.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("/opt/amiga/bin", script)
        self.assertIn("/tools/bin", script)
        self.assertIn("m68k-amigaos-gcc", script)

    def test_user_facing_project_files_are_english(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        package_readme = (ROOT / "packaging" / "README.txt").read_text(encoding="utf-8")
        self.assertIn("Load in an Emulator", readme)
        self.assertIn("This is an early scaffold build", package_readme)


if __name__ == "__main__":
    unittest.main()
