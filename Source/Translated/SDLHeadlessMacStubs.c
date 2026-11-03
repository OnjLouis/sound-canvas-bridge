typedef struct SDL_Window SDL_Window;
typedef enum SDL_bool {
    SDL_FALSE = 0,
    SDL_TRUE = 1,
} SDL_bool;

SDL_bool Cocoa_IsWindowInFullscreenSpace(SDL_Window *window) {
    (void)window;
    return SDL_FALSE;
}

SDL_bool Cocoa_SetWindowFullscreenSpace(SDL_Window *window, SDL_bool state) {
    (void)window;
    (void)state;
    return SDL_FALSE;
}
