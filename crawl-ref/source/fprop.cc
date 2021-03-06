/*
 * File:      envmap.cc
 * Summary:   Functions dealing with env.map_knowledge.
 */

#include "AppHdr.h"

#include "fprop.h"

#include "coord.h"
#include "env.h"
#include "stuff.h"

bool is_sanctuary(const coord_def& p)
{
    if (!map_bounds(p))
        return (false);
    return (testbits(env.pgrid(p), FPROP_SANCTUARY_1)
            || testbits(env.pgrid(p), FPROP_SANCTUARY_2));
}

bool is_bloodcovered(const coord_def& p)
{
    return (testbits(env.pgrid(p), FPROP_BLOODY));
}

bool is_tide_immune(const coord_def &p)
{
    return (env.pgrid(p) & FPROP_NO_TIDE);
}

feature_property_type str_to_fprop(const std::string &str)
{
    if (str == "bloody")
        return (FPROP_BLOODY);
    if (str == "no_cloud_gen")
        return (FPROP_NO_CLOUD_GEN);
    if (str == "no_rtele_into")
        return (FPROP_NO_RTELE_INTO);
    if (str == "no_ctele_into")
        return (FPROP_NO_CTELE_INTO);
    if (str == "no_tele_into")
        return (FPROP_NO_TELE_INTO);
    if (str == "no_tide")
        return (FPROP_NO_TIDE);

    return (FPROP_NONE);
}
