/*
 *  File:       tilesdl.h
 *  Summary:    SDL-related functionality for the tiles port
 *  Written by: Enne Walker
 */

#ifdef USE_TILE
#ifndef TILESDL_H
#define TILESDL_H

#include "externs.h"
#include "tilereg.h"

enum key_mod
{
    MOD_SHIFT = 0x1,
    MOD_CTRL  = 0x2,
    MOD_ALT   = 0x4
};

struct MouseEvent
{
    enum mouse_event_type
    {
        PRESS,
        RELEASE,
        MOVE
    };

    enum mouse_event_button
    {
        NONE        = 0x00,
        LEFT        = 0x01,
        MIDDLE      = 0x02,
        RIGHT       = 0x04,
        SCROLL_UP   = 0x08,
        SCROLL_DOWN = 0x10
    };

    // kind of event
    mouse_event_type event;
    // if PRESS or RELEASE, the button pressed
    mouse_event_button button;
    // bitwise-or of buttons currently pressed
    unsigned short held;
    // bitwise-or of key mods currently pressed
    unsigned char mod;
    // location of events in pixels and in window coordinate space
    unsigned int px;
    unsigned int py;
};

class SDL_Surface;
class FTFont;

class TilesFramework
{
public:
    TilesFramework();
    virtual ~TilesFramework();

    bool initialise();
    void shutdown();
    void load_dungeon(unsigned int *tileb, const coord_def &gc);
    void load_dungeon(const coord_def &gc);
    int getch_ck();
    void resize();
    void calculate_default_options();
    void clrscr();

    void message_out(int *which_line, int colour, const char *s, int firstcol);

    void cgotoxy(int x, int y, GotoRegion region = GOTO_CRT);
    GotoRegion get_cursor_region() const;
    int get_number_of_lines();
    int get_number_of_cols();
    void clear_message_window();

    void update_minimap(int gx, int gy);
    void update_minimap(int gx, int gy, map_feature f);
    void clear_minimap();
    void update_minimap_bounds();
    void update_spells();
    void update_inventory();

    void set_need_redraw();
    bool need_redraw() const;
    void redraw();

    void place_cursor(cursor_type type, const coord_def &gc);
    void clear_text_tags(text_tag_type type);
    void add_text_tag(text_tag_type type, const std::string &tag,
                      const coord_def &gc);
    void add_text_tag(text_tag_type type, const monsters* mon);

    bool initialise_items();

    const coord_def &get_cursor() const;

    void add_overlay(const coord_def &gc, int idx);
    void clear_overlays();

    void draw_title();
    void draw_doll_edit();

    MenuRegion *get_menu() { return m_region_menu; }
protected:
    int load_font(const char *font_file, int font_size,
                  bool default_on_fail, bool outline);
    int handle_mouse(MouseEvent &event);

    void use_control_region(ControlRegion *region);

    // screen pixel dimensions
    coord_def m_windowsz;
    // screen pixels per view cell
    coord_def m_viewsc;

    SDL_Surface* m_context;
    bool m_fullscreen;
    bool m_need_redraw;

    enum LayerID
    {
        LAYER_NORMAL,
        LAYER_CRT,
        LAYER_TILE_CONTROL,
        LAYER_MAX
    };

    class Layer
    {
    public:
        // Layers don't own these regions
        std::vector<Region*> m_regions;
    };
    Layer m_layers[LAYER_MAX];
    LayerID m_active_layer;

    // Normal layer
    DungeonRegion   *m_region_tile;
    StatRegion      *m_region_stat;
    MessageRegion   *m_region_msg;
    MapRegion       *m_region_map;
    InventoryRegion *m_region_inv;

    // Full-screen CRT layer
    CRTRegion       *m_region_crt;
    MenuRegion      *m_region_menu;

    struct font_info
    {
        std::string name;
        int size;
        bool outline;
        FTFont *font;
    };
    std::vector<font_info> m_fonts;
    int m_msg_font;
    int m_tip_font;

    void do_layout();
    bool layout_statcol(bool message_overlay, bool show_gold_turns);

    ImageManager m_image;

    // Mouse state.
    unsigned short m_buttons_held;
    unsigned char m_key_mod;
    coord_def m_mouse;
    unsigned int m_last_tick_moved;

    std::string m_tooltip;

    int m_screen_width;
    int m_screen_height;
};

// Main interface for tiles functions
extern TilesFramework tiles;

#ifdef TARGET_COMPILER_MINGW
#ifndef alloca
// Srsly, MinGW, wtf?
void *alloca(size_t);
#endif
#endif

#endif
#endif
