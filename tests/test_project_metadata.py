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
        self.assertIn("third_party/textfield/include", makefile)

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

    def test_textfield_gadget_runtime_is_packaged(self):
        gadget = ROOT / "packaging" / "Gadgets" / "textfield.gadget"
        license_file = ROOT / "packaging" / "ThirdParty" / "textfield-license.txt"
        vendored_header = ROOT / "third_party" / "textfield" / "include" / "gadgets" / "textfield.h"

        self.assertTrue(gadget.exists())
        self.assertGreater(gadget.stat().st_size, 0)
        self.assertTrue(license_file.exists())
        self.assertIn("Copyright", license_file.read_text(encoding="latin-1"))
        self.assertTrue(vendored_header.exists())

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

    def test_amiga_build_uses_native_workbench_gui(self):
        source = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("AMIGA_BUILD", makefile)
        self.assertIn("OpenWindowTags", source)
        self.assertIn("CreateGadget", source)
        self.assertIn("textfield.gadget", source)
        self.assertIn("TEXTFIELD_MaxSize", source)
        self.assertIn("INPUT_TEXT_LEN 2048", source)
        self.assertIn("LISTVIEW_KIND", source)
        self.assertIn("BUTTON_KIND", source)
        self.assertIn("IDCMP_VANILLAKEY", source)
        self.assertIn("IDCMP_RAWKEY", source)
        self.assertIn("IDCMP_GADGETDOWN", source)
        self.assertIn("IDCMP_MOUSEBUTTONS", source)
        self.assertIn("WA_SizeGadget", source)
        self.assertIn("WA_MaxWidth", source)
        self.assertIn("WA_MaxHeight", source)
        self.assertIn("BorderLeft", source)
        self.assertIn("BorderBottom", source)
        self.assertIn("IDCMP_NEWSIZE", source)
        self.assertIn("struct AppLayout", source)
        self.assertIn("compute_app_layout", source)
        self.assertIn("layout_gadgets", source)
        self.assertIn("add_app_gadgets", source)
        self.assertIn("#define TRANSCRIPT_INPUT_GAP 12", source)
        self.assertIn("rebuild_gadtools_gadgets", source)
        self.assertIn("RemoveGList(ui->window, ui->gadgets, -1)", source)
        self.assertIn("FreeGadgets(ui->gadgets)", source)
        self.assertNotIn("SEND_BUTTON_RIGHT_INSET", source)
        self.assertNotIn("texteditor.gadget", source)
        self.assertNotIn("WA_Gadgets", source)

    def test_configuration_milestone_is_implemented(self):
        source = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        package_readme = (ROOT / "packaging" / "README.txt").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

        self.assertIn("struct BridgeConfig", source)
        self.assertIn("config_load_file", source)
        self.assertIn("PROGDIR:AmiChatGPT.conf", source)
        self.assertIn("config_load_tooltypes", source)
        self.assertIn("struct WBStartup", source)
        self.assertIn("GetDiskObject", source)
        self.assertIn("config_load_cli_args", source)
        self.assertIn("HOST", source)
        self.assertIn("PORT", source)
        self.assertIn("WIDTH", source)
        self.assertIn('DEFAULT_BRIDGE_HOST "127.0.0.1"', source)
        self.assertIn("Config error:", source)
        self.assertIn("AmiChatGPT.conf", makefile)
        self.assertIn("HOST=127.0.0.1", makefile)
        self.assertIn("Workbench ToolTypes", readme)
        self.assertIn("AmiChatGPT --host", package_readme)

    def test_tcp_connect_milestone_is_implemented(self):
        source = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        package_readme = (ROOT / "packaging" / "README.txt").read_text(encoding="utf-8")

        self.assertIn("bsdsocket.library", source)
        self.assertIn("SocketBase", source)
        self.assertIn("socket(AF_INET, SOCK_STREAM", source)
        self.assertIn("connect(", source)
        self.assertIn("CloseSocket", source)
        self.assertIn("inet_addr", source)
        self.assertIn("Network: ", source)
        self.assertIn("connected to %s:%u", source)
        self.assertIn("TCP stack not available", source)
        self.assertIn("connects to the configured ChatGPT64 bridge", readme)
        self.assertIn("tries to connect to the configured", package_readme)

    def test_send_receive_milestone_is_implemented(self):
        source = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        package_readme = (ROOT / "packaging" / "README.txt").read_text(encoding="utf-8")

        self.assertIn("BRIDGE_PROMPT_LEN", source)
        self.assertIn("BRIDGE_RESPONSE_LEN", source)
        self.assertIn("build_bridge_prompt", source)
        self.assertIn("bridge_send_all", source)
        self.assertIn("receive_bridge_output", source)
        self.assertIn("append_bridge_output", source)
        self.assertIn("bridge_output_find_prompt", source)
        self.assertIn("bridge_output_is_display_byte", source)
        self.assertIn("bridge_output_decode_display_byte", source)
        self.assertIn("value >= 193 && value <= 218", source)
        self.assertIn("value - 128", source)
        self.assertIn("point_hits_send_button", source)
        self.assertIn("is_send_gadget_event", source)
        self.assertIn("GA_Immediate", source)
        self.assertIn("GA_RelVerify", source)
        self.assertIn("AllocMem(sizeof(*ui), MEMF_CLEAR)", source)
        self.assertIn("ui->bridge_output", source)
        self.assertNotIn("struct AppUi ui;", source)
        self.assertIn("send(ui->bridge_socket", source)
        self.assertIn("recv(ui->bridge_socket", source)
        self.assertIn("Waiting for bridge", source)
        self.assertIn("waiting for bridge reply", source)
        self.assertIn("sends the prompt as a terminal line", readme)
        self.assertIn("`tcpser` is only needed for C64/CCGMS", readme)
        self.assertIn("sends prompts as terminal lines", package_readme)

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
        self.assertIn("This is an early GUI build", package_readme)
        self.assertIn("textfield.gadget", package_readme)


if __name__ == "__main__":
    unittest.main()
