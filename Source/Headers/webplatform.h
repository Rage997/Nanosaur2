#pragma once
#include <emscripten.h>

void WebFrame(void);
void WebResize(int width, int height);
void WebSuspend(int suspended);
void WebClearInput(void);
void WebSetMouseCapture(int capture);
