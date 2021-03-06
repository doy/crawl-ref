/*
 *  File:       tilebuf.cc
 *  Summary:    Vertex buffer implementaions
 *
 *  Created by: ennewalker on Sat Jan 5 01:33:53 2008 UTC
 */

#include "AppHdr.h"

#ifdef USE_TILE

#include "tilebuf.h"
#include "tilefont.h"

#include <SDL.h>
#include <SDL_opengl.h>

/////////////////////////////////////////////////////////////////////////////
// GLState

// Note: these defaults should match the OpenGL defaults
GLState::GLState() :
    array_vertex(false),
    array_texcoord(false),
    array_colour(false),
    blend(false),
    texture(false),
    depthtest(false),
    alphatest(false),
    alpharef(0)
{
}

/////////////////////////////////////////////////////////////////////////////
// GLStateManager

void GLStateManager::init()
{
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0, 0.0, 0.0, 1.0f);
    glDepthFunc(GL_LEQUAL);
}

void GLStateManager::set(const GLState& state)
{
    if (state.array_vertex)
        glEnableClientState(GL_VERTEX_ARRAY);
    else
        glDisableClientState(GL_VERTEX_ARRAY);

    if (state.array_texcoord)
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    else
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    if (state.array_colour)
    {
        glEnableClientState(GL_COLOR_ARRAY);
    }
    else
    {
        glDisableClientState(GL_COLOR_ARRAY);

        // [enne] This should *not* be necessary, but the Linux OpenGL
        // driver that I'm using sets this to the last colour of the
        // colour array.  So, we need to unset it here.
        glColor3f(1.0f, 1.0f, 1.0f);
    }

    if (state.texture)
        glEnable(GL_TEXTURE_2D);
    else
        glDisable(GL_TEXTURE_2D);

    if (state.blend)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);

    if (state.depthtest)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);

    if (state.alphatest)
    {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_NOTEQUAL, state.alpharef);
    }
    else
        glDisable(GL_ALPHA_TEST);
}

/////////////////////////////////////////////////////////////////////////////
// VColour

VColour VColour::white(255, 255, 255, 255);
VColour VColour::black(0, 0, 0, 255);
VColour VColour::transparent(0, 0, 0, 0);

/////////////////////////////////////////////////////////////////////////////
// VertBuffer

#ifdef DEBUG
static bool _valid(int num_verts, int prim)
{
    switch (prim)
    {
    case GL_QUADS:
        return (num_verts % 4 == 0);
    case GL_TRIANGLES:
        return (num_verts % 3 == 0);
    case GL_LINES:
        return (num_verts % 2 == 0);
    case GL_POINTS:
        return (true);
    default:
        return (false);
    }
}
#endif

template<>
void VertBuffer<PTVert>::init_state()
{
    m_state.array_vertex   = true;
    m_state.array_texcoord = true;
    m_state.blend          = true;
    m_state.texture        = true;
}

template<>
void VertBuffer<PTVert>::draw() const
{
    if (size() == 0)
        return;

    ASSERT(_valid(size(), m_prim));
    ASSERT(m_tex);

    GLStateManager::set(m_state);

    m_tex->bind();
    glVertexPointer(2, GL_FLOAT, sizeof(Vert), &(*this)[0].pos_x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vert), &(*this)[0].tex_x);
    glDrawArrays(m_prim, 0, size());
}

template<>
void VertBuffer<PCVert>::init_state()
{
    m_state.array_vertex = true;
    m_state.array_colour = true;
    m_state.blend        = true;
    m_state.texture      = false;
}

template<>
void VertBuffer<PCVert>::draw() const
{
    if (size() == 0)
        return;

    ASSERT(_valid(size(), m_prim));
    ASSERT(!m_tex);

    GLStateManager::set(m_state);

    glVertexPointer(2, GL_FLOAT, sizeof(Vert), &(*this)[0].pos_x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vert), &(*this)[0].col);
    glDrawArrays(m_prim, 0, size());
}

template<>
void VertBuffer<PTCVert>::init_state()
{
    m_state.array_vertex   = true;
    m_state.array_texcoord = true;
    m_state.array_colour   = true;
    m_state.blend          = true;
    m_state.texture        = true;
}

template<>
void VertBuffer<PTCVert>::draw() const
{
    if (size() == 0)
        return;

    ASSERT(_valid(size(), m_prim));
    ASSERT(m_tex);

    GLStateManager::set(m_state);

    m_tex->bind();
    glVertexPointer(2, GL_FLOAT, sizeof(Vert), &(*this)[0].pos_x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vert), &(*this)[0].tex_x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vert), &(*this)[0].col);
    glDrawArrays(m_prim, 0, size());
}

template<>
void VertBuffer<P3TCVert>::init_state()
{
    m_state.array_vertex   = true;
    m_state.array_texcoord = true;
    m_state.array_colour   = true;
    m_state.blend          = true;
    m_state.texture        = true;
    m_state.depthtest      = true;
    m_state.alphatest      = true;
    m_state.alpharef       = 0;
}

template<>
void VertBuffer<P3TCVert>::draw() const
{
    if (size() == 0)
        return;

    ASSERT(_valid(size(), m_prim));
    ASSERT(m_tex);

    GLStateManager::set(m_state);

    m_tex->bind();
    glVertexPointer(3, GL_FLOAT, sizeof(Vert), &(*this)[0].pos_x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vert), &(*this)[0].tex_x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vert), &(*this)[0].col);
    glDrawArrays(m_prim, 0, size());
}

/////////////////////////////////////////////////////////////////////////////
// FontBuffer

FontBuffer::FontBuffer(FTFont *font) :
    VertBuffer<PTCVert>(font->font_tex(), GL_QUADS), m_font(font)
{
    ASSERT(m_font);
    ASSERT(m_tex);
}

void FontBuffer::add(const formatted_string &fs, float x, float y)
{
    m_font->store(*this, x, y, fs);
    ASSERT(_valid(size(), m_prim));
}

void FontBuffer::add(const std::string &s, const VColour &col, float x, float y)
{
    m_font->store(*this, x, y, s, col);
    ASSERT(_valid(size(), m_prim));
}

/////////////////////////////////////////////////////////////////////////////
// TileBuffer

TileBuffer::TileBuffer(const TilesTexture *t) : VertBuffer<PTVert>(t, GL_QUADS)
{
}

void TileBuffer::add_unscaled(int idx, float x, float y, int ymax)
{
    float pos_sx = x;
    float pos_sy = y;
    float pos_ex, pos_ey, tex_sx, tex_sy, tex_ex, tex_ey;
    TilesTexture *tex = (TilesTexture *)m_tex;
    bool drawn = tex->get_coords(idx, 0, 0,
                                 pos_sx, pos_sy, pos_ex, pos_ey,
                                 tex_sx, tex_sy, tex_ex, tex_ey,
                                 true, -1, ymax, 1.0f, 1.0f);

    if (!drawn)
        return;

    size_t last = std::vector<Vert>::size();
    std::vector<Vert>::resize(last + 4);

    {
        Vert &v = (*this)[last];
        v.pos_x = pos_sx;
        v.pos_y = pos_sy;
        v.tex_x = tex_sx;
        v.tex_y = tex_sy;
    }
    {
        Vert &v = (*this)[last + 1];
        v.pos_x = pos_sx;
        v.pos_y = pos_ey;
        v.tex_x = tex_sx;
        v.tex_y = tex_ey;
    }
    {
        Vert &v = (*this)[last + 2];
        v.pos_x = pos_ex;
        v.pos_y = pos_ey;
        v.tex_x = tex_ex;
        v.tex_y = tex_ey;
    }
    {
        Vert &v = (*this)[last + 3];
        v.pos_x = pos_ex;
        v.pos_y = pos_sy;
        v.tex_x = tex_ex;
        v.tex_y = tex_sy;
    }

    ASSERT(_valid(size(), m_prim));
}

void TileBuffer::add(int idx, int x, int y, int ox, int oy, bool centre, int ymax)
{
    float pos_sx = x;
    float pos_sy = y;
    float pos_ex, pos_ey, tex_sx, tex_sy, tex_ex, tex_ey;
    TilesTexture *tex = (TilesTexture *)m_tex;
    bool drawn = tex->get_coords(idx, ox, oy,
                                 pos_sx, pos_sy, pos_ex, pos_ey,
                                 tex_sx, tex_sy, tex_ex, tex_ey,
                                 centre, -1, ymax, TILE_X, TILE_Y);

    if (!drawn)
        return;

    size_t last = std::vector<Vert>::size();
    std::vector<Vert>::resize(last + 4);

    {
        Vert &v = (*this)[last];
        v.pos_x = pos_sx;
        v.pos_y = pos_sy;
        v.tex_x = tex_sx;
        v.tex_y = tex_sy;
    }

    {
        Vert &v = (*this)[last + 1];
        v.pos_x = pos_sx;
        v.pos_y = pos_ey;
        v.tex_x = tex_sx;
        v.tex_y = tex_ey;
    }

    {
        Vert &v = (*this)[last + 2];
        v.pos_x = pos_ex;
        v.pos_y = pos_ey;
        v.tex_x = tex_ex;
        v.tex_y = tex_ey;
    }

    {
        Vert &v = (*this)[last + 3];
        v.pos_x = pos_ex;
        v.pos_y = pos_sy;
        v.tex_x = tex_ex;
        v.tex_y = tex_sy;
    }
}

void TileBuffer::set_tex(const TilesTexture *new_tex)
{
    ASSERT(new_tex);
    m_tex = new_tex;
}

/////////////////////////////////////////////////////////////////////////////
// ColouredTileBuffer

ColouredTileBuffer::ColouredTileBuffer(const TilesTexture *t) :
    VertBuffer<P3TCVert>(t, GL_QUADS)
{
}

static unsigned char _get_alpha(float lerp, int alpha_top, int alpha_bottom)
{
    if (lerp < 0.0f)
        lerp = 0.0f;
    if (lerp > 1.0f)
        lerp = 1.0f;

    int ret = alpha_top * (1.0f - lerp) + alpha_bottom * lerp;

    ret = std::min(std::max(0, ret), 255);
    return (static_cast<unsigned char>(ret));
}

void ColouredTileBuffer::add(int idx, int x, int y, int z,
                             int ox,  int oy, int ymin, int ymax,
                             int alpha_top, int alpha_bottom)
{
    float pos_sx = x;
    float pos_sy = y;
    float pos_ex, pos_ey, tex_sx, tex_sy, tex_ex, tex_ey;
    TilesTexture *tex = (TilesTexture *)m_tex;
    bool drawn = tex->get_coords(idx, ox, oy,
                                 pos_sx, pos_sy, pos_ex, pos_ey,
                                 tex_sx, tex_sy, tex_ex, tex_ey,
                                 true, -1, ymax);

    if (!drawn)
        return;

    float lerp_s = (pos_sy - y);
    float lerp_e = (pos_ey - y);

    // Need to calculate the alpha at the top and bottom.  As the quad
    // being drawn may not actually start on the grid boundary, we need
    // to figure out what alpha value its vertices should use.
    unsigned char alpha_s = _get_alpha(lerp_s, alpha_top, alpha_bottom);
    unsigned char alpha_e = _get_alpha(lerp_e, alpha_top, alpha_bottom);

    VColour col_sy(255, 255, 255, alpha_s);
    VColour col_ey(255, 255, 255, alpha_e);

    float pos_z = (float)z;

    size_t last = std::vector<Vert>::size();
    std::vector<Vert>::resize(last + 4);
    {
        Vert &v = (*this)[last];
        v.pos_x = pos_sx;
        v.pos_y = pos_sy;
        v.pos_z = pos_z;
        v.tex_x = tex_sx;
        v.tex_y = tex_sy;
        v.col = col_sy;
    }

    {
        Vert &v = (*this)[last + 1];
        v.pos_x = pos_sx;
        v.pos_y = pos_ey;
        v.pos_z = pos_z;
        v.tex_x = tex_sx;
        v.tex_y = tex_ey;
        v.col = col_ey;
    }

    {
        Vert &v = (*this)[last + 2];
        v.pos_x = pos_ex;
        v.pos_y = pos_ey;
        v.pos_z = pos_z;
        v.tex_x = tex_ex;
        v.tex_y = tex_ey;
        v.col = col_ey;
    }

    {
        Vert &v = (*this)[last + 3];
        v.pos_x = pos_ex;
        v.pos_y = pos_sy;
        v.pos_z = pos_z;
        v.tex_x = tex_ex;
        v.tex_y = tex_sy;
        v.col = col_sy;
    }
}

/////////////////////////////////////////////////////////////////////////////
// SubmergedTileBuffer


SubmergedTileBuffer::SubmergedTileBuffer(const TilesTexture *tex,
                                         int mask_idx, int above_max,
                                         int below_min) :
    m_mask_idx(mask_idx),
    m_above_max(above_max),
    m_below_min(below_min),
    m_max_z(-1000),
    m_below_water(tex),
    m_mask(tex),
    m_above_water(tex)
{
    // Adjust the state of the mask buffer so that we can use it to mask out
    // the bottom half of the tile when drawing the character a second time.
    // See the draw() function for more details.
    GLState &state = m_mask.state();
    state.blend = true;
    state.depthtest = true;
    state.alphatest = true;
    state.alpharef = 255;
}

void SubmergedTileBuffer::add(int idx, int x, int y, int z, bool submerged,
                              bool ghost, int ox, int oy, int ymax)
{
    // Keep track of the maximum z.  We'll use this for drawing the mask
    // later, to ensure it is closer than all other tiles drawn.
    m_max_z = std::max(m_max_z, z);

    // Arbitrary alpha values for the top and bottom.
    int alpha_top = ghost ? 100 : 255;
    int alpha_bottom = ghost ? 0 : 40;

    if (submerged)
    {
        m_below_water.add(idx, x, y, z, ox, oy, m_below_min, ymax,
                          alpha_top, alpha_bottom);

        m_mask.add(m_mask_idx, x, y, 0, 0, 0, -1, -1, 255, 255);
        int above_max = (ymax == -1) ? m_above_max
                                     : std::min(ymax, m_above_max);
        m_above_water.add(idx, x, y, z, ox, oy, -1, above_max,
                          alpha_top, alpha_top);
    }
    else
    {
        // Unsubmerged tiles don't need a mask.
        m_above_water.add(idx, x, y, z, ox, oy, -1, ymax, alpha_top, alpha_top);
    }
}

void SubmergedTileBuffer::draw() const
{
    // First, draw anything below water.  Alpha blending is turned on,
    // so these tiles will blend with anything previously drawn.
    m_below_water.draw();

    // Second, draw all of the mask tiles.  The mask is white (255,255,255,255)
    // above water and transparent (0,0,0,0) below water.
    //
    // Alpha testing is turned on with a ref value of 255, so none of the
    // white pixels will be drawn because they will culled by alpha testing.
    // The transparent pixels below water will be "drawn", but because they
    // are fully transparent, they will not affect any colours on screen.
    // Instead, they will will set the z-buffer to a z-value (depth) that is
    // greater than the closest depth in either the below or above buffers.
    // Because all of the above water tiles are at lower z-values than this
    // maximum, they will not draw over these transparent pixels, effectively
    // masking them out. (My kingdom for a shader.)

    glPushMatrix();
    glTranslatef(0, 0, m_max_z + 1);
    m_mask.draw();
    glPopMatrix();

    // Now, draw all the above water tiles.  Some of these may draw over top
    // of part of the below water tiles, but that's fine as the mask will
    // take care of only drawing the correct parts.
    m_above_water.draw();
}

void SubmergedTileBuffer::clear()
{
    m_below_water.clear();
    m_mask.clear();
    m_above_water.clear();
}

/////////////////////////////////////////////////////////////////////////////
// ShapeBuffer

// [enne] - GL_POINTS should probably be used here, but there's (apparently)
// a bug in the OpenGL driver that I'm using and it doesn't respect
// glPointSize unless GL_SMOOTH_POINTS is on.  GL_SMOOTH_POINTS is
// *terrible* for performance if it has to fall back on software rendering,
// so instead we'll just make quads.
ShapeBuffer::ShapeBuffer() : VertBuffer<PCVert>(NULL, GL_QUADS)
{
}

void ShapeBuffer::add(float pos_sx, float pos_sy, float pos_ex, float pos_ey,
                      const VColour &col)
{
    {
        Vert &v = get_next();
        v.pos_x = pos_sx;
        v.pos_y = pos_sy;
        v.col = col;
    }
    {
        Vert &v = get_next();
        v.pos_x = pos_sx;
        v.pos_y = pos_ey;
        v.col = col;
    }
    {
        Vert &v = get_next();
        v.pos_x = pos_ex;
        v.pos_y = pos_ey;
        v.col = col;
    }
    {
        Vert &v = get_next();
        v.pos_x = pos_ex;
        v.pos_y = pos_sy;
        v.col = col;
    }

    ASSERT(_valid(size(), m_prim));
}

/////////////////////////////////////////////////////////////////////////////
// LineBuffer

LineBuffer::LineBuffer() : VertBuffer<PCVert>(NULL, GL_LINES)
{
}

void LineBuffer::add(float pos_sx, float pos_sy, float pos_ex, float pos_ey,
                     const VColour &col)
{
    {
        Vert &v = get_next();
        v.pos_x = pos_sx;
        v.pos_y = pos_sy;
        v.col = col;
    }
    {
        Vert &v = get_next();
        v.pos_x = pos_ex;
        v.pos_y = pos_ey;
        v.col = col;
    }

    ASSERT(_valid(size(), m_prim));
}

void LineBuffer::add_square(float sx, float sy, float ex, float ey,
                            const VColour &col)
{
    add(sx, sy, ex, sy, col);
    add(ex, sy, ex, ey, col);
    add(ex, ey, sx, ey, col);
    add(sx, ey, sx, sy, col);
}

#endif
