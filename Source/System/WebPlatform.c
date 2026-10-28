#ifdef __EMSCRIPTEN__
#include "game.h"

static bool suspended;
static double previousFrame;

// Asyncify preserves the native screen stack while the browser presents a frame.
// clang-format off
EM_ASYNC_JS(void, NextBrowserFrame, (), {
	await new Promise(resolve => requestAnimationFrame(resolve));
});
// clang-format on

void WebFrame(void)
{
	NextBrowserFrame();
	while (suspended)
		emscripten_sleep(100);

	double now = emscripten_get_now();
	double elapsed = previousFrame ? (now - previousFrame) / 1000.0 : 1.0 / 60.0;
	previousFrame = now;
	gFramesPerSecondFrac = fmax(1.0 / 300.0, fmin(1.0 / 13.0, elapsed));
	gFramesPerSecond = 1.0f / gFramesPerSecondFrac;
}

EMSCRIPTEN_KEEPALIVE void WebResize(int width, int height)
{
	if (gSDLWindow && width > 0 && height > 0)
		SDL_SetWindowSize(gSDLWindow, width, height);
}

EMSCRIPTEN_KEEPALIVE void WebClearInput(void)
{
	SDL_ResetKeyboard();
	InvalidateAllInputs();
	SDL_GetRelativeMouseState(NULL, NULL);
}

EMSCRIPTEN_KEEPALIVE void WebSuspend(int value)
{
	if (suspended == (value != 0))
		return;
	suspended = value != 0;
	previousFrame = 0;
	WebClearInput();
	// Suspend the device, rather than changing the game's own pause/music state.
	EM_ASM({
        const audio = Module['SDL3']?.audioContext;
        if (!audio) return;
        if ($0) {
            Module.webAudioWasRunning = audio.state === 'running';
            if (Module.webAudioWasRunning) audio.suspend().catch(() => {});
        } else if (Module.webAudioWasRunning) {
            audio.resume().catch(() => Module.reportWarning('Click Enable sound to resume audio.'));
        }
    }, value);
}

EMSCRIPTEN_KEEPALIVE void WebCaptureMouse(void)
{
	if (gSDLWindow && gPlayNow && !gGamePaused)
		SDL_SetWindowRelativeMouseMode(gSDLWindow, true);
}

void WebSetMouseCapture(int capture)
{
	// Entering gameplay can happen after asynchronous loading, outside a gesture.
	// Request pointer lock on the next canvas click; keyboard/gamepad stay usable.
	EM_ASM({
        Module.webWantsPointerLock = !!$0;
        if (!$0) document.exitPointerLock?.();
    }, capture);
	if (!capture)
		SDL_SetWindowRelativeMouseMode(gSDLWindow, false);
}
#endif
