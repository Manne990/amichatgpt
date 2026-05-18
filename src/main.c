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

#define GID_TRANSCRIPT 1
#define GID_INPUT 2
#define GID_SEND 3
#define GID_STATUS 4

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
    struct Gadget *send_gadget;
    struct Gadget *status_gadget;
    struct Window *window;
    struct ChatTranscript transcript;
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

    if (ui->window == NULL || ui->transcript_gadget == NULL) {
        return;
    }

    top = 0;
    if (ui->transcript.count > 9) {
        top = ui->transcript.count - 9;
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
    transcript_append(&ui->transcript, "Type a line and press Send.");
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

    init_new_gadget(&ng, ui->visual_info, 12, 18, 500, 118, NULL, GID_TRANSCRIPT);
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

    init_new_gadget(&ng, ui->visual_info, 12, 146, 390, 16, NULL, GID_INPUT);
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
    ui->input_gadget = tail;

    init_new_gadget(&ng, ui->visual_info, 414, 144, 70, 20, "Send", GID_SEND);
    tail = CreateGadget(BUTTON_KIND, tail, &ng, TAG_DONE);
    if (tail == NULL) {
        return FALSE;
    }
    ui->send_gadget = tail;

    init_new_gadget(&ng, ui->visual_info, 12, 172, 260, 14, NULL, GID_STATUS);
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
        530,
        WA_Height,
        205,
        WA_DepthGadget,
        TRUE,
        WA_DragBar,
        TRUE,
        WA_CloseGadget,
        TRUE,
        WA_Activate,
        TRUE,
        WA_Gadgets,
        ui->gadgets,
        WA_IDCMP,
        IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | STRINGIDCMP | LISTVIEWIDCMP,
        TAG_DONE);

    if (ui->window == NULL) {
        return FALSE;
    }

    GT_RefreshWindow(ui->window, NULL);
    ActivateGadget(ui->input_gadget, ui->window, NULL);

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

static void handle_send(struct AppUi *ui)
{
    struct StringInfo *string_info;
    char input[INPUT_TEXT_LEN];

    string_info = (struct StringInfo *)ui->input_gadget->SpecialInfo;
    if (string_info == NULL || string_info->Buffer == NULL) {
        return;
    }

    strncpy(input, string_info->Buffer, INPUT_TEXT_LEN - 1);
    input[INPUT_TEXT_LEN - 1] = '\0';
    if (input[0] == '\0') {
        ActivateGadget(ui->input_gadget, ui->window, NULL);
        return;
    }

    transcript_append_prefixed(&ui->transcript, "You: ", input);
    transcript_append(&ui->transcript, "AmiChatGPT: GUI step ready. Bridge comes next.");
    refresh_transcript(ui);
    set_status(ui, "Ready - not connected");

    GT_SetGadgetAttrs(
        ui->input_gadget,
        ui->window,
        NULL,
        GTST_String,
        "",
        TAG_DONE);
    ActivateGadget(ui->input_gadget, ui->window, NULL);
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

                case IDCMP_GADGETUP:
                    if (gadget_id == GID_SEND || gadget_id == GID_INPUT) {
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
