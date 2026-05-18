#ifndef PROTO_TEXTFIELD_H
#define PROTO_TEXTFIELD_H
#include <exec/types.h>
#include <intuition/classes.h>
extern struct Library *TextFieldBase;
extern Class *TextFieldClass;
Class *TEXTFIELD_GetClass(void);
char *TEXTFIELD_GetCopyright(void);
#include <pragmas/textfield_pragmas.h>
#endif
