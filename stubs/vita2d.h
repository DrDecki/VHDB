#ifndef STUB_VITA2D_H
#define STUB_VITA2D_H
#include <stdint.h>
#define RGBA8(r, g, b, a) ((((a) & 0xFF) << 24) | (((b) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((r) & 0xFF))
typedef struct vita2d_pgf vita2d_pgf;
typedef struct vita2d_texture vita2d_texture;
int vita2d_init(void);
int vita2d_fini(void);
void vita2d_set_clear_color(unsigned int color);
void vita2d_start_drawing(void);
void vita2d_end_drawing(void);
void vita2d_clear_screen(void);
void vita2d_swap_buffers(void);
void vita2d_wait_rendering_done(void);
void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned int color);
vita2d_pgf *vita2d_load_default_pgf(void);
void vita2d_free_pgf(vita2d_pgf *font);
int vita2d_pgf_draw_text(vita2d_pgf *font, int x, int y, unsigned int color, float scale, const char *text);
int vita2d_pgf_text_width(vita2d_pgf *font, float scale, const char *text);
vita2d_texture *vita2d_load_PNG_file(const char *filename);
void vita2d_free_texture(vita2d_texture *texture);
void vita2d_draw_texture_scale(const vita2d_texture *texture, float x, float y, float sx, float sy);
#endif
