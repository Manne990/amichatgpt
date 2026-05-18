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
        select_render = struct.unpack_from(">I", data, 26)[0]
        icon_type = data[48]
        drawer_data = struct.unpack_from(">I", data, 66)[0]
        tool_window = struct.unpack_from(">I", data, 70)[0]
        stack_size = struct.unpack_from(">I", data, 74)[0]

        self.assertEqual(magic, 0xE310)
        self.assertEqual(version, 1)
        self.assertGreater(width, 0)
        self.assertGreater(height, 0)
        self.assertLessEqual(width, 80)
        self.assertLessEqual(height, 80)
        self.assertEqual(select_render, 0)
        self.assertEqual(icon_type, 3)
        self.assertEqual(drawer_data, 0)
        self.assertEqual(tool_window, 0)
        self.assertGreaterEqual(stack_size, 4096)

        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("packaging/$(APP_NAME).info", makefile)
        self.assertIn("$(PACKAGE_DIR)/$(APP_NAME).info", makefile)

    def test_workbench_icon_images_are_present(self):
        data = (ROOT / "packaging" / "AmiChatGPT.info").read_bytes()
        image_header_offset = 78
        image_header_size = 20
        gadget_width, gadget_height = struct.unpack_from(">HH", data, 12)
        width, height, depth = struct.unpack_from(">HHH", data, image_header_offset + 4)
        row_words = (width + 15) // 16
        image_data_offset = image_header_offset + image_header_size
        image_data_size = row_words * 2 * height * depth
        selected_image_offset = image_data_offset + image_data_size

        self.assertEqual((width, height), (gadget_width, gadget_height))
        self.assertGreater(depth, 0)
        self.assertLessEqual(depth, 8)
        self.assertEqual(
            len(data[image_data_offset : image_data_offset + image_data_size]),
            image_data_size,
        )
        self.assertNotEqual(
            data[image_data_offset : image_data_offset + image_data_size],
            bytes(image_data_size),
        )
        row_bytes = row_words * 2
        image_data = data[image_data_offset : image_data_offset + image_data_size]
        occupied_x = []
        occupied_y = []
        for y in range(height):
            for plane in range(depth):
                row_offset = plane * row_bytes * height + y * row_bytes
                row = image_data[row_offset : row_offset + row_bytes]
                for x in range(width):
                    if row[x // 8] & (1 << (7 - (x % 8))):
                        occupied_x.append(x)
                        occupied_y.append(y)
        occupied_width = max(occupied_x) - min(occupied_x) + 1
        occupied_height = max(occupied_y) - min(occupied_y) + 1
        self.assertGreaterEqual(occupied_width, occupied_height * 3 // 2)
        self.assertLessEqual(occupied_width, 64)
        self.assertLessEqual(occupied_height, 32)

        select_render = struct.unpack_from(">I", data, 26)[0]
        if select_render == 0:
            self.assertEqual(selected_image_offset, len(data))
        else:
            self.assertLess(selected_image_offset + image_header_size, len(data))
            selected_width, selected_height, selected_depth = struct.unpack_from(
                ">HHH", data, selected_image_offset + 4
            )
            self.assertEqual((selected_width, selected_height, selected_depth), (width, height, depth))
            selected_data_offset = selected_image_offset + image_header_size
            selected_data = data[selected_data_offset : selected_data_offset + image_data_size]
            self.assertGreaterEqual(len(selected_data), image_data_size - 2)
            self.assertNotEqual(selected_data, bytes(len(selected_data)))

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
