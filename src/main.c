#include <stdio.h>

#ifndef AMICHATGPT_VERSION
#define AMICHATGPT_VERSION "0.0.0-dev"
#endif

#define DEFAULT_BRIDGE_HOST "192.168.1.50"
#define DEFAULT_BRIDGE_PORT "6464"
#define DEFAULT_TEXT_WIDTH "72"

static void write_scaffold_message(FILE *out)
{
    fputs("AmiChatGPT " AMICHATGPT_VERSION "\n", out);
    fputs("Workbench 3.x ChatGPT64 bridge client scaffold\n", out);
    fputs("\n", out);
    fputs("Default bridge:\n", out);
    fputs("  host: " DEFAULT_BRIDGE_HOST "\n", out);
    fputs("  port: " DEFAULT_BRIDGE_PORT "\n", out);
    fputs("  width: " DEFAULT_TEXT_WIDTH "\n", out);
    fputs("\n", out);
    fputs("Next milestone: native Intuition/GadTools GUI.\n", out);
}

int main(int argc, char **argv)
{
#ifdef AMIGA_BUILD
    char line[8];
    FILE *window;
#endif

    (void)argc;
    (void)argv;

#ifdef AMIGA_BUILD
    window = fopen("CON:40/40/560/145/AmiChatGPT/CLOSE/WAIT", "r+");
    if (window != NULL) {
        write_scaffold_message(window);
        fputs("\nPress Return to close.\n", window);
        fflush(window);
        (void)fgets(line, sizeof(line), window);
        fclose(window);
        return 0;
    }
#endif

    write_scaffold_message(stdout);

    return 0;
}
