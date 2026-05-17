from pathlib import Path
import re
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

    def test_user_facing_project_files_are_english(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        package_readme = (ROOT / "packaging" / "README.txt").read_text(encoding="utf-8")
        self.assertIn("Load in an Emulator", readme)
        self.assertIn("This is an early scaffold build", package_readme)


if __name__ == "__main__":
    unittest.main()

