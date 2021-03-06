/*
 *  File:       losparam.h
 *  Summary:    Parameters for the LOS algorithm
 */

#ifndef LOSPARAM_H
#define LOSPARAM_H

// Note: find_ray relies on the fact that 2*OPC_HALF == OPC_OPAQUE.
// On the other hand, losight tracks this explicitly.
enum opacity_type
{
    OPC_CLEAR  = 0,
    OPC_HALF   = 1,    // for opaque clouds; two or more block
    OPC_OPAQUE = 2,

    NUM_OPACITIES
};

struct opacity_func
{
    virtual opacity_type operator()(const coord_def& p) const = 0;
    virtual ~opacity_func() {}
    virtual opacity_func* clone() const = 0;
};

#define CLONE(typename) \
    typename* clone() const \
    { \
        return (new typename(*this)); \
    }

// Default LOS rules.
struct opacity_default : opacity_func
{
    CLONE(opacity_default)

    opacity_type operator()(const coord_def& p) const;
};
static opacity_default opc_default;

// Default LOS rules, but only consider fully opaque features blocking.
// In particular, clouds don't affect the result.
struct opacity_fullyopaque : opacity_func
{
    CLONE(opacity_fullyopaque)

    opacity_type operator()(const coord_def& p) const;
};
static opacity_fullyopaque opc_fullyopaque;

// Make transparent walls block in addition to normal LOS.
struct opacity_no_trans : opacity_func
{
    CLONE(opacity_no_trans)

    opacity_type operator()(const coord_def& p) const;
};
static opacity_no_trans opc_no_trans;

// Make anything solid block in addition to normal LOS.
struct opacity_solid : opacity_func
{
    CLONE(opacity_solid)

    opacity_type operator()(const coord_def& p) const;
};
static opacity_solid opc_solid;

// Opacity for monster movement, based on the late monster_los.
struct opacity_monmove : opacity_func
{
    const monsters& mon;

    opacity_monmove(const monsters& m)
        : mon(m)
    {
    }

    CLONE(opacity_monmove)

    opacity_type operator()(const coord_def& p) const;
};

// Make any actor (as well as solid features) block.
// Note that the blocking actors are still "visible".
struct opacity_no_actor : opacity_func
{
    CLONE(opacity_no_actor)

    opacity_type operator()(const coord_def& p) const;
};
static opacity_no_actor opc_no_actor;

// Subclasses of this are passed to losight() to modify the
// LOS calculation. Implementations will have to translate between
// relative coordinates (-8,-8)..(8,8) and real coordinates,
// specify how opaque the cells are and determine what values
// are written to the visible cells.
struct los_param
{
    virtual ~los_param() {}

    // Whether the translated coordinate lies within the map
    // (including boundary) and within the LOS area
    virtual bool los_bounds(const coord_def& p) const = 0;

    virtual opacity_type opacity(const coord_def& p) const = 0;
};

#endif
