/*
 *  File:       godabil.cc
 *  Summary:    God-granted abilities.
 */

#include "AppHdr.h"

#include <queue>

#include "beam.h"
#include "cloud.h"
#include "colour.h"
#include "coord.h"
#include "coordit.h"
#include "database.h"
#include "directn.h"
#include "effects.h"
#include "env.h"
#include "files.h"
#include "godabil.h"
#include "invent.h"
#include "itemprop.h"
#include "items.h"
#include "kills.h"
#include "message.h"
#include "misc.h"
#include "mon-act.h"
#include "mon-behv.h"
#include "mon-iter.h"
#include "mon-place.h"
#include "mgen_data.h"
#include "mon-stuff.h"
#include "mon-util.h"
#include "mutation.h"
#include "options.h"
#include "random.h"
#include "religion.h"
#include "shopping.h"
#include "spells1.h"
#include "spells3.h"
#include "spells4.h"
#include "spl-book.h"
#include "spl-util.h"
#include "state.h"
#include "stuff.h"
#include "terrain.h"
#include "view.h"

bool yred_injury_mirror(bool actual)
{
    return (you.religion == GOD_YREDELEMNUL && !player_under_penance()
            && you.piety >= piety_breakpoint(1)
            && (!actual || you.duration[DUR_PRAYER]));
}

bool beogh_water_walk()
{
    return (you.religion == GOD_BEOGH && !player_under_penance()
            && you.piety >= piety_breakpoint(4));
}

bool jiyva_grant_jelly(bool actual)
{
    return (you.religion == GOD_JIYVA && !player_under_penance()
            && you.piety >= piety_breakpoint(2)
            && (!actual || you.duration[DUR_PRAYER]));
}

bool jiyva_remove_bad_mutation()
{
    if (!how_mutated())
    {
        mpr("You have no bad mutations to be cured!");
        return (false);
    }

    // Ensure that only bad mutations are removed.
    if (!delete_mutation(RANDOM_BAD_MUTATION, true, false, true, true))
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return (false);
    }

    mpr("You feel cleansed.");
    return (true);
}

bool vehumet_supports_spell(spell_type spell)
{
    if (spell_typematch(spell, SPTYP_CONJURATION | SPTYP_SUMMONING))
        return (true);

    if (spell == SPELL_SHATTER
        || spell == SPELL_FRAGMENTATION
        || spell == SPELL_SANDBLAST)
    {
        return (true);
    }

    return (false);
}

// Returns false if the invocation fails (no spellbooks in sight, etc.).
bool trog_burn_spellbooks()
{
    if (you.religion != GOD_TROG)
        return (false);

    god_acting gdact;

    for (stack_iterator si(you.pos()); si; ++si)
    {
        if (si->base_type == OBJ_BOOKS
            && si->sub_type != BOOK_MANUAL
            && si->sub_type != BOOK_DESTRUCTION)
        {
            mpr("Burning your own feet might not be such a smart idea!");
            return (false);
        }
    }

    int totalpiety = 0;

    for (radius_iterator ri(you.pos(), LOS_RADIUS, true, true, true); ri; ++ri)
    {
        // If a grid is blocked, books lying there will be ignored.
        // Allow bombing of monsters.
        const unsigned short cloud = env.cgrid(*ri);
        if (feat_is_solid(grd(*ri))
            || cloud != EMPTY_CLOUD && env.cloud[cloud].type != CLOUD_FIRE)
        {
            continue;
        }

        int count = 0;
        int rarity = 0;
        for (stack_iterator si(*ri); si; ++si)
        {
            if (si->base_type != OBJ_BOOKS
                || si->sub_type == BOOK_MANUAL
                || si->sub_type == BOOK_DESTRUCTION)
            {
                continue;
            }

            // Ignore {!D} inscribed books.
            if (!check_warning_inscriptions(*si, OPER_DESTROY))
            {
                mpr("Won't ignite {!D} inscribed book.");
                continue;
            }

            rarity += book_rarity(si->sub_type);
            // Piety increases by 2 for books never cracked open, else 1.
            // Conversely, rarity influences the duration of the pyre.
            if (!item_type_known(*si))
                totalpiety += 2;
            else
                totalpiety++;

            dprf("Burned book rarity: %d", rarity);
            destroy_item(si.link());
            count++;
        }

        if (count)
        {
            if (cloud != EMPTY_CLOUD)
            {
                // Reinforce the cloud.
                mpr("The fire roars with new energy!");
                const int extra_dur = count + random2(rarity / 2);
                env.cloud[cloud].decay += extra_dur * 5;
                env.cloud[cloud].set_whose(KC_YOU);
                continue;
            }

            const int duration = std::min(4 + count + random2(rarity/2), 23);
            place_cloud(CLOUD_FIRE, *ri, duration, KC_YOU);

            mprf(MSGCH_GOD, "The book%s burst%s into flames.",
                 count == 1 ? ""  : "s",
                 count == 1 ? "s" : "");
        }
    }

    if (!totalpiety)
    {
         mpr("You cannot see a spellbook to ignite!");
         return (false);
    }
    else
    {
         simple_god_message(" is delighted!", GOD_TROG);
         gain_piety(totalpiety);
    }

    return (true);
}

static bool _is_yred_enslaved_soul(const monsters* mon)
{
    return (mon->alive() && mons_enslaved_soul(mon));
}

static bool _yred_enslaved_souls_on_level_disappear()
{
    bool success = false;

    for (monster_iterator mi; mi; ++mi)
    {
        if (_is_yred_enslaved_soul(*mi))
        {
#ifdef DEBUG_DIAGNOSTICS
            mprf(MSGCH_DIAGNOSTICS, "Undead soul disappearing: %s on level %d, branch %d",
                 mi->name(DESC_PLAIN).c_str(),
                 static_cast<int>(you.your_level),
                 static_cast<int>(you.where_are_you));
#endif

            simple_monster_message(*mi, " is freed.");

            // The monster disappears.
            monster_die(*mi, KILL_DISMISSED, NON_MONSTER);

            success = true;
        }
    }

    return (success);
}

static bool _yred_souls_disappear()
{
    return (apply_to_all_dungeons(_yred_enslaved_souls_on_level_disappear));
}

void yred_make_enslaved_soul(monsters *mon, bool force_hostile,
                             bool quiet, bool unrestricted)
{
    if (!unrestricted)
        _yred_souls_disappear();

    const int type = mon->type;
    monster_type soul_type = mons_species(type);
    const std::string whose =
        you.can_see(mon) ? apostrophise(mon->name(DESC_CAP_THE))
                         : mon->pronoun(PRONOUN_CAP_POSSESSIVE);
    const bool twisted =
        !unrestricted ? !x_chance_in_y(you.skills[SK_INVOCATIONS] * 20 / 9 + 20,
                                       100)
                      : false;
    int corps = -1;

    // If the monster's held in a net, get it out.
    mons_clear_trapping_net(mon);

    const monsters orig = *mon;

    if (twisted)
    {
        mon->type = mons_zombie_size(soul_type) == Z_BIG ?
            MONS_ABOMINATION_LARGE : MONS_ABOMINATION_SMALL;
        mon->base_monster = MONS_NO_MONSTER;
    }
    else
    {
        // Drop the monster's corpse, so that it can be properly
        // re-equipped below.
        corps = place_monster_corpse(mon, true, true);
    }

    // Drop the monster's equipment.
    monster_drop_ething(mon);

    // Recreate the monster as an abomination, or as itself before
    // turning it into a spectral thing below.
    define_monster(*mon);

    mon->colour = ETC_UNHOLY;

    mon->flags |= MF_NO_REWARD;
    mon->flags |= MF_ENSLAVED_SOUL;

    if (twisted)
        // Mark abominations as undead.
        mon->flags |= MF_HONORARY_UNDEAD;
    else if (corps != -1)
    {
        // Turn the monster into a spectral thing, minus the usual
        // adjustments for zombified monsters.
        mon->type = MONS_SPECTRAL_THING;
        mon->base_monster = soul_type;

        // Re-equip the spectral thing.
        equip_undead(mon->pos(), corps, mon->mindex(),
                     mon->base_monster);

        // Destroy the monster's corpse, as it's no longer needed.
        destroy_item(corps);
    }

    name_zombie(mon, &orig);

    mons_make_god_gift(mon, GOD_YREDELEMNUL);

    mon->attitude = !force_hostile ? ATT_FRIENDLY : ATT_HOSTILE;
    behaviour_event(mon, ME_ALERT, !force_hostile ? MHITNOT : MHITYOU);

    if (!quiet)
    {
        mprf("%s soul %s, and %s.", whose.c_str(),
             twisted        ? "becomes twisted" : "remains intact",
             !force_hostile ? "is now yours"    : "fights you");
    }
}

bool fedhas_passthrough_class(const monster_type mc)
{
    return (you.religion == GOD_FEDHAS
            && mons_class_is_plant(mc)
            && mons_class_is_stationary(mc));
}

// Fedhas allows worshipers to walk on top of stationary plants and
// fungi.
bool fedhas_passthrough(const monsters * target)
{
    return (target
            && fedhas_passthrough_class(target->type)
            && (target->type != MONS_OKLOB_PLANT
                || target->attitude != ATT_HOSTILE));
}

// Fedhas worshipers can shoot through non-hostile plants, can a
// particular beam go through a particular monster?
bool fedhas_shoot_through(const bolt & beam, const monsters * victim)
{
    actor * originator = beam.agent();
    if (!victim || !originator)
        return (false);

    bool origin_worships_fedhas;
    mon_attitude_type origin_attitude;
    if (originator->atype() == ACT_PLAYER)
    {
        origin_worships_fedhas = you.religion == GOD_FEDHAS;
        origin_attitude = ATT_FRIENDLY;
    }
    else
    {
        monsters * temp = dynamic_cast<monsters *> (originator);
        if (!temp)
            return (false);
        origin_worships_fedhas = temp->god == GOD_FEDHAS;
        origin_attitude = temp->attitude;
    }

    return (origin_worships_fedhas
            && fedhas_protects(victim)
            && !beam.is_enchantment()
            && !(beam.is_explosion && beam.in_explosion_phase)
            && (mons_atts_aligned(victim->attitude, origin_attitude)
                || victim->neutral() ));
}

// Turns corpses in LOS into skeletons and grows toadstools on them.
// Can also turn zombies into skeletons and destroy ghoul-type monsters.
// Returns the number of corpses consumed.
int fungal_bloom()
{
    int seen_mushrooms  = 0;
    int seen_corpses    = 0;

    int processed_count = 0;
    bool kills = false;

    for (radius_iterator i(you.pos(), LOS_RADIUS); i; ++i)
    {
        monsters * target = monster_at(*i);
        if (target && target->is_summoned())
            continue;

        if (target && target->mons_species() != MONS_TOADSTOOL)
        {
            switch (mons_genus(target->mons_species()))
            {
            case MONS_ZOMBIE_SMALL:
                // Maybe turn a zombie into a skeleton.
                if (mons_skeleton(mons_zombie_base(target)))
                {
                    processed_count++;

                    monster_type skele_type = MONS_SKELETON_LARGE;
                    if (mons_zombie_size(mons_zombie_base(target)) == Z_SMALL)
                        skele_type = MONS_SKELETON_SMALL;

                    // Killing and replacing the zombie since upgrade_type
                    // doesn't get skeleton speed right (and doesn't
                    // reduce the victim's HP). This is awkward. -cao
                    mgen_data mg(skele_type, target->behaviour, NULL, 0, 0,
                                 target->pos(),
                                 target->foe,
                                 MG_FORCE_BEH | MG_FORCE_PLACE,
                                 target->god,
                                 mons_zombie_base(target),
                                 target->number);

                    unsigned monster_flags = target->flags;
                    int current_hp = target->hit_points;
                    mon_enchant_list ench = target->enchantments;

                    simple_monster_message(target, "'s flesh rots away.");

                    monster_die(target, KILL_MISC, NON_MONSTER, true);
                    int monster = create_monster(mg);
                    env.mons[monster].flags = monster_flags;
                    env.mons[monster].enchantments = ench;

                    if (env.mons[monster].hit_points > current_hp)
                        env.mons[monster].hit_points = current_hp;

                    behaviour_event(&env.mons[monster], ME_ALERT, MHITYOU);

                    continue;
                }
                // Else fall through and destroy the zombie.
                // Ghoul-type monsters are always destroyed.
            case MONS_GHOUL:
            {
                simple_monster_message(target, " rots away and dies.");

                coord_def pos = target->pos();
                int colour    = target->colour;
                int corpse    = monster_die(target, KILL_MISC, NON_MONSTER, true);
                kills = true;

                // If a corpse didn't drop, create a toadstool.
                // If one did drop, we will create toadstools from it as usual
                // later on.
                if (corpse < 0)
                {
                    const int mushroom = create_monster(
                                mgen_data(MONS_TOADSTOOL,
                                          BEH_FRIENDLY,
                                          &you,
                                          0,
                                          0,
                                          pos,
                                          MHITNOT,
                                          MG_FORCE_PLACE,
                                          GOD_NO_GOD,
                                          MONS_NO_MONSTER,
                                          0,
                                          colour),
                                          false);

                    if (mushroom != -1)
                        seen_mushrooms++;

                    processed_count++;

                    continue;
                }
                break;
            }

            default:
                continue;
            }
        }

        for (stack_iterator j(*i); j; ++j)
        {
            bool corpse_on_pos = false;
            if (j->base_type == OBJ_CORPSES && j->sub_type == CORPSE_BODY)
            {
                corpse_on_pos  = true;
                int trial_prob = mushroom_prob(*j);

                processed_count++;
                int target_count = 1 + binomial_generator(20, trial_prob);

                int seen_per;
                spawn_corpse_mushrooms(*j, target_count, seen_per,
                                       BEH_FRIENDLY, true);

                seen_mushrooms += seen_per;

                // Either turn this corpse into a skeleton or destroy it.
                if (mons_skeleton(j->plus))
                    turn_corpse_into_skeleton(*j);
                else
                    destroy_item(j->index());
            }

            if (corpse_on_pos && you.see_cell(*i))
                seen_corpses++;
        }
    }

    if (seen_mushrooms > 0)
        mushroom_spawn_message(seen_mushrooms, seen_corpses);

    if (kills)
        mprf("That felt like a moral victory.");

    return (processed_count);
}

static int _create_plant(coord_def & target, int hp_adjust = 0)
{
    if (actor_at(target) || !mons_class_can_pass(MONS_PLANT, grd(target)))
        return (0);

    const int plant = create_monster(mgen_data
                                     (MONS_PLANT,
                                      BEH_FRIENDLY,
                                      &you,
                                      0,
                                      0,
                                      target,
                                      MHITNOT,
                                      MG_FORCE_PLACE, GOD_FEDHAS));


    if (plant != -1)
    {
        env.mons[plant].flags |= MF_ATT_CHANGE_ATTEMPT;
        env.mons[plant].max_hit_points += hp_adjust;
        env.mons[plant].hit_points += hp_adjust;

        if (you.see_cell(target))
        {
            if (hp_adjust)
                mpr("A plant grows up from the ground, it is strengthened by Fedhas.");
            else
                mpr("A plant grows up from the ground.");
        }
    }


    return (plant != -1);
}

bool sunlight()
{
    int c_size = 5;
    int x_offset[] = {-1, 0, 0, 0, 1};
    int y_offset[] = { 0,-1, 0, 1, 0};

    dist spelld;

    bolt temp_bolt;
    temp_bolt.colour = YELLOW;
    direction(spelld, DIR_TARGET, TARG_HOSTILE, LOS_RADIUS, false, false,
              false, false, "Select sunlight destination");

    if (!spelld.isValid)
        return (false);

    coord_def base = spelld.target;

    int evap_count  = 0;
    int plant_count = 0;
    int processed_count = 0;

    // This is dealt with outside of the main loop.
    int cloud_count = 0;

    // FIXME: Uncomfortable level of code duplication here but the explosion
    // code in bolt subjects the input radius to r*(r+1) for the threshold and
    // since r is an integer we can never get just the 4-connected neighbours.
    // Anyway the bolt code doesn't seem to be well set up to handle the
    // 'occasional plant' gimmick.
    for (int i = 0; i < c_size; ++i)
    {
        coord_def target = base;
        target.x += x_offset[i];
        target.y += y_offset[i];

        if (!in_bounds(target) || feat_is_solid(grd(target)))
            continue;

        temp_bolt.explosion_draw_cell(target);

        actor *victim = actor_at(target);

        // If this is a water square we will evaporate it.
        dungeon_feature_type ftype = grd(target);
        dungeon_feature_type orig_type = ftype;

        switch (ftype)
        {
        case DNGN_SHALLOW_WATER:
            ftype = DNGN_FLOOR;
            break;

        case DNGN_DEEP_WATER:
            ftype = DNGN_SHALLOW_WATER;
            break;

        default:
            break;
        }

        if (orig_type != ftype)
        {
            dungeon_terrain_changed(target, ftype);

            if (you.see_cell(target))
                evap_count++;

            // This is a little awkward but if we evaporated all the way to
            // the dungeon floor that may have given a monster
            // ENCH_AQUATIC_LAND, and if that happened the player should get
            // credit if the monster dies. The enchantment is inflicted via
            // the dungeon_terrain_changed call chain and that doesn't keep
            // track of what caused the terrain change. -cao
            monsters * monster = monster_at(target);
            if (monster && ftype == DNGN_FLOOR
                && monster->has_ench(ENCH_AQUATIC_LAND))
            {
                mon_enchant temp = monster->get_ench(ENCH_AQUATIC_LAND);
                temp.who = KC_YOU;
                monster->add_ench(temp);
            }

            processed_count++;
        }

        monsters *mons = monster_at(target);

        if (victim)
        {
            if (!mons)
                you.backlight();
            else
            {
                backlight_monsters(target, 1, 0);
                behaviour_event(mons, ME_ALERT, MHITYOU);
            }

            processed_count++;
        }
        else if (one_chance_in(100)
                 && ftype >= DNGN_FLOOR_MIN
                 && ftype <= DNGN_FLOOR_MAX
                 && orig_type == DNGN_SHALLOW_WATER)
        {
            // Create a plant.
            const int plant = create_monster(mgen_data(MONS_PLANT,
                                                       BEH_HOSTILE,
                                                       &you,
                                                       0,
                                                       0,
                                                       target,
                                                       MHITNOT,
                                                       MG_FORCE_PLACE,
                                                       GOD_FEDHAS));

            if (plant != -1 && you.see_cell(target))
                plant_count++;

            processed_count++;
        }
    }

    // We damage clousd for a large radius, though.
    for (radius_iterator ai(base, 7); ai; ++ai)
    {
        if (env.cgrid(*ai) != EMPTY_CLOUD)
        {
            const int cloudidx = env.cgrid(*ai);
            if (env.cloud[cloudidx].type == CLOUD_GLOOM)
            {
                cloud_count++;
                delete_cloud(cloudidx);
            }
        }
    }

#ifndef USE_TILE
    // Move the cursor out of the way (it looks weird).
    cgotoxy(base.x, base.y, GOTO_DNGN);
#endif
    delay(200);

    if (plant_count)
    {
        mprf("%s grow%s in the sunlight.",
             (plant_count > 1 ? "Some plants": "A plant"),
             (plant_count > 1 ? "": "s"));
    }

    if (evap_count)
        mprf("Some water evaporates in the bright sunlight.");

    if (cloud_count)
        mprf("Sunlight penetrates the thick gloom.");

    return (processed_count);
}

template<typename T>
bool less_second(const T & left, const T & right)
{
    return (left.second < right.second);
}

typedef std::pair<coord_def, int> point_distance;

// dfs starting at origin, find the distance from the origin to the targets
// (not leaving LOS, not crossing monsters or solid walls) and store that in
// distances
static void _path_distance(coord_def & origin,
                           std::vector<coord_def> & targets,
                           std::vector<int> & distances)
{
    std::set<unsigned> exclusion;
    std::queue<point_distance> fringe;
    fringe.push(point_distance(origin,0));

    int idx = origin.x + origin.y * X_WIDTH;
    exclusion.insert(idx);

    while (!fringe.empty())
    {
        point_distance current = fringe.front();
        fringe.pop();

        // did we hit a target?
        for (unsigned i = 0; i < targets.size(); ++i)
        {
            if (current.first == targets[i])
            {
                distances[i] = current.second;
                break;
            }
        }

        for (adjacent_iterator adj_it(current.first); adj_it; ++adj_it)
        {
            idx = adj_it->x + adj_it->y * X_WIDTH;
            if (you.see_cell(*adj_it)
                && !feat_is_solid(env.grid(*adj_it))
                && exclusion.insert(idx).second)
            {
                monsters * temp = monster_at(*adj_it);
                if (!temp || (temp->attitude == ATT_HOSTILE
                              && temp->mons_species() != MONS_PLANT
                              && temp->mons_species() != MONS_TOADSTOOL
                              && temp->mons_species() != MONS_FUNGUS
                              && temp->mons_species() != MONS_BALLISTOMYCETE))
                {
                    fringe.push(point_distance(*adj_it, current.second+1));
                }
            }
        }
    }
}

// so we are basically going to compute point to point distance between
// the points of origin and the end points (origins and targets respecitvely)
// We will return a vector consisting of the minimum distances along one
// dimension of the distance matrix.
static void _point_point_distance(std::vector<coord_def> & origins,
                                  std::vector<coord_def> & targets,
                                  bool origin_to_target,
                                  std::vector<int> & distances)
{

    distances.clear();
    // Consider a matrix where the points of origin form the rows and
    // the target points form the column, we want to take the minimum along
    // one of those dimensions.
    if (origin_to_target)
        distances.resize(origins.size(), INT_MAX);
    else
        distances.resize(targets.size(), INT_MAX);

    std::vector<int> current_distances(targets.size(), 0);
    for (unsigned i = 0; i < origins.size(); ++i)
    {
        for (unsigned j = 0; j < current_distances.size(); ++j)
            current_distances[j] = INT_MAX;

        _path_distance(origins[i], targets, current_distances);

        // So we got the distance from a point of origin to one of the
        // targets. What should we do with it?
        if (origin_to_target)
        {
            // The minimum of current_distances is points(i)
            int min_dist = current_distances[0];
            for (unsigned j = 1; j < current_distances.size(); ++j)
                if (current_distances[j] < min_dist)
                    min_dist = current_distances[j];

            distances[i] = min_dist;
        }
        else
        {
            for (unsigned j = 0; j < targets.size(); ++j)
            {
                if (i == 0)
                    distances[j] = current_distances[j];
                else if (current_distances[j] < distances[j])
                    distances[j] = current_distances[j];
            }
        }
    }
}

// So the idea is we want to decide which adjacent tiles are in the most 'danger'
// We claim danger is proportional to the minimum distances from the point to a
// (hostile) monster. This function carries out at most 8 depth-first searches
// to calculate the distances in question. In practice it should be called for
// at most 7 searches since 8 (all adjacent free, > 8 monsters in view) can be
// special cased easily.
bool prioritise_adjacent(const coord_def &target, std::vector<coord_def> & candidates)
{
    radius_iterator los_it(target, LOS_RADIUS, true, true, true);

    std::vector<coord_def> mons_positions;
    // collect hostile monster positions in LOS
    for ( ; los_it; ++los_it)
    {
        monsters *hostile = monster_at(*los_it);

        if (hostile && hostile->attitude == ATT_HOSTILE)
            mons_positions.push_back(hostile->pos());
    }

    if (mons_positions.empty())
    {
        std::random_shuffle(candidates.begin(), candidates.end());
        return (true);
    }

    bool squares_to_monsters = mons_positions.size() > candidates.size();

    std::vector<int> distances;

    // So the idea is we will search from either possible plant locations to
    // monsters or from monsters to possible plant locations, but honestly the
    // implementation is unnecessarily tense and doing plants to monsters all
    // the time would be fine. Yet I'm reluctant to change it because it does
    // work.
    if (squares_to_monsters)
    {
        _point_point_distance(candidates, mons_positions,
                              squares_to_monsters, distances);
    }
    else
    {
        _point_point_distance(mons_positions, candidates,
                              squares_to_monsters, distances);
    }

    std::vector<point_distance> possible_moves(candidates.size());

    for (unsigned i = 0; i < possible_moves.size(); ++i)
    {
        possible_moves[i].first  = candidates[i];
        possible_moves[i].second = distances[i];
    }

    std::sort(possible_moves.begin(), possible_moves.end(),
              less_second<point_distance>);

    for (unsigned i = 0; i < candidates.size(); ++i)
        candidates[i] = possible_moves[i].first;

    return (true);
}

// Prompt the user to select a stack of fruit from their inventory.  The
// user can optionally select only a partial stack of fruit (the count
// variable will store the number of fruit the user wants).  Return the
// index of the item selected in the user's inventory, or a negative
// number if the prompt failed (user cancelled or had no fruit).
int _prompt_for_fruit(int & count, const char * prompt_string)
{
    int rc = prompt_invent_item(prompt_string,
                                MT_INVLIST,
                                OSEL_FRUIT,
                                true,
                                true,
                                true,
                                '\0',
                                -1,
                                &count);

    if (prompt_failed(rc))
        return (rc);

    // Return PROMPT_INAPPROPRIATE if the 'object selected isn't
    // actually fruit.
    if (!is_fruit(you.inv[rc]))
        return (PROMPT_INAPPROPRIATE);

    // Handle it if the user lies about the amount of fruit available.
    if (count > you.inv[rc].quantity)
        count = you.inv[rc].quantity;

    return (rc);
}

bool _prompt_amount(int max, int & selected, const std::string & prompt)
{
    selected = max;
    while (true)
    {
        msg::streams(MSGCH_PROMPT) << prompt <<" (" << max << " max)"<< std::endl;

        unsigned char keyin = get_ch();

        // Cancel
        if (keyin == ESCAPE || keyin == ' ')
        {
            canned_msg( MSG_OK );
            return (false);
        }

        // Default is max
        if (keyin == '\n'  || keyin == '\r')
            return (true);

        // Otherwise they should enter a digit
        if (isdigit(keyin))
        {
            selected = keyin - '0';
            if (selected > 0 && selected <= max)
                return (true);
        }
        // else they entered some garbage?
    }

    return (max);
}


int _collect_fruit(std::vector<std::pair<int,int>  > & available_fruit)
{
    int total=0;

    for (int i = 0; i < ENDOFPACK; i++)
    {
        if (you.inv[i].is_valid()
            && is_fruit(you.inv[i]) )
        {
            total += you.inv[i].quantity;
            available_fruit.push_back(std::pair<int,int> (you.inv[i].quantity, i));
        }
    }

    return (total);
}

bool _less_first(const std::pair<int, int> & left, const std::pair<int, int> & right)
{
    return (left.first < right.first);
}
void _decrease_amount(std::vector<std::pair<int, int> > & available, int amount)
{
    int total_decrease = amount;
    for (unsigned i=0; i < available.size() && amount > 0; i++)
    {

        int decrease_amount = available[i].first;
        if (decrease_amount > amount)
        {
            decrease_amount = amount;
        }
        amount -= decrease_amount;
        dec_inv_item_quantity(available[i].second, decrease_amount);
    }
    if (total_decrease > 1)
        mprf("%d pieces of fruit are consumed!", total_decrease);
    else
        mpr("A piece of fruit is consumed!");

}

// Create a ring or partial ring around the caster.  The user is
// prompted to select a stack of fruit, and then plants are placed on open
// squares adjacent to the user.  Of course, one piece of fruit is
// consumed per plant, so a complete ring may not be formed.
bool plant_ring_from_fruit()
{
    // How much fruit is available?
    std::vector<std::pair<int, int> > collected_fruit;
    int total_fruit = _collect_fruit(collected_fruit);

    // How many adjacent open spaces are there?
    std::vector<coord_def> adjacent;
    for (adjacent_iterator adj_it(you.pos()); adj_it; ++adj_it)
    {
        if (mons_class_can_pass(MONS_PLANT, env.grid(*adj_it))
            && !actor_at(*adj_it))
        {
            adjacent.push_back(*adj_it);
        }
    }

    int max_use = std::min(total_fruit, int(adjacent.size()) );

    // Don't prompt if we can't do anything (due to having no fruit or
    // no squares to place plants on).
    if (max_use == 0)
        return (false);

    // And how many plants does the user want to create?
    int target_count;
    if (!_prompt_amount(max_use, target_count, "How many plants will you create?"))
    {
        return (false);
    }

    if ((int)adjacent.size() > target_count)
        prioritise_adjacent(you.pos(), adjacent);

    int hp_adjust = you.skills[SK_INVOCATIONS] * 10;

    int created_count = 0;
    for (int i = 0; i < target_count; ++i)
    {
        if (_create_plant(adjacent[i], hp_adjust))
            created_count++;
    }

    std::sort(collected_fruit.begin(), collected_fruit.end(), _less_first);
    _decrease_amount(collected_fruit, created_count);

    return (created_count);
}

// Create a circle of water around the target, with a radius of
// approximately 2.  This turns normal floor tiles into shallow water
// and turns (unoccupied) shallow water into deep water.  There is a
// chance of spawning plants or fungus on unoccupied dry floor tiles
// outside of the rainfall area.  Return the number of plants/fungi
// created.
int rain(const coord_def &target)
{
    int spawned_count = 0;
    int processed_count = 0;

    for (radius_iterator rad(target, LOS_RADIUS, true, true, true); rad; ++rad)
    {
        // Adjust the shape of the rainfall slightly to make it look
        // nicer.  I want a threshold of 2.5 on the euclidean distance,
        // so a threshold of 6 prior to the sqrt is close enough.
        int rain_thresh = 6;
        coord_def local = *rad - target;

        dungeon_feature_type ftype = grd(*rad);

        if (local.abs() > rain_thresh)
        {
            // Maybe spawn a plant on (dry, open) squares that are in
            // LOS but outside the rainfall area.  In open space, there
            // are 213 squares in LOS, and we are going to drop water on
            // (25-4) of those, so if we want x plants to spawn on
            // average in open space, the trial probability should be
            // x/192.
            if (x_chance_in_y(5, 192)
                && !actor_at(*rad)
                && ftype >= DNGN_FLOOR_MIN
                && ftype <= DNGN_FLOOR_MAX)
            {
                const int plant = create_monster(mgen_data
                                     (coinflip() ? MONS_PLANT : MONS_FUNGUS,
                                      BEH_GOOD_NEUTRAL,
                                      &you,
                                      0,
                                      0,
                                      *rad,
                                      MHITNOT,
                                      MG_FORCE_PLACE, GOD_FEDHAS));

                if (plant != -1)
                    spawned_count++;

                processed_count++;
            }

            continue;
        }

        // Turn regular floor squares only into shallow water.
        if (ftype >= DNGN_FLOOR_MIN && ftype <= DNGN_FLOOR_MAX)
        {
            dungeon_terrain_changed(*rad, DNGN_SHALLOW_WATER);

            processed_count++;
        }
        // We can also turn shallow water into deep water, but we're
        // just going to skip cases where there is something on the
        // shallow water.  Destroying items will probably be annoying,
        // and insta-killing monsters is clearly out of the question.
        else if (!actor_at(*rad)
                 && igrd(*rad) == NON_ITEM
                 && ftype == DNGN_SHALLOW_WATER)
        {
            dungeon_terrain_changed(*rad, DNGN_DEEP_WATER);

            processed_count++;
        }

        if (ftype >= DNGN_MINMOVE)
        {
            // Maybe place a raincloud.
            //
            // The rainfall area is 20 (5*5 - 4 (corners) - 1 (center));
            // the expected number of clouds generated by a fixed chance
            // per tile is 20 * p = expected.  Say an Invocations skill
            // of 27 gives expected 5 clouds.
            int max_expected = 5;
            int expected = div_rand_round(max_expected
                                          * you.skills[SK_INVOCATIONS], 27);

            if (x_chance_in_y(expected, 20))
            {
                place_cloud(CLOUD_RAIN, *rad, 10, KC_YOU);

                processed_count++;
            }
        }
    }

    if (spawned_count > 0)
    {
        mprf("%s grow%s in the rain.",
             (spawned_count > 1 ? "Some plants" : "A plant"),
             (spawned_count > 1 ? "" : "s"));
    }

    return (processed_count);
}

// Destroy corpses in the player's LOS (first corpse on a stack only)
// and make 1 giant spore per corpse.  Spores are given the input as
// their starting behavior; the function returns the number of corpses
// processed.
int corpse_spores(beh_type behavior)
{
    int count = 0;
    for (radius_iterator rad(you.pos(), LOS_RADIUS, true, true, true); rad;
         ++rad)
    {
        for (stack_iterator stack_it(*rad); stack_it; ++stack_it)
        {
            if (stack_it->base_type == OBJ_CORPSES
                && stack_it->sub_type == CORPSE_BODY)
            {
                count++;

                int rc = create_monster(mgen_data(MONS_GIANT_SPORE,
                                                  behavior,
                                                  &you,
                                                  0,
                                                  0,
                                                  *rad,
                                                  MHITNOT,
                                                  MG_FORCE_PLACE));

                if (rc != -1)
                {
                    env.mons[rc].flags |= MF_ATT_CHANGE_ATTEMPT;
                    if (behavior == BEH_FRIENDLY)
                    {
                        env.mons[rc].behaviour = BEH_WANDER;
                        env.mons[rc].foe = MHITNOT;
                    }
                }

                if (mons_skeleton(stack_it->plus))
                    turn_corpse_into_skeleton(*stack_it);
                else
                    destroy_item(stack_it->index());

                break;
            }
        }
    }

    return (count);
}

struct monster_conversion
{
    monster_conversion()
    {
        base_monster = NULL;
        piety_cost = 0;
        fruit_cost = 0;

    }
    monsters * base_monster;
    int piety_cost;
    int fruit_cost;
    monster_type new_type;
};


// Given a monster (which should be a plant/fungus), see if
// evolve_flora() can upgrade it, and set up a monster_conversion
// structure for it.  Return true (and fill in possible_monster) if the
// monster can be upgraded, and return false otherwise.
bool _possible_evolution(monsters * input,
                         monster_conversion & possible_monster)
{
    possible_monster.base_monster = input;
    switch (input->mons_species())
    {
    case MONS_PLANT:
    case MONS_BUSH:
        possible_monster.new_type = MONS_OKLOB_PLANT;
        possible_monster.fruit_cost = 1;
        break;

    case MONS_FUNGUS:
    case MONS_BALLISTOMYCETE:
        possible_monster.new_type = MONS_WANDERING_MUSHROOM;
        possible_monster.piety_cost = 1;
        break;

    case MONS_TOADSTOOL:
        possible_monster.new_type = MONS_WANDERING_MUSHROOM;
        possible_monster.piety_cost = 2;
        break;

    default:
        return (false);
    }

    return (true);
}

void _collect_adjacent_monsters(std::vector<monster_conversion> & available,
                                const coord_def &  center)
{
    for (adjacent_iterator adjacent(center, false); adjacent; ++adjacent)
    {
        monsters * candidate = monster_at(*adjacent);
        monster_conversion monster_upgrade;
        if (candidate && _possible_evolution(candidate, monster_upgrade))
            available.push_back(monster_upgrade);
    }
}

void _cost_summary(int oklob_count, int wandering_count, int total_in_range)
{
    mesclr(true);
    if (oklob_count)
    {
        std::string str = (oklob_count > 1 ? "ts" : "t");
        mprf("Upgrading %d plan%s to oklob plan%s (%d fruit)",
              oklob_count, str.c_str(), str.c_str(), oklob_count);
    }

    if (wandering_count)
        mprf("Upgrading %d fungi to wandering mushroo%s (piety cost)",
             wandering_count, (wandering_count > 1 ? "ms" : "m "));
}

bool evolve_flora()
{
    // Collect adjacent monsters
    std::vector<monster_conversion> available_monsters;
    _collect_adjacent_monsters(available_monsters, you.pos());

    // No monsters in range can be upgraded.
    if (available_monsters.empty() )
    {
        mpr("No flora in range can be evolved.");
        return (false);
    }

    // What are the total costs of all adjacent upgrades?
    int piety_cost = 0;
    int fruit_cost = 0;

    int oklob_generation = 0;
    int wandering_generation = 0;

    for (unsigned i=0; i < available_monsters.size(); i++)
    {
        piety_cost += available_monsters[i].piety_cost;
        fruit_cost += available_monsters[i].fruit_cost;
        switch(available_monsters[i].new_type)
        {
        case MONS_OKLOB_PLANT:
            oklob_generation++;
            break;
        case MONS_WANDERING_MUSHROOM:
            wandering_generation++;
            break;

        default:
            break;
        }
    }


    std::vector<std::pair<int, int> > collected_fruit;
    int total_fruit = _collect_fruit(collected_fruit);
    int useable_fruit = std::min(total_fruit, fruit_cost);

    _cost_summary(useable_fruit, wandering_generation, available_monsters.size());

    crawl_state.darken_range = 1;
    viewwindow(false, false);

    int target_fruit = useable_fruit;
    // Ask the user how many fruit to use
    if (useable_fruit > 1)
    {
        if(!_prompt_amount(useable_fruit, target_fruit,
                           "How many oklobs will you create?"))
        {
            crawl_state.darken_range = -1;
            viewwindow(false,false);
            return (false);
        }
    }
    else
    {
        delay(500);
    }

    crawl_state.darken_range = -1;
    viewwindow(false, false);

    int fruit_used = target_fruit;
    int reduction = fruit_cost - target_fruit;

    if (int(available_monsters.size()) <= reduction)
    {
        mprf("Not enough fruit available.");
        return false;
    }

    int plants_evolved = 0;
    int toadstools_evolved = 0;
    int fungi_evolved = 0;


    std::random_shuffle(available_monsters.begin(), available_monsters.end() );

    for (unsigned i=0; i < available_monsters.size(); i++)
    {
        monsters * current_plant = available_monsters[i].base_monster;
        monster_conversion current_target = available_monsters[i];

        if (current_target.new_type == MONS_OKLOB_PLANT)
        {
            if (target_fruit)
                target_fruit--;
            else
                continue;
        }
        else if (you.piety > current_target.piety_cost)
        {
            lose_piety(current_target.piety_cost);
        }
        // This would wipe out our remaining piety, so don't do it.
        else
        {
            continue;
        }

        switch (current_plant->mons_species())
        {
        case MONS_PLANT:
            plants_evolved++;
            break;

        case MONS_FUNGUS:
        case MONS_BALLISTOMYCETE:
            fungi_evolved++;
            break;

        case MONS_TOADSTOOL:
            toadstools_evolved++;
            break;
        };

        current_plant->upgrade_type(current_target.new_type, true, true);
        current_plant->god = GOD_FEDHAS;
        current_plant->attitude = ATT_FRIENDLY;
        current_plant->flags |= MF_NO_REWARD;
        current_plant->flags |= MF_ATT_CHANGE_ATTEMPT;

        // Try to remove slowly dying in case we are upgrading a
        // toadstool, and spore production in case we are upgrading a
        // ballistomycete.
        current_plant->del_ench(ENCH_SLOWLY_DYING);
        current_plant->del_ench(ENCH_SPORE_PRODUCTION);
    }

    if (fruit_used)
    {
        _decrease_amount(collected_fruit, fruit_used);
    }

    return (true);
}

bool ponderousify_armour()
{
    int item_slot = -1;
    do
    {
        if (item_slot == -1)
        {
            item_slot = prompt_invent_item("Make which item ponderous?",
                            MT_INVLIST, OSEL_PONDER_ARM, true, true, false);
        }

        if (prompt_failed(item_slot))
            return (false);

        item_def& arm(you.inv[item_slot]);

        if (!is_enchantable_armour(arm, true, true)
            || get_armour_ego_type(arm) != SPARM_NORMAL
            || get_armour_slot(arm) != EQ_BODY_ARMOUR)
        {
            mpr("This armour can't be made ponderous. "
                "Choose a different one, or Esc to abort.");
            if (Options.auto_list)
                more();

            item_slot = -1;
            mpr("You can't enchant that."); //does not appear
            continue;
        }

        //make item desc runed if desc was vanilla?

        set_item_ego_type(arm, OBJ_ARMOUR, SPARM_PONDEROUSNESS);

        you.redraw_armour_class = true;
        you.redraw_evasion = true;

        simple_god_message(" says: Use this wisely!");

        return (true);
    }
    while (true);

    return true;
}

static int _slouch_monsters(coord_def where, int pow, int, actor* agent)
{
    monsters* mon = monster_at(where);
    if (mon == NULL || mons_is_stationary(mon) || mon->cannot_move()
        || mons_is_projectile(mon->type))
        return (0);

    int dmg = (mon->speed - 1000/player_movement_speed()/player_speed());
    dmg = (dmg > 0 ? roll_dice(dmg*4, 3)/2 : 0);

    mon->hurt(agent, dmg, BEAM_MMISSILE, true);
    return (1);
}

int cheibriados_slouch(int pow)
{
    return (apply_area_visible(_slouch_monsters, pow));
}

////////////////////////////////////////////////////////////////////////////

static int _lugonu_warp_monster(coord_def where, int pow, int, actor *)
{
    if (!in_bounds(where))
        return (0);

    monsters* mon = monster_at(where);
    if (mon == NULL)
        return (0);

    if (!mon->friendly())
        behaviour_event(mon, ME_ANNOY, MHITYOU);

    if (mon->check_res_magic(pow * 2))
    {
        mprf("%s %s.",
             mon->name(DESC_CAP_THE).c_str(), mons_resist_string(mon));
        return (1);
    }

    const int damage = 1 + random2(pow / 6);
    if (mons_genus(mon->type) == MONS_BLINK_FROG)
        mon->heal(damage, false);
    else if (!mon->check_res_magic(pow))
    {
        mon->hurt(&you, damage);
        if (!mon->alive())
            return (1);
    }

    mon->blink();

    return (1);
}

static void _lugonu_warp_area(int pow)
{
    apply_area_around_square(_lugonu_warp_monster, you.pos(), pow);
}

void lugonu_bends_space()
{
    const int pow = 4 + skill_bump(SK_INVOCATIONS);
    const bool area_warp = random2(pow) > 9;

    mprf("Space bends %saround you!", area_warp ? "sharply " : "");

    if (area_warp)
        _lugonu_warp_area(pow);

    random_blink(false, true);

    const int damage = roll_dice(1, 4);
    ouch(damage, NON_MONSTER, KILLED_BY_WILD_MAGIC, "a spatial distortion");
}

////////////////////////////////////////////////////////////////////////

void cheibriados_time_bend(int pow)
{
    mpr("The flow of time bends around you.");

    for (adjacent_iterator ai(you.pos()); ai; ++ai)
    {
        monsters* mon = monster_at(*ai);
        if (mon && !mons_is_stationary(mon))
        {
            if (roll_dice(mon->hit_dice, 3) > random2avg(pow, 2))
            {
                mprf("%s %s.",
                     mon->name(DESC_CAP_THE).c_str(), mons_resist_string(mon));
                continue;
            }

            simple_god_message(
                make_stringf(" rebukes %s.",
                             mon->name(DESC_NOCAP_THE).c_str()).c_str(),
                             GOD_CHEIBRIADOS);
            do_slow_monster(mon, KC_YOU);
        }
    }
}

void cheibriados_time_step(int pow) // pow is the number of turns to skip
{
    const coord_def old_pos = you.pos();

    mpr("You step out of the flow of time.");
    flash_view(LIGHTBLUE);
    you.moveto(coord_def(0, 0));
    you.duration[DUR_TIME_STEP] = pow;

    you.time_taken = 10;
    do
    {
        run_environment_effects();
        handle_monsters();
        manage_clouds();
    }
    while (--you.duration[DUR_TIME_STEP] > 0);
    // Update corpses, etc.  This does also shift monsters, but only by
    // a tiny bit.
    update_level(pow * 10);

#ifndef USE_TILE
    delay(1000);
#endif

    you.moveto(old_pos);
    you.duration[DUR_TIME_STEP] = 0;

    flash_view(0);
    mpr("You return to the normal time flow.");
}
