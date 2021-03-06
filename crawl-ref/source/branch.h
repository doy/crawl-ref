/*
 *  File:       branch.h
 *  Summary:    Dungeon branch classes
 *  Written by: Haran Pilpel
 */

#ifndef BRANCH_H
#define BRANCH_H

#include "enum.h"

struct fog_machine_data;

enum branch_flag_type
{
    BFLAG_NONE = 0,

    BFLAG_NO_TELE_CONTROL = (1 << 0), // Teleport control not allowed.
    BFLAG_NOT_MAPPABLE    = (1 << 1), // Branch levels not mappable.
    BFLAG_NO_MAGIC_MAP    = (1 << 2), // Branch levels can't be magic mapped.
    BFLAG_HAS_ORB         = (1 << 3), // Orb is on the floor in this branch

    BFLAG_ISLANDED        = (1 << 4)  // May have isolated zones with no stairs.
};

struct Branch
{
    branch_type id;
    branch_type parent_branch;
    int depth;
    int startdepth;             // which level of the parent branch,
                                // 1 for first level
    unsigned long branch_flags;
    unsigned long default_level_flags;
    dungeon_feature_type entry_stairs;
    dungeon_feature_type exit_stairs;
    const char* shortname;      // "Slime Pits"
    const char* longname;       // "The Pits of Slime"
    const char* abbrevname;     // "Slime"
    const char* entry_message;
    bool has_shops;
    bool has_uniques;
    char floor_colour;          // Zot needs special handling.
    char rock_colour;
    int       (*mons_rarity_function)(int);
    int       (*mons_level_function)(int);
    int       (*num_traps_function)(int);
    trap_type (*rand_trap_function)(int);
    int       (*num_fogs_function)(int);
    void      (*rand_fog_function)(int,fog_machine_data&);
    int altar_chance;            // in percent
    int travel_shortcut;         // Which key to press for travel.
    bool any_upstair_exits;      // any upstair exits the branch (Hell branches)
    bool dangerous_bottom_level; // bottom level is more dangerous than normal
};

extern Branch branches[];

Branch& your_branch();

bool at_branch_bottom();

branch_type str_to_branch(const std::string &branch,
                          branch_type err = NUM_BRANCHES);

const char *level_area_type_name(int level_type);
level_area_type str_to_level_area_type(const std::string &s);

bool set_branch_flags(unsigned long flags, bool silent = false,
                      branch_type branch = NUM_BRANCHES);
bool unset_branch_flags(unsigned long flags, bool silent = false,
                        branch_type branch = NUM_BRANCHES);
unsigned long get_branch_flags(branch_type branch = NUM_BRANCHES);

#endif
