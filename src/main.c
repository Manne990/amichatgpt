#include <stdio.h>

#ifndef AMICHATGPT_VERSION
#define AMICHATGPT_VERSION "0.0.0-dev"
#endif

#define DEFAULT_BRIDGE_HOST "192.168.1.50"
#define DEFAULT_BRIDGE_PORT "6464"
#define DEFAULT_TEXT_WIDTH "72"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    puts("AmiChatGPT " AMICHATGPT_VERSION);
    puts("Workbench 3.x ChatGPT64 bridge client scaffold");
    puts("");
    puts("Default bridge:");
    puts("  host: " DEFAULT_BRIDGE_HOST);
    puts("  port: " DEFAULT_BRIDGE_PORT);
    puts("  width: " DEFAULT_TEXT_WIDTH);
    puts("");
    puts("Next milestone: native Intuition/GadTools GUI.");

    return 0;
}

