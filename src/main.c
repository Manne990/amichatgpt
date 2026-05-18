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
#define INPUT_TEXT_LEN 128
#define INPUT_LINE_COUNT 3

#define WINDOW_MIN_WIDTH 360
#define WINDOW_MIN_HEIGHT 220
#define WINDOW_DEFAULT_WIDTH 560
#define WINDOW_DEFAULT_HEIGHT 285
#define UI_MARGIN 12
#define UI_GAP 8
#define INPUT_LINE_HEIGHT 16
#define INPUT_LINE_GAP 3
#define STATUS_HEIGHT 16
#define SEND_BUTTON_WIDTH 82

#define GID_TRANSCRIPT 1
#define GID_INPUT_1 2
#define GID_INPUT_2 3
#define GID_INPUT_3 4
#define GID_SEND 5
#define GID_STATUS 6

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
    struct Gadget *input_gadgets[INPUT_LINE_COUNT];
    struct Gadget *send_gadget;
    struct Gadget *status_gadget;
    struct Window *window;
    struct ChatTranscript transcript;
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
    UWORD index;

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
    input_area_height = (INPUT_LINE_COUNT * INPUT_LINE_HEIGHT) +
        ((INPUT_LINE_COUNT - 1) * INPUT_LINE_GAP);
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

    text_line_height = 12;
    if (ui->window->RPort != NULL && ui->window->RPort->TxHeight > 0) {
        text_line_height = ui->window->RPort->TxHeight + 2;
    }
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

    for (index = 0; index < INPUT_LINE_COUNT; index++) {
        GT_SetGadgetAttrs(
            ui->input_gadgets[index],
            ui->window,
            NULL,
            GA_Left,
            inner_left,
            GA_Top,
            input_top + (index * (INPUT_LINE_HEIGHT + INPUT_LINE_GAP)),
            GA_Width,
            input_width,
            GA_Height,
            INPUT_LINE_HEIGHT,
            TAG_DONE);
    }

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
    UWORD index;

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

    for (index = 0; index < INPUT_LINE_COUNT; index++) {
        init_new_gadget(
            &ng,
            ui->visual_info,
            UI_MARGIN,
            194 + (index * (INPUT_LINE_HEIGHT + INPUT_LINE_GAP)),
            466,
            INPUT_LINE_HEIGHT,
            NULL,
            GID_INPUT_1 + index);
        tail = CreateGadget(
            STRING_KIND,
            tail,
            &ng,
            GTST_String,
            "",
            GTST_MaxChars,
            INPUT_TEXT_LEN - 1,
            TAG_DONE);
        if (tail == NULL) {
            return FALSE;
        }
        ui->input_gadgets[index] = tail;
    }

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
        IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | BUTTONIDCMP | STRINGIDCMP |
            LISTVIEWIDCMP,
        TAG_DONE);

    if (ui->window == NULL) {
        return FALSE;
    }

    GT_RefreshWindow(ui->window, NULL);
    layout_gadgets(ui);
    ActivateGadget(ui->input_gadgets[0], ui->window, NULL);

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

static void copy_input_lines(struct AppUi *ui, char lines[INPUT_LINE_COUNT][INPUT_TEXT_LEN])
{
    struct StringInfo *string_info;
    UWORD index;

    for (index = 0; index < INPUT_LINE_COUNT; index++) {
        lines[index][0] = '\0';
        string_info = (struct StringInfo *)ui->input_gadgets[index]->SpecialInfo;
        if (string_info != NULL && string_info->Buffer != NULL) {
            strncpy(lines[index], string_info->Buffer, INPUT_TEXT_LEN - 1);
            lines[index][INPUT_TEXT_LEN - 1] = '\0';
        }
    }
}

static BOOL has_input_text(char lines[INPUT_LINE_COUNT][INPUT_TEXT_LEN])
{
    UWORD index;

    for (index = 0; index < INPUT_LINE_COUNT; index++) {
        if (lines[index][0] != '\0') {
            return TRUE;
        }
    }

    return FALSE;
}

static void clear_input_gadgets(struct AppUi *ui)
{
    UWORD index;

    for (index = 0; index < INPUT_LINE_COUNT; index++) {
        GT_SetGadgetAttrs(
            ui->input_gadgets[index],
            ui->window,
            NULL,
            GTST_String,
            "",
            TAG_DONE);
    }
}

static void handle_send(struct AppUi *ui)
{
    char lines[INPUT_LINE_COUNT][INPUT_TEXT_LEN];
    BOOL first_line;
    UWORD index;

    copy_input_lines(ui, lines);
    if (!has_input_text(lines)) {
        ActivateGadget(ui->input_gadgets[0], ui->window, NULL);
        return;
    }

    first_line = TRUE;
    for (index = 0; index < INPUT_LINE_COUNT; index++) {
        if (lines[index][0] != '\0') {
            if (first_line) {
                transcript_append_prefixed(&ui->transcript, "You: ", lines[index]);
                first_line = FALSE;
            } else {
                transcript_append_prefixed(&ui->transcript, "     ", lines[index]);
            }
        }
    }
    transcript_append(&ui->transcript, "AmiChatGPT: GUI step ready. Bridge comes next.");
    refresh_transcript(ui);
    set_status(ui, "Ready - not connected");

    clear_input_gadgets(ui);
    ActivateGadget(ui->input_gadgets[0], ui->window, NULL);
}

static void handle_input_gadget_up(struct AppUi *ui, UWORD gadget_id)
{
    if (gadget_id == GID_INPUT_1) {
        ActivateGadget(ui->input_gadgets[1], ui->window, NULL);
    } else if (gadget_id == GID_INPUT_2) {
        ActivateGadget(ui->input_gadgets[2], ui->window, NULL);
    } else {
        handle_send(ui);
    }
}

static void run_event_loop(struct AppUi *ui)
{
    BOOL running;
    struct IntuiMessage *message;
    ULONG message_class;
    UWORD gadget_id;
    struct Gadget *gadget;

    running = TRUE;
    while (running) {
        Wait(1L << ui->window->UserPort->mp_SigBit);

        while ((message = GT_GetIMsg(ui->window->UserPort)) != NULL) {
            message_class = message->Class;
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
                    break;

                case IDCMP_NEWSIZE:
                    layout_gadgets(ui);
                    break;

                case IDCMP_GADGETUP:
                    if (gadget_id == GID_SEND) {
                        handle_send(ui);
                    } else if (gadget_id >= GID_INPUT_1 && gadget_id <= GID_INPUT_3) {
                        handle_input_gadget_up(ui, gadget_id);
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
