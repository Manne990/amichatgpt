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
#include <gadgets/textfield.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <intuition/classes.h>
#include <intuition/icclass.h>
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
struct Library *TextFieldBase = NULL;
Class *TextFieldClass = NULL;

#define CHAT_LINE_COUNT 48
#define CHAT_LINE_LEN 96
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
#define TRANSCRIPT_INPUT_GAP 24
#define INPUT_SCROLL_WIDTH 16
#define INPUT_SCROLL_GAP 3
#define SEND_BUTTON_WIDTH 82

#define GID_TRANSCRIPT 1
#define GID_INPUT 2
#define GID_INPUT_SCROLL 3
#define GID_SEND 4
#define GID_STATUS 5

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
    struct Gadget *input_gadget;
    struct Gadget *input_scroll_gadget;
    struct Gadget *send_gadget;
    struct Gadget *status_gadget;
    struct Window *window;
    struct ChatTranscript transcript;
    WORD input_left;
    WORD input_top;
    WORD input_width;
    WORD input_height;
    WORD input_scroll_left;
    WORD input_scroll_width;
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

static void layout_gadgets(struct AppUi *ui)
{
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
    WORD text_line_height;
    BOOL needs_refresh;

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
    input_width = button_left - inner_left - UI_GAP - INPUT_SCROLL_WIDTH - INPUT_SCROLL_GAP;
    if (input_width < 80) {
        input_width = 80;
    }

    ui->visible_transcript_lines = transcript_height / text_line_height;
    if (ui->visible_transcript_lines == 0) {
        ui->visible_transcript_lines = 1;
    }

    clear_window_content(ui);

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
    ui->input_scroll_left = inner_left + input_width + INPUT_SCROLL_GAP;
    ui->input_scroll_width = INPUT_SCROLL_WIDTH;

    if (ui->input_gadget != NULL) {
        needs_refresh = SetGadgetAttrs(
            ui->input_gadget,
            ui->window,
            NULL,
            GA_Left,
            ui->input_left,
            GA_Top,
            ui->input_top,
            GA_Width,
            ui->input_width,
            GA_Height,
            ui->input_height,
            TAG_DONE);
        if (needs_refresh) {
            RefreshGList(ui->input_gadget, ui->window, NULL, 1);
        }
    }

    if (ui->input_scroll_gadget != NULL) {
        needs_refresh = SetGadgetAttrs(
            ui->input_scroll_gadget,
            ui->window,
            NULL,
            GA_Left,
            ui->input_scroll_left,
            GA_Top,
            ui->input_top,
            GA_Width,
            ui->input_scroll_width,
            GA_Height,
            ui->input_height,
            TAG_DONE);
        if (needs_refresh) {
            RefreshGList(ui->input_scroll_gadget, ui->window, NULL, 1);
        }
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

    RefreshGList(ui->gadgets, ui->window, NULL, -1);
    if (ui->input_scroll_gadget != NULL) {
        RefreshGList(ui->input_scroll_gadget, ui->window, NULL, -1);
    }
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
    transcript_append(&ui->transcript, "Type your message and press Send.");
}

static BOOL open_libraries(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    GadToolsBase = OpenLibrary("gadtools.library", 39);
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
    if (TextFieldBase != NULL) {
        CloseLibrary(TextFieldBase);
        TextFieldBase = NULL;
        TextFieldClass = NULL;
    }
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
        ui->input_scroll_left,
        GA_Top,
        ui->input_top,
        GA_Width,
        ui->input_scroll_width,
        GA_Height,
        ui->input_height,
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
        ui->input_left,
        GA_Top,
        ui->input_top,
        GA_Width,
        ui->input_width,
        GA_Height,
        ui->input_height,
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

    AddGList(ui->window, ui->input_scroll_gadget, -1, -1, NULL);
    RefreshGList(ui->input_scroll_gadget, ui->window, NULL, -1);

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
        IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_RAWKEY |
            IDCMP_VANILLAKEY | BUTTONIDCMP | LISTVIEWIDCMP,
        TAG_DONE);

    if (ui->window == NULL) {
        return FALSE;
    }

    layout_gadgets(ui);
    GT_RefreshWindow(ui->window, NULL);
    if (!create_textfield_input(ui)) {
        return FALSE;
    }
    ActivateGadget(ui->input_gadget, ui->window, NULL);

    return TRUE;
}

static void close_app_ui(struct AppUi *ui)
{
    if (ui->window != NULL && ui->input_scroll_gadget != NULL) {
        RemoveGList(ui->window, ui->input_scroll_gadget, -1);
    }
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

static void handle_send(struct AppUi *ui)
{
    char *input_text;
    ULONG allocated_size;

    input_text = copy_input_text(ui, &allocated_size);
    if (input_text == NULL) {
        set_status(ui, "Could not read input");
        ActivateGadget(ui->input_gadget, ui->window, NULL);
        return;
    }

    if (!text_has_content(input_text)) {
        FreeMem(input_text, allocated_size);
        ActivateGadget(ui->input_gadget, ui->window, NULL);
        return;
    }

    append_input_to_transcript(&ui->transcript, input_text);
    transcript_append(&ui->transcript, "AmiChatGPT: GUI step ready. Bridge comes next.");
    refresh_transcript(ui);
    set_status(ui, "Ready - not connected");
    clear_input_text(ui);

    FreeMem(input_text, allocated_size);
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
                    if (ui->input_scroll_gadget != NULL) {
                        RefreshGList(ui->input_scroll_gadget, ui->window, NULL, -1);
                    }
                    break;

                case IDCMP_NEWSIZE:
                    layout_gadgets(ui);
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
