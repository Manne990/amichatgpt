#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef AMICHATGPT_VERSION
#define AMICHATGPT_VERSION "0.0.0-dev"
#endif

#define DEFAULT_BRIDGE_HOST "127.0.0.1"
#define DEFAULT_BRIDGE_PORT "6464"
#define DEFAULT_TEXT_WIDTH "72"

#ifdef AMIGA_BUILD
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <gadgets/textfield.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <intuition/classes.h>
#include <intuition/icclass.h>
#include <intuition/gadgetclass.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <libraries/gadtools.h>
#include <proto/icon.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <netinet/in.h>
#include <proto/socket.h>
#include <sys/socket.h>
#include <workbench/icon.h>
#include <workbench/startup.h>

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Library *GadToolsBase = NULL;
struct Library *IconBase = NULL;
struct Library *SocketBase = NULL;
struct Library *TextFieldBase = NULL;
Class *TextFieldClass = NULL;

#define CHAT_LINE_COUNT 48
#define CHAT_LINE_LEN 96
#define BRIDGE_PROMPT_LEN 1200
#define BRIDGE_RESPONSE_LEN 8192
#define BRIDGE_READ_CHUNK 256
#define CONFIG_ERROR_COUNT 8
#define CONFIG_ERROR_LEN CHAT_LINE_LEN
#define CONFIG_HOST_LEN 64
#define INPUT_TEXT_LEN 2048
#define INPUT_VISIBLE_LINES 4
#define INPUT_MIN_VISIBLE_LINES 3
#define TRANSCRIPT_MIN_VISIBLE_LINES 4

#define WINDOW_MIN_WIDTH 360
#define WINDOW_MIN_HEIGHT 260
#define WINDOW_DEFAULT_WIDTH 560
#define WINDOW_DEFAULT_HEIGHT 315
#define UI_MARGIN 12
#define UI_GAP 8
#define TRANSCRIPT_INPUT_GAP 12
#define INPUT_SCROLL_WIDTH 16
#define INPUT_SCROLL_GAP 3
#define SEND_BUTTON_WIDTH 82
#define SEND_BUTTON_VERTICAL_INSET 8
#define SEND_BUTTON_TOP_NUDGE 100
#define TRANSCRIPT_WRAP_WIDTH 60

#define GID_TRANSCRIPT 1
#define GID_INPUT 2
#define GID_INPUT_SCROLL 3
#define GID_SEND 4
#define GID_STATUS 5

#define INVALID_BRIDGE_SOCKET -1L

#ifndef SELECTUP
#define SELECTUP 0xE8
#endif

struct BridgeConfig {
    char host[CONFIG_HOST_LEN];
    UWORD port;
    UWORD width;
    char errors[CONFIG_ERROR_COUNT][CONFIG_ERROR_LEN];
    UWORD error_count;
};

struct ChatTranscript {
    struct List labels;
    struct Node nodes[CHAT_LINE_COUNT];
    char text[CHAT_LINE_COUNT][CHAT_LINE_LEN];
    UWORD count;
};

struct AppLayout {
    WORD transcript_left;
    WORD transcript_top;
    WORD transcript_width;
    WORD transcript_height;
    WORD input_left;
    WORD input_top;
    WORD input_width;
    WORD input_height;
    WORD input_scroll_left;
    WORD input_scroll_top;
    WORD input_scroll_width;
    WORD input_scroll_height;
    WORD send_left;
    WORD send_top;
    WORD send_width;
    WORD send_height;
    UWORD visible_transcript_lines;
};

struct AppUi {
    struct Screen *screen;
    APTR visual_info;
    struct Gadget *gadgets;
    struct Gadget *transcript_gadget;
    struct Gadget *input_gadget;
    struct Gadget *input_scroll_gadget;
    struct Gadget *send_gadget;
    struct Gadget *status_gadget;
    struct Window *window;
    struct BridgeConfig config;
    struct ChatTranscript transcript;
    LONG bridge_socket;
    BOOL bridge_connected;
    BOOL gadtools_added;
    BOOL input_gadgets_added;
    struct AppLayout layout;
    char bridge_prompt[BRIDGE_PROMPT_LEN];
    char bridge_output[BRIDGE_RESPONSE_LEN];
};

static int ascii_equals_ignore_case(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right)) {
            return FALSE;
        }
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static char *trim_text(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';

    return text;
}

static void config_add_error(struct BridgeConfig *config, const char *message)
{
    if (config->error_count >= CONFIG_ERROR_COUNT) {
        return;
    }

    strncpy(config->errors[config->error_count], message, CONFIG_ERROR_LEN - 1);
    config->errors[config->error_count][CONFIG_ERROR_LEN - 1] = '\0';
    config->error_count++;
}

static void config_add_value_error(
    struct BridgeConfig *config,
    const char *key,
    const char *value,
    const char *reason)
{
    char error[CONFIG_ERROR_LEN];

    snprintf(error, sizeof(error), "%s=%s ignored: %s", key, value != NULL ? value : "", reason);
    config_add_error(config, error);
}

static void config_init_defaults(struct BridgeConfig *config)
{
    memset(config, 0, sizeof(*config));
    strncpy(config->host, DEFAULT_BRIDGE_HOST, CONFIG_HOST_LEN - 1);
    config->host[CONFIG_HOST_LEN - 1] = '\0';
    config->port = (UWORD)strtoul(DEFAULT_BRIDGE_PORT, NULL, 10);
    config->width = (UWORD)strtoul(DEFAULT_TEXT_WIDTH, NULL, 10);
}

static BOOL config_parse_ushort(
    const char *text,
    UWORD min_value,
    UWORD max_value,
    UWORD *result)
{
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0') {
        return FALSE;
    }

    value = strtoul(text, &end, 10);
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }

    if (*end != '\0' || value < min_value || value > max_value) {
        return FALSE;
    }

    *result = (UWORD)value;
    return TRUE;
}

static BOOL config_apply_pair(
    struct BridgeConfig *config,
    const char *key,
    const char *value,
    BOOL report_unknown)
{
    UWORD parsed_value;

    if (ascii_equals_ignore_case(key, "HOST")) {
        if (value == NULL || *value == '\0') {
            config_add_value_error(config, "HOST", "", "missing host name");
            return TRUE;
        }
        strncpy(config->host, value, CONFIG_HOST_LEN - 1);
        config->host[CONFIG_HOST_LEN - 1] = '\0';
        return TRUE;
    }

    if (ascii_equals_ignore_case(key, "PORT")) {
        if (!config_parse_ushort(value, 1, 65535, &parsed_value)) {
            config_add_value_error(config, "PORT", value, "expected 1-65535");
            return TRUE;
        }
        config->port = parsed_value;
        return TRUE;
    }

    if (ascii_equals_ignore_case(key, "WIDTH")) {
        if (!config_parse_ushort(value, 20, 240, &parsed_value)) {
            config_add_value_error(config, "WIDTH", value, "expected 20-240");
            return TRUE;
        }
        config->width = parsed_value;
        return TRUE;
    }

    if (report_unknown) {
        char error[CONFIG_ERROR_LEN];

        snprintf(error, sizeof(error), "%s ignored: unknown setting", key);
        config_add_error(config, error);
    }

    return FALSE;
}

static BOOL config_apply_assignment(
    struct BridgeConfig *config,
    const char *assignment,
    BOOL report_unknown)
{
    char buffer[160];
    char *key;
    char *value;
    char *equals;

    if (assignment == NULL) {
        return FALSE;
    }

    strncpy(buffer, assignment, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    key = trim_text(buffer);
    if (key[0] == '-' && key[1] == '-') {
        key += 2;
    }

    equals = strchr(key, '=');
    if (equals == NULL) {
        if (report_unknown && *key != '\0') {
            config_add_value_error(config, key, "", "expected KEY=VALUE");
        }
        return FALSE;
    }

    *equals = '\0';
    value = trim_text(equals + 1);
    key = trim_text(key);

    return config_apply_pair(config, key, value, report_unknown);
}

static void config_load_file(struct BridgeConfig *config)
{
    FILE *file;
    char line[160];
    char *trimmed;

    file = fopen("PROGDIR:AmiChatGPT.conf", "r");
    if (file == NULL) {
        file = fopen("AmiChatGPT.conf", "r");
    }
    if (file == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        trimmed = trim_text(line);
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
            continue;
        }
        config_apply_assignment(config, trimmed, TRUE);
    }

    fclose(file);
}

static void config_load_cli_args(struct BridgeConfig *config, int argc, char **argv)
{
    int index;
    const char *argument;

    for (index = 1; index < argc; index++) {
        argument = argv[index];
        if ((ascii_equals_ignore_case(argument, "HOST") ||
             ascii_equals_ignore_case(argument, "--host")) &&
            index + 1 < argc) {
            config_apply_pair(config, "HOST", argv[++index], TRUE);
            continue;
        }
        if ((ascii_equals_ignore_case(argument, "PORT") ||
             ascii_equals_ignore_case(argument, "--port")) &&
            index + 1 < argc) {
            config_apply_pair(config, "PORT", argv[++index], TRUE);
            continue;
        }
        if ((ascii_equals_ignore_case(argument, "WIDTH") ||
             ascii_equals_ignore_case(argument, "--width")) &&
            index + 1 < argc) {
            config_apply_pair(config, "WIDTH", argv[++index], TRUE);
            continue;
        }

        config_apply_assignment(config, argument, TRUE);
    }
}

static void config_load_tooltypes(struct BridgeConfig *config, int argc, char **argv)
{
    struct WBStartup *startup;
    struct WBArg *tool_arg;
    struct DiskObject *disk_object;
    BPTR old_dir;
    char **tool_type;

    if (argc != 0 || argv == NULL || IconBase == NULL) {
        return;
    }

    startup = (struct WBStartup *)argv;
    if (startup->sm_NumArgs < 1 || startup->sm_ArgList == NULL) {
        return;
    }

    tool_arg = &startup->sm_ArgList[0];
    if (tool_arg->wa_Name == NULL) {
        return;
    }

    old_dir = 0;
    if (tool_arg->wa_Lock != 0) {
        old_dir = CurrentDir(tool_arg->wa_Lock);
    }

    disk_object = GetDiskObject((UBYTE *)tool_arg->wa_Name);

    if (tool_arg->wa_Lock != 0) {
        CurrentDir(old_dir);
    }

    if (disk_object == NULL) {
        return;
    }

    for (tool_type = disk_object->do_ToolTypes; tool_type != NULL && *tool_type != NULL; tool_type++) {
        config_apply_assignment(config, *tool_type, FALSE);
    }

    FreeDiskObject(disk_object);
}

static void load_config(struct BridgeConfig *config, int argc, char **argv)
{
    config_init_defaults(config);
    config_load_file(config);
    config_load_tooltypes(config, argc, argv);
    if (argc > 0) {
        config_load_cli_args(config, argc, argv);
    }
}

static void transcript_rebuild_list(struct ChatTranscript *transcript)
{
    UWORD index;

    NewList(&transcript->labels);
    for (index = 0; index < transcript->count; index++) {
        transcript->nodes[index].ln_Name = transcript->text[index];
        AddTail(&transcript->labels, &transcript->nodes[index]);
    }
}

static void transcript_init(struct ChatTranscript *transcript)
{
    memset(transcript, 0, sizeof(*transcript));
    NewList(&transcript->labels);
}

static void transcript_append_raw(struct ChatTranscript *transcript, const char *line)
{
    UWORD index;

    if (transcript->count == CHAT_LINE_COUNT) {
        for (index = 1; index < CHAT_LINE_COUNT; index++) {
            strcpy(transcript->text[index - 1], transcript->text[index]);
        }
        transcript->count--;
    }

    strncpy(transcript->text[transcript->count], line, CHAT_LINE_LEN - 1);
    transcript->text[transcript->count][CHAT_LINE_LEN - 1] = '\0';
    transcript->count++;

    transcript_rebuild_list(transcript);
}

static void transcript_append(struct ChatTranscript *transcript, const char *line)
{
    char wrapped[CHAT_LINE_LEN];
    const char *cursor;
    ULONG remaining;
    UWORD wrap_width;
    UWORD chunk_len;
    UWORD split_at;

    wrap_width = TRANSCRIPT_WRAP_WIDTH;
    if (wrap_width >= CHAT_LINE_LEN) {
        wrap_width = CHAT_LINE_LEN - 1;
    }
    if (wrap_width == 0) {
        wrap_width = 1;
    }

    if (line == NULL || *line == '\0') {
        transcript_append_raw(transcript, "");
        return;
    }

    cursor = line;
    while (*cursor != '\0') {
        remaining = strlen(cursor);
        chunk_len = remaining > wrap_width ? wrap_width : (UWORD)remaining;

        if (remaining > wrap_width) {
            split_at = chunk_len;
            while (split_at > 0 && cursor[split_at] != ' ') {
                split_at--;
            }
            if (split_at >= wrap_width / 2) {
                chunk_len = split_at;
            }
        }

        strncpy(wrapped, cursor, chunk_len);
        wrapped[chunk_len] = '\0';
        while (chunk_len > 0 && wrapped[chunk_len - 1] == ' ') {
            wrapped[--chunk_len] = '\0';
        }

        if (wrapped[0] != '\0') {
            transcript_append_raw(transcript, wrapped);
        }

        cursor += chunk_len;
        while (*cursor == ' ') {
            cursor++;
        }
    }
}

static void transcript_append_prefixed(
    struct ChatTranscript *transcript,
    const char *prefix,
    const char *line)
{
    char formatted[CHAT_LINE_LEN];

    strncpy(formatted, prefix, CHAT_LINE_LEN - 1);
    formatted[CHAT_LINE_LEN - 1] = '\0';
    strncat(formatted, line, CHAT_LINE_LEN - strlen(formatted) - 1);
    transcript_append(transcript, formatted);
}

static void refresh_transcript(struct AppUi *ui)
{
    UWORD top;
    UWORD visible_lines;

    if (ui->window == NULL || ui->transcript_gadget == NULL) {
        return;
    }

    visible_lines = ui->layout.visible_transcript_lines;
    if (visible_lines == 0) {
        visible_lines = 1;
    }

    top = 0;
    if (ui->transcript.count > visible_lines) {
        top = ui->transcript.count - visible_lines;
    }

    GT_SetGadgetAttrs(
        ui->transcript_gadget,
        ui->window,
        NULL,
        GTLV_Labels,
        ~0L,
        TAG_DONE);
    GT_SetGadgetAttrs(
        ui->transcript_gadget,
        ui->window,
        NULL,
        GTLV_Labels,
        &ui->transcript.labels,
        GTLV_Top,
        top,
        TAG_DONE);
}

static WORD get_text_line_height(struct AppUi *ui)
{
    WORD height;

    height = 12;
    if (ui->window != NULL && ui->window->RPort != NULL && ui->window->RPort->TxHeight > 0) {
        height = ui->window->RPort->TxHeight + 2;
    }

    return height;
}

static WORD line_box_height(WORD line_height, WORD visible_lines)
{
    return (visible_lines * line_height) + 6;
}

static void clear_window_content(struct AppUi *ui)
{
    struct RastPort *rast_port;

    if (ui->window == NULL || ui->window->RPort == NULL || ui->screen == NULL) {
        return;
    }

    rast_port = ui->window->RPort;
    SetAPen(rast_port, 0);
    RectFill(
        rast_port,
        ui->window->BorderLeft,
        ui->window->BorderTop,
        ui->window->Width - ui->window->BorderRight - 1,
        ui->window->Height - ui->window->BorderBottom - 1);
}

static Class *open_textfield_class(void)
{
    register struct Library *base __asm("a6");
    register Class *result __asm("d0");

    base = TextFieldBase;
    __asm volatile("jsr a6@(-30:W)" : "=r"(result) : "r"(base) : "d1", "a0", "a1", "cc", "memory");

    return result;
}

static struct TagItem input_scroll_to_text[] = {
    {PGA_Top, TEXTFIELD_Top},
    {TAG_DONE, 0},
};

static struct TagItem input_text_to_scroll[] = {
    {TEXTFIELD_Top, PGA_Top},
    {TEXTFIELD_Visible, PGA_Visible},
    {TEXTFIELD_Lines, PGA_Total},
    {TAG_DONE, 0},
};

static BOOL create_gadgets(struct AppUi *ui);

static void compute_app_layout(struct AppUi *ui)
{
    struct AppLayout *layout;
    WORD inner_left;
    WORD inner_top;
    WORD inner_right;
    WORD inner_bottom;
    WORD available_height;
    WORD content_width;
    WORD transcript_height;
    WORD transcript_top;
    WORD input_top;
    WORD input_width;
    WORD input_area_height;
    WORD min_input_height;
    WORD min_transcript_height;
    WORD max_input_height;
    WORD button_left;
    WORD button_top;
    WORD button_height;
    WORD text_line_height;

    if (ui->window == NULL) {
        return;
    }

    layout = &ui->layout;
    memset(layout, 0, sizeof(*layout));

    inner_left = ui->window->BorderLeft + UI_MARGIN;
    inner_top = ui->window->BorderTop + UI_MARGIN;
    inner_right = ui->window->Width - ui->window->BorderRight - UI_MARGIN;
    inner_bottom = ui->window->Height - ui->window->BorderBottom - UI_MARGIN;

    content_width = inner_right - inner_left;
    if (content_width < 1) {
        content_width = 1;
    }
    available_height = inner_bottom - inner_top;
    if (available_height < 1) {
        available_height = 1;
    }

    text_line_height = get_text_line_height(ui);
    input_area_height = line_box_height(text_line_height, INPUT_VISIBLE_LINES);
    min_input_height = line_box_height(text_line_height, INPUT_MIN_VISIBLE_LINES);
    min_transcript_height = line_box_height(text_line_height, TRANSCRIPT_MIN_VISIBLE_LINES);
    max_input_height = available_height - min_transcript_height - TRANSCRIPT_INPUT_GAP;
    if (max_input_height < min_input_height) {
        max_input_height = min_input_height;
    }
    if (input_area_height > max_input_height) {
        input_area_height = max_input_height;
    }

    input_top = inner_bottom - input_area_height;
    transcript_top = inner_top;
    transcript_height = input_top - transcript_top - TRANSCRIPT_INPUT_GAP;

    if (transcript_height < min_transcript_height) {
        transcript_height = min_transcript_height;
    }

    button_left = inner_right - SEND_BUTTON_WIDTH;
    button_height = input_area_height;
    if (button_height < text_line_height) {
        button_height = text_line_height;
    }
    button_top = input_top;
    input_width = button_left - inner_left - UI_GAP - INPUT_SCROLL_WIDTH - INPUT_SCROLL_GAP;
    if (input_width < 80) {
        input_width = 80;
    }

    layout->transcript_left = inner_left;
    layout->transcript_top = transcript_top;
    layout->transcript_width = content_width;
    layout->transcript_height = transcript_height;
    layout->input_left = inner_left;
    layout->input_top = input_top;
    layout->input_width = input_width;
    layout->input_height = input_area_height;
    layout->input_scroll_left = inner_left + input_width + INPUT_SCROLL_GAP;
    layout->input_scroll_top = input_top;
    layout->input_scroll_width = INPUT_SCROLL_WIDTH;
    layout->input_scroll_height = input_area_height;
    layout->send_left = button_left;
    layout->send_top = button_top;
    layout->send_width = SEND_BUTTON_WIDTH;
    layout->send_height = button_height;
    layout->visible_transcript_lines = transcript_height / text_line_height;
    if (layout->visible_transcript_lines == 0) {
        layout->visible_transcript_lines = 1;
    }
}

static void remove_gadtools_gadgets(struct AppUi *ui)
{
    if (ui->window != NULL && ui->gadtools_added && ui->gadgets != NULL) {
        RemoveGList(ui->window, ui->gadgets, -1);
        ui->gadtools_added = FALSE;
    }

    if (ui->gadgets != NULL) {
        FreeGadgets(ui->gadgets);
        ui->gadgets = NULL;
    }

    ui->transcript_gadget = NULL;
    ui->send_gadget = NULL;
}

static BOOL rebuild_gadtools_gadgets(struct AppUi *ui)
{
    remove_gadtools_gadgets(ui);

    if (!create_gadgets(ui)) {
        return FALSE;
    }

    if (ui->window != NULL && ui->gadgets != NULL) {
        AddGList(ui->window, ui->gadgets, -1, -1, NULL);
        ui->gadtools_added = TRUE;
        RefreshGList(ui->gadgets, ui->window, NULL, -1);
    }

    return TRUE;
}

static BOOL layout_gadgets(struct AppUi *ui)
{
    struct AppLayout *layout;
    BOOL needs_refresh;

    if (ui->window == NULL) {
        return FALSE;
    }

    compute_app_layout(ui);
    layout = &ui->layout;

    clear_window_content(ui);

    if (ui->input_gadget != NULL) {
        needs_refresh = SetGadgetAttrs(
            ui->input_gadget,
            ui->window,
            NULL,
            GA_Left,
            layout->input_left,
            GA_Top,
            layout->input_top,
            GA_Width,
            layout->input_width,
            GA_Height,
            layout->input_height,
            TAG_DONE);
        if (needs_refresh) {
            RefreshGList(ui->input_gadget, ui->window, NULL, 1);
        }

        layout->input_top = ui->input_gadget->TopEdge;
        layout->input_height = ui->input_gadget->Height;
        layout->input_scroll_top = layout->input_top;
        layout->input_scroll_height = layout->input_height;
        layout->send_top = layout->input_top;
        layout->send_height = layout->input_height;
    }

    if (ui->input_scroll_gadget != NULL) {
        needs_refresh = SetGadgetAttrs(
            ui->input_scroll_gadget,
            ui->window,
            NULL,
            GA_Left,
            layout->input_scroll_left,
            GA_Top,
            layout->input_scroll_top,
            GA_Width,
            layout->input_scroll_width,
            GA_Height,
            layout->input_scroll_height,
            TAG_DONE);
        if (needs_refresh) {
            RefreshGList(ui->input_scroll_gadget, ui->window, NULL, 1);
        }
    }

    if (!rebuild_gadtools_gadgets(ui)) {
        return FALSE;
    }

    if (ui->input_gadgets_added && ui->input_scroll_gadget != NULL) {
        RefreshGList(ui->input_scroll_gadget, ui->window, NULL, -1);
    }
    refresh_transcript(ui);

    return TRUE;
}

static void set_status(struct AppUi *ui, const char *status)
{
    if (ui->window == NULL || ui->status_gadget == NULL) {
        return;
    }

    GT_SetGadgetAttrs(
        ui->status_gadget,
        ui->window,
        NULL,
        GTTX_Text,
        status,
        TAG_DONE);
}

static void add_initial_transcript(struct AppUi *ui)
{
    char config_line[CHAT_LINE_LEN];
    UWORD index;

    transcript_append(&ui->transcript, "AmiChatGPT " AMICHATGPT_VERSION);
    transcript_append(&ui->transcript, "Workbench 3.x GadTools GUI prototype.");
    transcript_append(&ui->transcript, "Configuration ready. Connecting to bridge next.");
    transcript_append(&ui->transcript, "");
    transcript_append(&ui->transcript, "Type your message and press Send.");
    transcript_append(&ui->transcript, "");

    snprintf(
        config_line,
        sizeof(config_line),
        "Config: HOST=%s PORT=%u WIDTH=%u",
        ui->config.host,
        ui->config.port,
        ui->config.width);
    transcript_append(&ui->transcript, config_line);

    for (index = 0; index < ui->config.error_count; index++) {
        transcript_append_prefixed(&ui->transcript, "Config error: ", ui->config.errors[index]);
    }
}

static void append_network_message(struct AppUi *ui, const char *message)
{
    transcript_append_prefixed(&ui->transcript, "Network: ", message);
    refresh_transcript(ui);
}

static void close_bridge_connection(struct AppUi *ui)
{
    if (ui->bridge_socket != INVALID_BRIDGE_SOCKET) {
        CloseSocket(ui->bridge_socket);
        ui->bridge_socket = INVALID_BRIDGE_SOCKET;
    }
    ui->bridge_connected = FALSE;
}

static BOOL open_socket_library(struct AppUi *ui)
{
    if (SocketBase != NULL) {
        return TRUE;
    }

    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (SocketBase == NULL) {
        append_network_message(ui, "TCP stack not available (bsdsocket.library).");
        set_status(ui, "TCP stack not available");
        return FALSE;
    }

    return TRUE;
}

static BOOL bridge_output_is_display_byte(unsigned char value)
{
    return (value >= 32 && value <= 126) || (value >= 193 && value <= 218);
}

static BOOL bridge_output_decode_display_byte(unsigned char value, char *decoded)
{
    if (value >= 32 && value <= 126) {
        *decoded = (char)value;
        return TRUE;
    }

    if (value >= 193 && value <= 218) {
        *decoded = (char)(value - 128);
        return TRUE;
    }

    return FALSE;
}

static BOOL bridge_output_find_prompt(const char *output, ULONG length, ULONG *prompt_offset)
{
    LONG index;
    LONG space_index;

    if (length < 2) {
        return FALSE;
    }

    space_index = -1;
    index = (LONG)length - 1;
    while (index >= 0) {
        unsigned char value;

        value = (unsigned char)output[index];
        if (!bridge_output_is_display_byte(value)) {
            index--;
            continue;
        }

        if (space_index < 0) {
            if (value != ' ') {
                return FALSE;
            }
            space_index = index;
            index--;
            continue;
        }

        if (value == '>') {
            if (prompt_offset != NULL) {
                *prompt_offset = (ULONG)index;
            }
            return TRUE;
        }

        return FALSE;
    }

    return FALSE;
}

static BOOL bridge_output_has_prompt(const char *output, ULONG length)
{
    return bridge_output_find_prompt(output, length, NULL);
}

static BOOL receive_bridge_output(struct AppUi *ui, char *output, ULONG output_size)
{
    char chunk[BRIDGE_READ_CHUNK];
    ULONG used;
    ULONG copy_size;
    LONG received;

    used = 0;
    if (output_size == 0) {
        return FALSE;
    }
    output[0] = '\0';

    while (used < output_size - 1) {
        received = recv(ui->bridge_socket, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            append_network_message(ui, "bridge disconnected while reading.");
            close_bridge_connection(ui);
            return FALSE;
        }

        copy_size = (ULONG)received;
        if (copy_size > output_size - used - 1) {
            copy_size = output_size - used - 1;
        }
        CopyMem(chunk, output + used, copy_size);
        used += copy_size;
        output[used] = '\0';

        if (bridge_output_has_prompt(output, used)) {
            return TRUE;
        }
    }

    append_network_message(ui, "bridge reply was too long and was truncated.");
    return TRUE;
}

static void append_sanitized_bridge_line(
    struct ChatTranscript *transcript,
    const char *source_line,
    const char *echo_line)
{
    char clean_line[CHAT_LINE_LEN];
    UWORD used;
    char *line;

    used = 0;
    while (*source_line != '\0' && used < CHAT_LINE_LEN - 1) {
        unsigned char value;

        value = (unsigned char)*source_line++;
        if (bridge_output_decode_display_byte(value, &clean_line[used])) {
            used++;
        } else if (value == '\t') {
            clean_line[used++] = ' ';
        }
    }
    clean_line[used] = '\0';

    line = trim_text(clean_line);
    if (*line != '\0' && strcmp(line, ">") != 0 &&
        (echo_line == NULL || strcmp(line, echo_line) != 0)) {
        transcript_append(transcript, line);
    }
}

static void append_bridge_output(struct AppUi *ui, char *output, const char *echo_line)
{
    char *cursor;
    char *line_end;
    char saved;
    ULONG length;
    ULONG prompt_offset;

    length = strlen(output);
    if (bridge_output_find_prompt(output, length, &prompt_offset)) {
        output[prompt_offset] = '\0';
    }

    cursor = output;
    while (*cursor != '\0') {
        while (*cursor == '\r' || *cursor == '\n') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        line_end = cursor;
        while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') {
            line_end++;
        }

        saved = *line_end;
        *line_end = '\0';
        append_sanitized_bridge_line(&ui->transcript, cursor, echo_line);

        if (saved == '\0') {
            break;
        }
        cursor = line_end + 1;
    }
}

static BOOL bridge_send_all(struct AppUi *ui, const char *text)
{
    const char *cursor;
    ULONG remaining;
    LONG sent;

    cursor = text;
    remaining = strlen(text);

    while (remaining > 0) {
        sent = send(ui->bridge_socket, cursor, remaining, 0);
        if (sent <= 0) {
            append_network_message(ui, "could not send to bridge.");
            close_bridge_connection(ui);
            return FALSE;
        }

        cursor += sent;
        remaining -= (ULONG)sent;
    }

    return TRUE;
}

static BOOL connect_bridge(struct AppUi *ui)
{
    struct sockaddr_in server_addr;
    ULONG address;
    LONG socket_handle;
    char status_line[CHAT_LINE_LEN];

    if (ui->bridge_connected) {
        return TRUE;
    }

    if (!open_socket_library(ui)) {
        return FALSE;
    }

    address = inet_addr(ui->config.host);
    if (address == (ULONG)-1) {
        append_network_message(ui, "HOST must be a numeric IPv4 address for this build.");
        set_status(ui, "Invalid bridge host");
        return FALSE;
    }

    socket_handle = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_handle < 0) {
        append_network_message(ui, "Could not create TCP socket.");
        set_status(ui, "Could not create socket");
        return FALSE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_len = sizeof(server_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ui->config.port);
    server_addr.sin_addr.s_addr = address;

    snprintf(
        status_line,
        sizeof(status_line),
        "connecting to %s:%u...",
        ui->config.host,
        ui->config.port);
    append_network_message(ui, status_line);

    if (connect(socket_handle, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        CloseSocket(socket_handle);
        snprintf(
            status_line,
            sizeof(status_line),
            "could not connect to %s:%u.",
            ui->config.host,
            ui->config.port);
        append_network_message(ui, status_line);
        set_status(ui, "Not connected");
        return FALSE;
    }

    ui->bridge_socket = socket_handle;
    ui->bridge_connected = TRUE;

    snprintf(
        status_line,
        sizeof(status_line),
        "connected to %s:%u.",
        ui->config.host,
        ui->config.port);
    append_network_message(ui, status_line);

    if (!receive_bridge_output(ui, ui->bridge_output, sizeof(ui->bridge_output))) {
        set_status(ui, "Not connected");
        return FALSE;
    }
    append_bridge_output(ui, ui->bridge_output, NULL);

    set_status(ui, "Connected");
    refresh_transcript(ui);

    return TRUE;
}

static BOOL open_libraries(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    GadToolsBase = OpenLibrary("gadtools.library", 39);
    IconBase = OpenLibrary("icon.library", 39);
    TextFieldBase = OpenLibrary(TEXTFIELD_NAME, TEXTFIELD_VER);
    if (TextFieldBase == NULL) {
        TextFieldBase = OpenLibrary("PROGDIR:Gadgets/textfield.gadget", TEXTFIELD_VER);
    }
    if (TextFieldBase != NULL) {
        TextFieldClass = open_textfield_class();
    }

    if (IntuitionBase == NULL || GfxBase == NULL || GadToolsBase == NULL || TextFieldClass == NULL) {
        return FALSE;
    }

    return TRUE;
}

static void close_libraries(void)
{
    if (SocketBase != NULL) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
    if (TextFieldBase != NULL) {
        CloseLibrary(TextFieldBase);
        TextFieldBase = NULL;
        TextFieldClass = NULL;
    }
    if (GadToolsBase != NULL) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
    }
    if (IconBase != NULL) {
        CloseLibrary(IconBase);
        IconBase = NULL;
    }
    if (GfxBase != NULL) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

static void init_new_gadget(
    struct NewGadget *new_gadget,
    APTR visual_info,
    WORD left,
    WORD top,
    WORD width,
    WORD height,
    const char *text,
    UWORD gadget_id)
{
    memset(new_gadget, 0, sizeof(*new_gadget));
    new_gadget->ng_LeftEdge = left;
    new_gadget->ng_TopEdge = top;
    new_gadget->ng_Width = width;
    new_gadget->ng_Height = height;
    new_gadget->ng_GadgetText = (UBYTE *)text;
    new_gadget->ng_TextAttr = NULL;
    new_gadget->ng_GadgetID = gadget_id;
    new_gadget->ng_Flags = PLACETEXT_IN;
    new_gadget->ng_VisualInfo = visual_info;
    new_gadget->ng_UserData = NULL;
}

static BOOL create_gadgets(struct AppUi *ui)
{
    struct NewGadget ng;
    struct Gadget *tail;

    tail = CreateContext(&ui->gadgets);
    if (tail == NULL) {
        return FALSE;
    }

    init_new_gadget(
        &ng,
        ui->visual_info,
        ui->layout.transcript_left,
        ui->layout.transcript_top,
        ui->layout.transcript_width,
        ui->layout.transcript_height,
        NULL,
        GID_TRANSCRIPT);
    tail = CreateGadget(
        LISTVIEW_KIND,
        tail,
        &ng,
        GTLV_Labels,
        &ui->transcript.labels,
        GTLV_ReadOnly,
        TRUE,
        GTLV_Top,
        0,
        TAG_DONE);
    if (tail == NULL) {
        return FALSE;
    }
    ui->transcript_gadget = tail;

    init_new_gadget(
        &ng,
        ui->visual_info,
        ui->layout.send_left,
        ui->layout.send_top,
        ui->layout.send_width,
        ui->layout.send_height,
        "Send",
        GID_SEND);
    tail = CreateGadget(
        BUTTON_KIND,
        tail,
        &ng,
        GA_RelVerify,
        TRUE,
        GA_Immediate,
        TRUE,
        TAG_DONE);
    if (tail == NULL) {
        return FALSE;
    }
    ui->send_gadget = tail;

    return TRUE;
}

static BOOL create_textfield_input(struct AppUi *ui)
{
    ui->input_scroll_gadget = (struct Gadget *)NewObject(
        NULL,
        "propgclass",
        GA_ID,
        GID_INPUT_SCROLL,
        GA_Left,
        ui->layout.input_scroll_left,
        GA_Top,
        ui->layout.input_scroll_top,
        GA_Width,
        ui->layout.input_scroll_width,
        GA_Height,
        ui->layout.input_scroll_height,
        GA_RelVerify,
        TRUE,
        ICA_MAP,
        input_scroll_to_text,
        PGA_NewLook,
        TRUE,
        PGA_Borderless,
        TRUE,
        PGA_Visible,
        1,
        PGA_Total,
        1,
        PGA_Top,
        0,
        TAG_DONE);
    if (ui->input_scroll_gadget == NULL) {
        return FALSE;
    }

    ui->input_gadget = (struct Gadget *)NewObject(
        TextFieldClass,
        NULL,
        GA_ID,
        GID_INPUT,
        GA_Left,
        ui->layout.input_left,
        GA_Top,
        ui->layout.input_top,
        GA_Width,
        ui->layout.input_width,
        GA_Height,
        ui->layout.input_height,
        GA_Previous,
        ui->input_scroll_gadget,
        GA_RelVerify,
        TRUE,
        ICA_MAP,
        input_text_to_scroll,
        ICA_TARGET,
        ui->input_scroll_gadget,
        TEXTFIELD_Text,
        (ULONG)"",
        TEXTFIELD_MaxSize,
        INPUT_TEXT_LEN - 1,
        TEXTFIELD_Border,
        TEXTFIELD_BORDER_DOUBLEBEVEL,
        TEXTFIELD_Partial,
        TRUE,
        TEXTFIELD_BlinkRate,
        500000,
        TEXTFIELD_MaxSizeBeep,
        TRUE,
        TEXTFIELD_TabSpaces,
        4,
        TAG_DONE);
    if (ui->input_gadget == NULL) {
        return FALSE;
    }

    SetGadgetAttrs(
        ui->input_scroll_gadget,
        ui->window,
        NULL,
        ICA_TARGET,
        ui->input_gadget,
        TAG_DONE);

    return TRUE;
}

static void add_app_gadgets(struct AppUi *ui)
{
    if (ui->window == NULL) {
        return;
    }

    if (ui->gadgets != NULL && !ui->gadtools_added) {
        AddGList(ui->window, ui->gadgets, -1, -1, NULL);
        ui->gadtools_added = TRUE;
    }

    if (ui->input_scroll_gadget != NULL && !ui->input_gadgets_added) {
        AddGList(ui->window, ui->input_scroll_gadget, -1, -1, NULL);
        ui->input_gadgets_added = TRUE;
    }
}

static BOOL open_app_window(struct AppUi *ui)
{
    ui->window = OpenWindowTags(
        NULL,
        WA_Title,
        "AmiChatGPT",
        WA_PubScreen,
        ui->screen,
        WA_Left,
        40,
        WA_Top,
        35,
        WA_Width,
        WINDOW_DEFAULT_WIDTH,
        WA_Height,
        WINDOW_DEFAULT_HEIGHT,
        WA_DepthGadget,
        TRUE,
        WA_DragBar,
        TRUE,
        WA_SizeGadget,
        TRUE,
        WA_MinWidth,
        WINDOW_MIN_WIDTH,
        WA_MinHeight,
        WINDOW_MIN_HEIGHT,
        WA_MaxWidth,
        ui->screen->Width,
        WA_MaxHeight,
        ui->screen->Height,
        WA_SizeBBottom,
        TRUE,
        WA_CloseGadget,
        TRUE,
        WA_Activate,
        TRUE,
        WA_IDCMP,
        IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_GADGETDOWN |
            IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS | BUTTONIDCMP |
            LISTVIEWIDCMP,
        TAG_DONE);

    if (ui->window == NULL) {
        return FALSE;
    }

    compute_app_layout(ui);
    if (!create_textfield_input(ui)) {
        return FALSE;
    }
    add_app_gadgets(ui);
    if (!layout_gadgets(ui)) {
        return FALSE;
    }
    GT_RefreshWindow(ui->window, NULL);
    ActivateGadget(ui->input_gadget, ui->window, NULL);

    return TRUE;
}

static void close_app_ui(struct AppUi *ui)
{
    close_bridge_connection(ui);
    if (ui->window != NULL && ui->input_gadgets_added && ui->input_scroll_gadget != NULL) {
        RemoveGList(ui->window, ui->input_scroll_gadget, -1);
        ui->input_gadgets_added = FALSE;
    }
    remove_gadtools_gadgets(ui);
    if (ui->input_gadget != NULL) {
        DisposeObject(ui->input_gadget);
        ui->input_gadget = NULL;
    }
    if (ui->input_scroll_gadget != NULL) {
        DisposeObject(ui->input_scroll_gadget);
        ui->input_scroll_gadget = NULL;
    }
    if (ui->window != NULL) {
        CloseWindow(ui->window);
        ui->window = NULL;
    }
    if (ui->gadgets != NULL) {
        FreeGadgets(ui->gadgets);
        ui->gadgets = NULL;
    }
    if (ui->visual_info != NULL) {
        FreeVisualInfo(ui->visual_info);
        ui->visual_info = NULL;
    }
    if (ui->screen != NULL) {
        UnlockPubScreen(NULL, ui->screen);
        ui->screen = NULL;
    }
}

static BOOL open_app_ui(struct AppUi *ui, int argc, char **argv)
{
    memset(ui, 0, sizeof(*ui));
    ui->bridge_socket = INVALID_BRIDGE_SOCKET;
    load_config(&ui->config, argc, argv);
    transcript_init(&ui->transcript);
    add_initial_transcript(ui);

    ui->screen = LockPubScreen(NULL);
    if (ui->screen == NULL) {
        return FALSE;
    }

    ui->visual_info = GetVisualInfo(ui->screen, TAG_DONE);
    if (ui->visual_info == NULL) {
        return FALSE;
    }

    if (!open_app_window(ui)) {
        return FALSE;
    }

    connect_bridge(ui);

    return TRUE;
}

static BOOL text_has_content(const char *text)
{
    while (*text != '\0') {
        if (*text != '\n' && *text != '\r' && *text != ' ' && *text != '\t') {
            return TRUE;
        }
        text++;
    }

    return FALSE;
}

static char *copy_input_text(struct AppUi *ui, ULONG *allocated_size)
{
    ULONG text_size;
    ULONG text_pointer;
    char *copy;

    text_size = 0;
    text_pointer = 0;
    *allocated_size = 0;

    SetGadgetAttrs(ui->input_gadget, ui->window, NULL, TEXTFIELD_ReadOnly, TRUE, TAG_DONE);
    if (!GetAttr(TEXTFIELD_Size, (Object *)ui->input_gadget, &text_size)) {
        SetGadgetAttrs(ui->input_gadget, ui->window, NULL, TEXTFIELD_ReadOnly, FALSE, TAG_DONE);
        return NULL;
    }

    copy = AllocMem(text_size + 1, MEMF_CLEAR);
    if (copy == NULL) {
        SetGadgetAttrs(ui->input_gadget, ui->window, NULL, TEXTFIELD_ReadOnly, FALSE, TAG_DONE);
        return NULL;
    }

    if (text_size > 0 && GetAttr(TEXTFIELD_Text, (Object *)ui->input_gadget, &text_pointer) &&
        text_pointer != 0) {
        CopyMem((APTR)text_pointer, copy, text_size);
    }
    copy[text_size] = '\0';
    *allocated_size = text_size + 1;

    SetGadgetAttrs(ui->input_gadget, ui->window, NULL, TEXTFIELD_ReadOnly, FALSE, TAG_DONE);

    return copy;
}

static void clear_input_text(struct AppUi *ui)
{
    SetGadgetAttrs(
        ui->input_gadget,
        ui->window,
        NULL,
        TEXTFIELD_Text,
        (ULONG)"",
        TEXTFIELD_CursorPos,
        0,
        TEXTFIELD_Top,
        0,
        TAG_DONE);
    ActivateGadget(ui->input_gadget, ui->window, NULL);
}

static void append_input_to_transcript(struct ChatTranscript *transcript, const char *text)
{
    char line[CHAT_LINE_LEN];
    BOOL first_line;
    UWORD max_line_len;
    UWORD line_index;
    const char *cursor;
    const char *prefix;

    first_line = TRUE;
    cursor = text;

    while (*cursor != '\0') {
        if (*cursor == '\r') {
            cursor++;
            continue;
        }

        prefix = first_line ? "You: " : "     ";
        max_line_len = CHAT_LINE_LEN - strlen(prefix) - 1;
        line_index = 0;

        while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r' && line_index < max_line_len) {
            line[line_index++] = *cursor++;
        }
        line[line_index] = '\0';

        if (line[0] != '\0') {
            transcript_append_prefixed(transcript, prefix, line);
            first_line = FALSE;
        }

        if (*cursor == '\n' || *cursor == '\r') {
            cursor++;
        }
    }
}

static BOOL build_bridge_prompt(const char *input_text, char *prompt, ULONG prompt_size)
{
    ULONG index;
    BOOL pending_space;
    BOOL truncated;
    unsigned char value;

    index = 0;
    pending_space = FALSE;
    truncated = FALSE;

    if (prompt_size == 0) {
        return FALSE;
    }

    while (*input_text != '\0') {
        value = (unsigned char)*input_text++;

        if (value == '\r' || value == '\n' || value == '\t' || value == ' ') {
            pending_space = index > 0;
            continue;
        }

        if (value < 32 || value > 126) {
            continue;
        }

        if (pending_space) {
            if (index >= prompt_size - 1) {
                truncated = TRUE;
                break;
            }
            prompt[index++] = ' ';
            pending_space = FALSE;
        }

        if (index >= prompt_size - 1) {
            truncated = TRUE;
            break;
        }
        prompt[index++] = (char)value;
    }

    prompt[index] = '\0';
    return truncated;
}

static BOOL point_hits_send_button(struct AppUi *ui, WORD x, WORD y)
{
    struct AppLayout *layout;

    layout = &ui->layout;
    return x >= layout->send_left && x < layout->send_left + layout->send_width &&
           y >= layout->send_top && y < layout->send_top + layout->send_height;
}

static BOOL is_send_gadget_event(struct AppUi *ui, struct Gadget *gadget, UWORD gadget_id)
{
    return gadget_id == GID_SEND || (ui->send_gadget != NULL && gadget == ui->send_gadget);
}

static void handle_send(struct AppUi *ui)
{
    char *input_text;
    ULONG allocated_size;
    BOOL was_truncated;

    input_text = copy_input_text(ui, &allocated_size);
    if (input_text == NULL) {
        set_status(ui, "Could not read input");
        ActivateGadget(ui->input_gadget, ui->window, NULL);
        return;
    }

    was_truncated = build_bridge_prompt(input_text, ui->bridge_prompt, sizeof(ui->bridge_prompt));
    if (!text_has_content(ui->bridge_prompt)) {
        FreeMem(input_text, allocated_size);
        ActivateGadget(ui->input_gadget, ui->window, NULL);
        return;
    }

    append_input_to_transcript(&ui->transcript, ui->bridge_prompt);
    if (was_truncated) {
        transcript_append(&ui->transcript, "AmiChatGPT: prompt was truncated before sending.");
    }
    transcript_append(&ui->transcript, "AmiChatGPT: waiting for bridge reply...");
    refresh_transcript(ui);

    if (!ui->bridge_connected) {
        connect_bridge(ui);
    }

    if (ui->bridge_connected) {
        set_status(ui, "Waiting for bridge");
        if (bridge_send_all(ui, ui->bridge_prompt) && bridge_send_all(ui, "\r\n") &&
            receive_bridge_output(ui, ui->bridge_output, sizeof(ui->bridge_output))) {
            append_bridge_output(ui, ui->bridge_output, ui->bridge_prompt);
            set_status(ui, "Connected");
        } else {
            set_status(ui, "Not connected");
        }
    } else {
        transcript_append(&ui->transcript, "AmiChatGPT: Not connected. Check TCP stack and bridge.");
        set_status(ui, "Not connected");
    }
    refresh_transcript(ui);
    clear_input_text(ui);

    FreeMem(input_text, allocated_size);
}

static void run_event_loop(struct AppUi *ui)
{
    BOOL running;
    struct IntuiMessage *message;
    ULONG message_class;
    UWORD message_code;
    UWORD gadget_id;
    WORD mouse_x;
    WORD mouse_y;
    struct Gadget *gadget;

    running = TRUE;
    while (running) {
        Wait(1L << ui->window->UserPort->mp_SigBit);

        while ((message = GT_GetIMsg(ui->window->UserPort)) != NULL) {
            message_class = message->Class;
            message_code = message->Code;
            mouse_x = message->MouseX;
            mouse_y = message->MouseY;
            gadget_id = 0;
            gadget = NULL;
            if (message->IAddress != NULL) {
                gadget = (struct Gadget *)message->IAddress;
                gadget_id = gadget->GadgetID;
            }
            GT_ReplyIMsg(message);

            switch (message_class) {
                case IDCMP_CLOSEWINDOW:
                    running = FALSE;
                    break;

                case IDCMP_REFRESHWINDOW:
                    GT_BeginRefresh(ui->window);
                    GT_EndRefresh(ui->window, TRUE);
                    if (ui->input_scroll_gadget != NULL) {
                        RefreshGList(ui->input_scroll_gadget, ui->window, NULL, -1);
                    }
                    break;

                case IDCMP_NEWSIZE:
                    if (!layout_gadgets(ui)) {
                        running = FALSE;
                    }
                    break;

                case IDCMP_GADGETDOWN:
                    if (is_send_gadget_event(ui, gadget, gadget_id)) {
                        handle_send(ui);
                    }
                    break;

                case IDCMP_GADGETUP:
                    if (is_send_gadget_event(ui, gadget, gadget_id)) {
                        handle_send(ui);
                    }
                    break;

                case IDCMP_MOUSEBUTTONS:
                    if (message_code == SELECTUP && point_hits_send_button(ui, mouse_x, mouse_y)) {
                        handle_send(ui);
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

static int run_amiga_gui(int argc, char **argv)
{
    struct AppUi *ui;

    if (!open_libraries()) {
        PutStr("AmiChatGPT: could not open Workbench 3.x GUI libraries.\n");
        close_libraries();
        return RETURN_FAIL;
    }

    ui = AllocMem(sizeof(*ui), MEMF_CLEAR);
    if (ui == NULL) {
        PutStr("AmiChatGPT: not enough memory for the application state.\n");
        close_libraries();
        return RETURN_FAIL;
    }

    if (!open_app_ui(ui, argc, argv)) {
        PutStr("AmiChatGPT: could not open the Workbench GUI.\n");
        close_app_ui(ui);
        FreeMem(ui, sizeof(*ui));
        close_libraries();
        return RETURN_FAIL;
    }

    run_event_loop(ui);
    close_app_ui(ui);
    FreeMem(ui, sizeof(*ui));
    close_libraries();

    return RETURN_OK;
}
#endif

static void write_scaffold_message(FILE *out)
{
    fputs("AmiChatGPT " AMICHATGPT_VERSION "\n", out);
    fputs("Workbench 3.x ChatGPT64 bridge client\n", out);
    fputs("Native Intuition/GadTools GUI prototype for Amiga builds.\n", out);
    fputs("\n", out);
    fputs("Default bridge:\n", out);
    fputs("  host: " DEFAULT_BRIDGE_HOST "\n", out);
    fputs("  port: " DEFAULT_BRIDGE_PORT "\n", out);
    fputs("  width: " DEFAULT_TEXT_WIDTH "\n", out);
    fputs("\n", out);
    fputs("Current milestone: bridge send and receive.\n", out);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#ifdef AMIGA_BUILD
    return run_amiga_gui(argc, argv);
#else
    write_scaffold_message(stdout);
    return 0;
#endif
}
