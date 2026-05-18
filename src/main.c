#include <stdio.h>
#include <string.h>

#ifndef AMICHATGPT_VERSION
#define AMICHATGPT_VERSION "0.0.0-dev"
#endif

#define DEFAULT_BRIDGE_HOST "192.168.1.50"
#define DEFAULT_BRIDGE_PORT "6464"
#define DEFAULT_TEXT_WIDTH "72"

#ifdef AMIGA_BUILD
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <intuition/gadgetclass.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <libraries/gadtools.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Library *GadToolsBase = NULL;

#define CHAT_LINE_COUNT 48
#define CHAT_LINE_LEN 96
#define INPUT_TEXT_LEN 384
#define INPUT_LINE_COUNT 3

#define WINDOW_MIN_WIDTH 360
#define WINDOW_MIN_HEIGHT 220
#define WINDOW_DEFAULT_WIDTH 560
#define WINDOW_DEFAULT_HEIGHT 285
#define UI_MARGIN 12
#define UI_GAP 8
#define INPUT_LINE_HEIGHT 16
#define INPUT_EDITOR_PADDING_X 5
#define INPUT_EDITOR_PADDING_Y 4
#define STATUS_HEIGHT 16
#define SEND_BUTTON_WIDTH 82

#define GID_TRANSCRIPT 1
#define GID_SEND 2
#define GID_STATUS 3

struct ChatTranscript {
    struct List labels;
    struct Node nodes[CHAT_LINE_COUNT];
    char text[CHAT_LINE_COUNT][CHAT_LINE_LEN];
    UWORD count;
};

struct AppUi {
    struct Screen *screen;
    APTR visual_info;
    struct Gadget *gadgets;
    struct Gadget *transcript_gadget;
    struct Gadget *send_gadget;
    struct Gadget *status_gadget;
    struct Window *window;
    struct ChatTranscript transcript;
    char input_text[INPUT_TEXT_LEN];
    UWORD input_len;
    BOOL input_active;
    WORD input_left;
    WORD input_top;
    WORD input_width;
    WORD input_height;
    UWORD visible_transcript_lines;
};

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

static void transcript_append(struct ChatTranscript *transcript, const char *line)
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

    visible_lines = ui->visible_transcript_lines;
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

static UWORD input_line_count(struct AppUi *ui)
{
    UWORD count;
    UWORD index;

    count = 1;
    for (index = 0; index < ui->input_len; index++) {
        if (ui->input_text[index] == '\n') {
            count++;
        }
    }

    return count;
}

static UWORD input_current_line_length(struct AppUi *ui)
{
    UWORD length;
    UWORD index;

    length = 0;
    index = ui->input_len;
    while (index > 0 && ui->input_text[index - 1] != '\n') {
        length++;
        index--;
    }

    return length;
}

static UWORD input_max_columns(struct AppUi *ui)
{
    WORD char_width;
    WORD available_width;

    char_width = 8;
    if (ui->window != NULL && ui->window->RPort != NULL && ui->window->RPort->TxWidth > 0) {
        char_width = ui->window->RPort->TxWidth;
    }

    available_width = ui->input_width - (2 * INPUT_EDITOR_PADDING_X) - 4;
    if (available_width < char_width) {
        return 1;
    }

    return available_width / char_width;
}

static BOOL input_has_text(struct AppUi *ui)
{
    UWORD index;

    for (index = 0; index < ui->input_len; index++) {
        if (ui->input_text[index] != '\n' && ui->input_text[index] != '\r') {
            return TRUE;
        }
    }

    return FALSE;
}

static void input_cursor_line_and_column(struct AppUi *ui, UWORD *line, UWORD *column)
{
    UWORD index;

    *line = 0;
    *column = 0;
    for (index = 0; index < ui->input_len; index++) {
        if (ui->input_text[index] == '\n') {
            (*line)++;
            *column = 0;
        } else {
            (*column)++;
        }
    }
    if (*line >= INPUT_LINE_COUNT) {
        *line = INPUT_LINE_COUNT - 1;
    }
}

static const char *input_current_line_start(struct AppUi *ui)
{
    UWORD index;

    index = ui->input_len;
    while (index > 0 && ui->input_text[index - 1] != '\n') {
        index--;
    }

    return &ui->input_text[index];
}

static void draw_input_editor(struct AppUi *ui)
{
    struct RastPort *rp;
    WORD left;
    WORD top;
    WORD right;
    WORD bottom;
    WORD text_x;
    WORD text_y;
    WORD line_step;
    WORD cursor_x;
    WORD cursor_top;
    WORD cursor_bottom;
    UWORD index;
    UWORD line;
    UWORD cursor_line;
    UWORD cursor_column;
    UWORD line_len;
    char line_buffer[INPUT_TEXT_LEN];
    const char *cursor_line_start;

    if (ui->window == NULL || ui->window->RPort == NULL || ui->input_width <= 0 ||
        ui->input_height <= 0) {
        return;
    }

    rp = ui->window->RPort;
    left = ui->input_left;
    top = ui->input_top;
    right = left + ui->input_width - 1;
    bottom = top + ui->input_height - 1;

    SetAPen(rp, 0);
    RectFill(rp, left, top, right, bottom);

    SetAPen(rp, 1);
    Move(rp, left, top);
    Draw(rp, right, top);
    Draw(rp, right, bottom);
    Draw(rp, left, bottom);
    Draw(rp, left, top);

    SetAPen(rp, 2);
    Move(rp, left + 1, top + 1);
    Draw(rp, right - 1, top + 1);
    Move(rp, left + 1, top + 1);
    Draw(rp, left + 1, bottom - 1);

    SetAPen(rp, 1);
    Move(rp, left + 2, bottom - 2);
    Draw(rp, right - 2, bottom - 2);
    Move(rp, right - 2, top + 2);
    Draw(rp, right - 2, bottom - 2);

    SetAPen(rp, 1);
    SetBPen(rp, 0);
    SetDrMd(rp, JAM2);

    text_x = left + INPUT_EDITOR_PADDING_X;
    line_step = get_text_line_height(ui);
    text_y = top + INPUT_EDITOR_PADDING_Y + rp->TxBaseline;

    line = 0;
    line_len = 0;
    for (index = 0; index <= ui->input_len && line < INPUT_LINE_COUNT; index++) {
        if (index == ui->input_len || ui->input_text[index] == '\n') {
            line_buffer[line_len] = '\0';
            if (line_len > 0) {
                Move(rp, text_x, text_y + (line * line_step));
                Text(rp, line_buffer, line_len);
            }
            line++;
            line_len = 0;
        } else if (line_len < INPUT_TEXT_LEN - 1) {
            line_buffer[line_len++] = ui->input_text[index];
        }
    }

    if (ui->input_active) {
        input_cursor_line_and_column(ui, &cursor_line, &cursor_column);
        cursor_line_start = input_current_line_start(ui);
        cursor_x = text_x;
        if (cursor_column > 0) {
            cursor_x += TextLength(rp, (char *)cursor_line_start, cursor_column);
        }
        cursor_top = text_y + (cursor_line * line_step) - rp->TxBaseline;
        cursor_bottom = cursor_top + rp->TxHeight - 1;
        SetAPen(rp, 1);
        Move(rp, cursor_x, cursor_top);
        Draw(rp, cursor_x, cursor_bottom);
    }
}

static BOOL point_is_in_input_editor(struct AppUi *ui, WORD x, WORD y)
{
    return x >= ui->input_left && x < ui->input_left + ui->input_width && y >= ui->input_top &&
        y < ui->input_top + ui->input_height;
}

static void clear_input_editor(struct AppUi *ui)
{
    ui->input_len = 0;
    ui->input_text[0] = '\0';
    draw_input_editor(ui);
}

static void append_input_char(struct AppUi *ui, char ch)
{
    if (ui->input_len >= INPUT_TEXT_LEN - 1) {
        return;
    }

    if (ch == '\n') {
        if (input_line_count(ui) >= INPUT_LINE_COUNT) {
            return;
        }
    } else if (input_current_line_length(ui) >= input_max_columns(ui)) {
        return;
    }

    ui->input_text[ui->input_len++] = ch;
    ui->input_text[ui->input_len] = '\0';
}

static void handle_input_key(struct AppUi *ui, UWORD code)
{
    if (!ui->input_active) {
        return;
    }

    if (code == 8 || code == 127) {
        if (ui->input_len > 0) {
            ui->input_len--;
            ui->input_text[ui->input_len] = '\0';
        }
    } else if (code == '\r' || code == '\n') {
        append_input_char(ui, '\n');
    } else if (code >= 32 && code < 256) {
        append_input_char(ui, (char)code);
    }

    draw_input_editor(ui);
}

static void handle_mouse_button(struct AppUi *ui, UWORD code, WORD x, WORD y)
{
    BOOL was_active;

    if (code != SELECTDOWN) {
        return;
    }

    was_active = ui->input_active;
    ui->input_active = point_is_in_input_editor(ui, x, y);
    if (was_active != ui->input_active) {
        draw_input_editor(ui);
    }
}

static void layout_gadgets(struct AppUi *ui)
{
    WORD inner_left;
    WORD inner_top;
    WORD inner_right;
    WORD inner_bottom;
    WORD content_width;
    WORD transcript_height;
    WORD transcript_top;
    WORD input_top;
    WORD input_width;
    WORD input_area_height;
    WORD button_left;
    WORD status_top;
    WORD status_width;
    WORD text_line_height;

    if (ui->window == NULL) {
        return;
    }

    inner_left = ui->window->BorderLeft + UI_MARGIN;
    inner_top = ui->window->BorderTop + UI_MARGIN;
    inner_right = ui->window->Width - ui->window->BorderRight - UI_MARGIN;
    inner_bottom = ui->window->Height - ui->window->BorderBottom - UI_MARGIN;

    content_width = inner_right - inner_left;
    if (content_width < 1) {
        content_width = 1;
    }
    input_area_height = (INPUT_LINE_COUNT * INPUT_LINE_HEIGHT) + (2 * INPUT_EDITOR_PADDING_Y);
    status_top = inner_bottom - STATUS_HEIGHT;
    input_top = status_top - UI_GAP - input_area_height;
    transcript_top = inner_top;
    transcript_height = input_top - transcript_top - UI_GAP;

    if (transcript_height < 48) {
        transcript_height = 48;
    }

    button_left = inner_right - SEND_BUTTON_WIDTH;
    input_width = button_left - inner_left - UI_GAP;
    if (input_width < 80) {
        input_width = 80;
    }
    status_width = content_width;
    if (status_width > 320) {
        status_width = 320;
    }

    text_line_height = get_text_line_height(ui);
    ui->visible_transcript_lines = transcript_height / text_line_height;
    if (ui->visible_transcript_lines == 0) {
        ui->visible_transcript_lines = 1;
    }

    GT_SetGadgetAttrs(
        ui->transcript_gadget,
        ui->window,
        NULL,
        GA_Left,
        inner_left,
        GA_Top,
        transcript_top,
        GA_Width,
        content_width,
        GA_Height,
        transcript_height,
        TAG_DONE);

    ui->input_left = inner_left;
    ui->input_top = input_top;
    ui->input_width = input_width;
    ui->input_height = input_area_height;

    GT_SetGadgetAttrs(
        ui->send_gadget,
        ui->window,
        NULL,
        GA_Left,
        button_left,
        GA_Top,
        input_top,
        GA_Width,
        SEND_BUTTON_WIDTH,
        GA_Height,
        input_area_height,
        TAG_DONE);

    GT_SetGadgetAttrs(
        ui->status_gadget,
        ui->window,
        NULL,
        GA_Left,
        inner_left,
        GA_Top,
        status_top,
        GA_Width,
        status_width,
        GA_Height,
        STATUS_HEIGHT,
        TAG_DONE);

    GT_RefreshWindow(ui->window, NULL);
    draw_input_editor(ui);
    refresh_transcript(ui);
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
    transcript_append(&ui->transcript, "AmiChatGPT " AMICHATGPT_VERSION);
    transcript_append(&ui->transcript, "Workbench 3.x GadTools GUI prototype.");
    transcript_append(&ui->transcript, "Bridge connection comes in the next step.");
    transcript_append(&ui->transcript, "");
    transcript_append(&ui->transcript, "Type up to three lines and press Send.");
}

static BOOL open_libraries(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    GadToolsBase = OpenLibrary("gadtools.library", 39);

    if (IntuitionBase == NULL || GfxBase == NULL || GadToolsBase == NULL) {
        return FALSE;
    }

    return TRUE;
}

static void close_libraries(void)
{
    if (GadToolsBase != NULL) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
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

    init_new_gadget(&ng, ui->visual_info, UI_MARGIN, UI_MARGIN, 536, 164, NULL, GID_TRANSCRIPT);
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

    init_new_gadget(&ng, ui->visual_info, 486, 194, SEND_BUTTON_WIDTH, 54, "Send", GID_SEND);
    tail = CreateGadget(BUTTON_KIND, tail, &ng, TAG_DONE);
    if (tail == NULL) {
        return FALSE;
    }
    ui->send_gadget = tail;

    init_new_gadget(&ng, ui->visual_info, UI_MARGIN, 257, 320, STATUS_HEIGHT, NULL, GID_STATUS);
    tail = CreateGadget(
        TEXT_KIND,
        tail,
        &ng,
        GTTX_Text,
        "Offline prototype",
        GTTX_Border,
        TRUE,
        TAG_DONE);
    if (tail == NULL) {
        return FALSE;
    }
    ui->status_gadget = tail;

    return TRUE;
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
        WA_Gadgets,
        ui->gadgets,
        WA_IDCMP,
        IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_MOUSEBUTTONS |
            IDCMP_VANILLAKEY | BUTTONIDCMP | LISTVIEWIDCMP,
        TAG_DONE);

    if (ui->window == NULL) {
        return FALSE;
    }

    GT_RefreshWindow(ui->window, NULL);
    ui->input_active = TRUE;
    layout_gadgets(ui);

    return TRUE;
}

static void close_app_ui(struct AppUi *ui)
{
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

static BOOL open_app_ui(struct AppUi *ui)
{
    memset(ui, 0, sizeof(*ui));
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

    if (!create_gadgets(ui)) {
        return FALSE;
    }

    if (!open_app_window(ui)) {
        return FALSE;
    }

    return TRUE;
}

static void append_input_to_transcript(struct AppUi *ui)
{
    char line[CHAT_LINE_LEN];
    BOOL first_line;
    UWORD input_index;
    UWORD line_index;
    char ch;

    first_line = TRUE;
    line_index = 0;
    for (input_index = 0; input_index <= ui->input_len; input_index++) {
        ch = ui->input_text[input_index];
        if (input_index == ui->input_len || ch == '\n') {
            line[line_index] = '\0';
            if (line[0] != '\0') {
                if (first_line) {
                    transcript_append_prefixed(&ui->transcript, "You: ", line);
                    first_line = FALSE;
                } else {
                    transcript_append_prefixed(&ui->transcript, "     ", line);
                }
            }
            line_index = 0;
        } else if (line_index < CHAT_LINE_LEN - 1) {
            line[line_index++] = ch;
        }
    }
}

static void handle_send(struct AppUi *ui)
{
    if (!input_has_text(ui)) {
        ui->input_active = TRUE;
        draw_input_editor(ui);
        return;
    }

    append_input_to_transcript(ui);
    transcript_append(&ui->transcript, "AmiChatGPT: GUI step ready. Bridge comes next.");
    refresh_transcript(ui);
    set_status(ui, "Ready - not connected");

    clear_input_editor(ui);
    ui->input_active = TRUE;
    draw_input_editor(ui);
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
                    draw_input_editor(ui);
                    break;

                case IDCMP_NEWSIZE:
                    layout_gadgets(ui);
                    break;

                case IDCMP_MOUSEBUTTONS:
                    handle_mouse_button(ui, message_code, mouse_x, mouse_y);
                    break;

                case IDCMP_VANILLAKEY:
                    handle_input_key(ui, message_code);
                    break;

                case IDCMP_GADGETUP:
                    if (gadget_id == GID_SEND) {
                        handle_send(ui);
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

static int run_amiga_gui(void)
{
    struct AppUi ui;

    if (!open_libraries()) {
        PutStr("AmiChatGPT: could not open Workbench 3.x GUI libraries.\n");
        close_libraries();
        return RETURN_FAIL;
    }

    if (!open_app_ui(&ui)) {
        PutStr("AmiChatGPT: could not open the Workbench GUI.\n");
        close_app_ui(&ui);
        close_libraries();
        return RETURN_FAIL;
    }

    run_event_loop(&ui);
    close_app_ui(&ui);
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
    fputs("Current milestone: local GUI prototype, no networking yet.\n", out);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#ifdef AMIGA_BUILD
    return run_amiga_gui();
#else
    write_scaffold_message(stdout);
    return 0;
#endif
}
