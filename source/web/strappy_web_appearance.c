#include "strappy_palette.h"

#include <emscripten/emscripten.h>

/* The browser shell consumes the same compiled palette as the C conversation
 * renderer. CSS owns layout; shared C owns Strappy's visual identity. */
EMSCRIPTEN_KEEPALIVE
const char *strappy_web_appearance_css_variables(void)
{
  return ":root{"
    "--background:" STRAPPY_PALETTE_PAGE_BACKGROUND ";"
    "--panel:" STRAPPY_PALETTE_PANEL_BACKGROUND ";"
    "--document:" STRAPPY_PALETTE_PAGE_BACKGROUND ";"
    "--line:" STRAPPY_PALETTE_BORDER ";"
    "--muted:" STRAPPY_PALETTE_SECONDARY_TEXT ";"
    "--text:" STRAPPY_PALETTE_PRIMARY_TEXT ";"
    "--control-text:" STRAPPY_PALETTE_CONTROL_TEXT ";"
    "--accent:" STRAPPY_PRIMARY_TINT_HEX ";"
    "--accent-active:" STRAPPY_DARK_TINT_HEX ";"
    "--accent-soft:" STRAPPY_LIGHT_PURPLE_HEX ";"
    "--selection:" STRAPPY_MUTED_PURPLE_HEX ";"
    "--section:" STRAPPY_PALETTE_SECTION_BACKGROUND ";"
    "--section-header:" STRAPPY_SECTION_HEADER_TINT_HEX ";"
    "--surface:" STRAPPY_PALETTE_SURFACE_BACKGROUND ";"
    "--strong-surface:" STRAPPY_PALETTE_STRONG_SURFACE ";"
    "--strong-border:" STRAPPY_PALETTE_STRONG_BORDER ";"
    "--danger:" STRAPPY_PALETTE_ERROR ";"
    "--danger-background:" STRAPPY_PALETTE_ERROR_BACKGROUND ";"
    "--control:" STRAPPY_PALETTE_TERMINAL_BACKGROUND ";"
  "}";
}
