/*
 *  File:       main.cc
 *  Summary:    Main entry point, event loop, and some initialization functions
 *  Written by: Linley Henzell
 */

#include "AppHdr.h"

#include <string>
#include <algorithm>

// I don't seem to need values.h for VACPP..
#if !defined(__IBMCPP__)
#include <limits.h>
#endif

#ifdef DEBUG
  // this contains the DBL_MAX constant
  #include <float.h>
#endif

#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <sstream>
#include <iostream>

#ifdef USE_UNIX_SIGNALS
#include <signal.h>
#endif

#include "externs.h"

#include "abl-show.h"
#include "abyss.h"
#include "areas.h"
#include "artefact.h"
#include "arena.h"
#include "branch.h"
#include "chardump.h"
#include "cio.h"
#include "cloud.h"
#include "clua.h"
#include "command.h"
#include "coord.h"
#include "coordit.h"
#include "ctest.h"
#include "crash.h"
#include "database.h"
#include "dbg-maps.h"
#include "dbg-scan.h"
#include "debug.h"
#include "delay.h"
#include "describe.h"
#include "dlua.h"
#include "directn.h"
#include "dungeon.h"
#include "effects.h"
#include "env.h"
#include "map_knowledge.h"
#include "fprop.h"
#include "fight.h"
#include "files.h"
#include "food.h"
#include "godabil.h"
#include "hiscores.h"
#include "initfile.h"
#include "invent.h"
#include "item_use.h"
#include "it_use3.h"
#include "itemname.h"
#include "itemprop.h"
#include "items.h"
#include "lev-pand.h"
#include "los.h"
#include "luaterp.h"
#include "macro.h"
#include "makeitem.h"
#include "mapmark.h"
#include "maps.h"
#include "message.h"
#include "misc.h"
#include "mon-act.h"
#include "mon-cast.h"
#include "mon-iter.h"
#include "mon-stuff.h"
#include "mon-util.h"
#include "mutation.h"
#include "newgame.h"
#include "ng-init.h"
#include "notes.h"
#include "options.h"
#include "ouch.h"
#include "output.h"
#include "overmap.h"
#include "player.h"
#include "quiver.h"
#include "random.h"
#include "religion.h"
#include "shopping.h"
#include "skills.h"
#include "skills2.h"
#include "species.h"
#include "spells1.h"
#include "spells2.h"
#include "spells3.h"
#include "spells4.h"
#include "spl-book.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "stash.h"
#include "state.h"
#include "stuff.h"
#include "tags.h"
#include "terrain.h"
#include "transform.h"
#include "traps.h"
#include "travel.h"
#include "tutorial.h"
#include "view.h"
#include "shout.h"
#include "viewchar.h"
#include "viewgeom.h"
#include "stash.h"
#include "wiz-dgn.h"
#include "wiz-fsim.h"
#include "wiz-item.h"
#include "wiz-mon.h"
#include "wiz-you.h"
#include "xom.h"

#ifdef USE_TILE
#include "tiles.h"
#include "tiledef-dngn.h"
#endif

// ----------------------------------------------------------------------
// Globals whose construction/destruction order needs to be managed
// ----------------------------------------------------------------------

CLua clua(true);
CLua dlua(false);      // Lua interpreter for the dungeon builder.
crawl_environment env; // Requires dlua.
player you;
system_environment SysEnv;
game_state crawl_state;



std::string init_file_error;    // externed in newgame.cc

char info[ INFO_SIZE ];         // messaging queue extern'd everywhere {dlb}

int stealth;                    // externed in view.cc

void world_reacts();

static key_recorder repeat_again_rec;

// Clockwise, around the compass from north (same order as enum RUN_DIR)
const struct coord_def Compass[8] =
{
    coord_def(0, -1), coord_def(1, -1), coord_def(1, 0), coord_def(1, 1),
    coord_def(0, 1), coord_def(-1, 1), coord_def(-1, 0), coord_def(-1, -1),
};

// Functions in main module
static void _do_berserk_no_combat_penalty(void);
static bool _initialise(void);
static void _input(void);
static void _move_player(int move_x, int move_y);
static void _move_player(coord_def move);
static int  _check_adjacent(dungeon_feature_type feat, coord_def& delta);
static void _open_door(coord_def move, bool check_confused = true);
static void _open_door(int x, int y, bool check_confused = true)
{
    _open_door(coord_def(x, y), check_confused);
}
static void _close_door(coord_def move);
static void _start_running( int dir, int mode );

static void _prep_input();
static command_type _get_next_cmd();
static keycode_type _get_next_keycode();
static command_type _keycode_to_command( keycode_type key );
static void _setup_cmd_repeat();
static void _do_prev_cmd_again();
static void _update_replay_state();

static void _show_commandline_options_help();
static void _wanderer_startup_message();
static void _god_greeting_message( bool game_start );
static void _take_starting_note();
static void _startup_tutorial();

#ifdef DGL_SIMPLE_MESSAGING
static void _read_messages();
#endif

static void _compile_time_asserts();

//
//  It all starts here. Some initialisations are run first, then straight
//  to new_game and then input.
//

#ifdef USE_TILE
#include <SDL_main.h>
#endif

int main( int argc, char *argv[] )
{
    _compile_time_asserts();  // Just to quiet "unused static function" warning.

    init_crash_handler();

    // Hardcoded initial keybindings.
    init_keybindings();

    // Load in the system environment variables
    get_system_environment();

    // Parse command line args -- look only for initfile & crawl_dir entries.
    if (!parse_args(argc, argv, true))
    {
        _show_commandline_options_help();
        return 1;
    }

    // Init monsters up front - needed to handle the mon_glyph option right.
    init_monsters();

    // Init name cache so that we can parse stash_filter by item name.
    init_properties();
    init_item_name_cache();

    // Read the init file.
    init_file_error = read_init_file();

    // Now parse the args again, looking for everything else.
    parse_args( argc, argv, false );

    if (Options.sc_entries != 0 || !SysEnv.scorefile.empty())
    {
        hiscores_print_all( Options.sc_entries, Options.sc_format );
        return 0;
    }
    else
    {
        // Don't allow scorefile override for actual gameplay, only for
        // score listings.
        SysEnv.scorefile.clear();
    }

#ifdef USE_TILE
    if (!tiles.initialise())
        return -1;
#endif

    const bool game_start = _initialise();

    // Override some options for tutorial.
    init_tutorial_options();

    msg::stream << "Welcome" << (game_start? "" : " back") << ", "
                << you.your_name << " the "
                << species_name( you.species,you.experience_level )
                << " " << you.class_name << "."
                << std::endl;

    // Activate markers only after the welcome message, so the
    // player can see any resulting messages.
    env.markers.activate_all();
    // Call again to make Ctrl-X work correctly for items
    // in los on turn 0.
    maybe_update_stashes();
#ifdef USE_TILE
    viewwindow(false);
#endif

    if (game_start && you.char_class == JOB_WANDERER)
        _wanderer_startup_message();

    _god_greeting_message( game_start );

    // Warn player about their weapon, if unsuitable.
    wield_warning(false);

    _prep_input();

    if (game_start)
    {
        if (Tutorial.tutorial_left)
            _startup_tutorial();
        _take_starting_note();
    }
    else
        tutorial_load_game();

    // Catch up on any experience levels we did not assign last time. This
    // can happen if Crawl sees SIGHUP while it is waiting for the player
    // to dismiss a level-up prompt.
    level_change();

    cursor_control ccon(!Options.use_fake_player_cursor);
    while (true)
        _input();

    clear_globals_on_exit();

    return 0;
}

static void _show_commandline_options_help()
{
    puts("Command line options:");
    puts("  -help                 prints this list of options");
    puts("  -name <string>        character name");
    puts("  -species <arg>        preselect race (by letter, abbreviation, or name)");
    puts("  -job <arg>            preselect class (by letter, abbreviation, or name)");
    puts("  -plain                don't use IBM extended characters");
    puts("  -dir <path>           crawl directory");
    puts("  -rc <file>            init file name");
    puts("  -rcdir <dir>          directory that contains (included) rc files");
    puts("  -morgue <dir>         directory to save character dumps");
    puts("  -macro <dir>          directory to save/find macro.txt");
    puts("  -version              Crawl version (and compilation info)");
    puts("  -save-version <name>  Save file version for the given player");
    puts("");
    puts("Command line options override init file options, which override");
    puts("environment options (CRAWL_NAME, CRAWL_DIR, CRAWL_RC).");
    puts("");

    puts("  -extra-opt-first optname=optval");
    puts("  -extra-opt-last  optname=optval");
    puts("");
    puts("Acts as if 'optname=optval' was at the top or bottom of the init");
    puts("file.  Can be used multiple times.");
    puts("");

    puts("Highscore list options: (Can be redirected to more, etc.)");
    puts("  -scores [N]            highscore list");
    puts("  -tscores [N]           terse highscore list");
    puts("  -vscores [N]           verbose highscore list");
    puts("  -scorefile <filename>  scorefile to report on");
    puts("");
    puts("Arena options: (Stage a tournament between various monsters.)");
    puts("  -arena \"<monster list> v <monster list> arena:<arena map>\"");
#if DEBUG_DIAGNOSTICS
    puts("");
    puts("  -test            run test cases in ./test");
    puts("  -script <name>   run script matching <name> in ./scripts");
#endif
}

static void _wanderer_startup_message()
{
    int skill_levels = 0;
    for (int i = 0; i < NUM_SKILLS; ++i)
        skill_levels += you.skills[ i ];

    if (skill_levels <= 2)
    {
        // Some wanderers stand to not be able to see any of their
        // skills at the start of the game (one or two skills should be
        // easily guessed from starting equipment).  Anyway, we'll give
        // the player a message to warn them (and a reason why). - bwr
        mpr("You wake up in a daze, and can't recall much.");
    }
}

static void _god_greeting_message(bool game_start)
{
    switch (you.religion)
    {
    case GOD_ZIN:
        simple_god_message(" says: Spread the light, my child.");
        break;
    case GOD_SHINING_ONE:
        simple_god_message(" says: Lead the forces of light to victory!");
        break;
    case GOD_KIKUBAAQUDGHA:
        simple_god_message(" says: Welcome...");
        break;
    case GOD_YREDELEMNUL:
        simple_god_message(" says: Carry the black torch! Rouse the idle dead!");
        break;
    case GOD_NEMELEX_XOBEH:
        simple_god_message(" says: It's all in the cards!");
        break;
    case GOD_XOM:
        if (game_start)
            simple_god_message(" says: A new plaything!");
        break;
    case GOD_VEHUMET:
        simple_god_message(" says: Let it end in hellfire!");
        break;
    case GOD_OKAWARU:
        simple_god_message(" says: Welcome, disciple.");
        break;
    case GOD_MAKHLEB:
        god_speaks(you.religion, "Blood and souls for Makhleb!");
        break;
    case GOD_SIF_MUNA:
        simple_god_message(" whispers: I know many secrets...");
        break;
    case GOD_TROG:
        simple_god_message(" says: Kill them all!");
        break;
    case GOD_ELYVILON:
        simple_god_message(" says: Go forth and aid the weak!");
        break;
    case GOD_LUGONU:
        simple_god_message(" says: Spread carnage and corruption!");
        break;
    case GOD_BEOGH:
        simple_god_message(" says: Drown the unbelievers in a sea of blood!");
        break;
    case GOD_JIYVA:
        god_speaks(you.religion, "Slime for the Slime God!");
        break;
    case GOD_FEDHAS:
        simple_god_message(" says: Spread life and death.");
        break;
    case GOD_CHEIBRIADOS:
        simple_god_message(" says: Take it easy.");
        break;

    case GOD_NO_GOD:
    case NUM_GODS:
    case GOD_RANDOM:
    case GOD_NAMELESS:
        break;
    }
}

static void _take_starting_note()
{
    std::ostringstream notestr;
    notestr << you.your_name << ", the "
            << species_name(you.species,you.experience_level) << " "
            << you.class_name
            << ", began the quest for the Orb.";
    take_note(Note(NOTE_MESSAGE, 0, 0, notestr.str().c_str()));

    notestr.str("");
    notestr.clear();

#ifdef WIZARD
    if (you.wizard)
    {
        notestr << "You started the game in wizard mode.";
        take_note(Note(NOTE_MESSAGE, 0, 0, notestr.str().c_str()));

        notestr.str("");
        notestr.clear();
    }
#endif

    notestr << "HP: " << you.hp << "/" << you.hp_max
            << " MP: " << you.magic_points << "/" << you.max_magic_points;

    take_note(Note(NOTE_XP_LEVEL_CHANGE, you.experience_level, 0,
                   notestr.str().c_str()));
}

static void _startup_tutorial()
{
    // Don't allow triggering at game start.
    Tutorial.tut_just_triggered = true;

    msg::streams(MSGCH_TUTORIAL)
        << "Press any key to start the tutorial intro, or Escape to skip it."
        << std::endl;

    flush_prev_message();
    const int ch = c_getch();
    if (ch != ESCAPE)
        tut_starting_screen();
}

#ifdef WIZARD
static void _do_wizard_command(int wiz_command, bool silent_fail)
{
    ASSERT(you.wizard);

    switch (wiz_command)
    {
    case '?':
    {
        const int key = list_wizard_commands(true);
        _do_wizard_command(key, true);
        return;
    }

    case CONTROL('B'): you.teleport(true, false, true); break;
    case CONTROL('D'): wizard_edit_durations(); break;
    case CONTROL('F'): debug_fight_statistics(false, true); break;
    case CONTROL('G'): debug_ghosts(); break;
    case CONTROL('H'): wizard_set_hunger_state(); break;
    case CONTROL('I'): debug_item_statistics(); break;
    case CONTROL('L'): wizard_set_xl(); break;
    case CONTROL('T'): debug_terp_dlua(); break;
    case CONTROL('X'): debug_xom_effects(); break;

    case 'O': debug_test_explore();                  break;
    case 'S': wizard_set_skill_level();              break;
    case 'A': wizard_set_all_skills();               break;
    case 'a': acquirement(OBJ_RANDOM, AQ_WIZMODE);   break;
    case 'v': wizard_value_artefact();               break;
    case '+': wizard_make_object_randart();          break;
    case '|': wizard_create_all_artefacts();         break;
    case 'C': wizard_uncurse_item();                 break;
    case 'g': wizard_exercise_skill();               break;
    case 'G': wizard_dismiss_all_monsters();         break;
    case 'c': wizard_draw_card();                    break;
    case 'H': wizard_heal(true);                     break;
    case 'h': wizard_heal(false);                    break;
    case 'b': blink(1000, true, true);               break;
    case '~': wizard_interlevel_travel();            break;
    case '"': debug_list_monsters();                 break;
    case 't': wizard_tweak_object();                 break;
    case 'T': debug_make_trap();                     break;
    case '\\': debug_make_shop();                    break;
    case 'f': debug_fight_statistics(false);         break;
    case 'F': debug_fight_statistics(true);          break;
    case 'm': wizard_create_spec_monster();          break;
    case 'M': wizard_create_spec_monster_name();     break;
    case 'R': wizard_spawn_control();                break;
    case 'r': wizard_change_species();               break;
    case '>': wizard_place_stairs(true);             break;
    case '<': wizard_place_stairs(false);            break;
    case 'P': wizard_create_portal();                break;
    case 'L': debug_place_map();                     break;
    case 'i': wizard_identify_pack();                break;
    case 'I': wizard_unidentify_pack();              break;
    case 'Z':
    case 'z': wizard_cast_spec_spell();              break;
    case '(':
    case ')': wizard_create_feature();               break;
    case ':': wizard_list_branches();                break;
    case '{': wizard_map_level();                    break;
    case '}': wizard_reveal_traps();                 break;
    case '@': wizard_set_stats();                    break;
    case '^': wizard_gain_piety();                   break;
    case '_': wizard_get_religion();                 break;
    case '-': wizard_get_god_gift();                 break;
    case '\'': wizard_list_items();                  break;
    case 'd': wizard_level_travel(true);             break;
    case 'D': wizard_detect_creatures();             break;
    case 'u': case 'U': wizard_level_travel(false);  break;
    case '%': case 'o': wizard_create_spec_object(); break;

    case 'x':
        you.experience = 1 + exp_needed( 2 + you.experience_level );
        level_change();
        break;

    case 's':
        you.exp_available = FULL_EXP_POOL;
        you.redraw_experience = true;
        break;

    case '$':
        you.add_gold(1000);
        if (!Options.show_gold_turns)
        {
            mprf("You now have %d gold piece%s.",
                 you.gold, you.gold != 1 ? "s" : "");
        }
        break;

    case 'B':
        if (you.level_type != LEVEL_ABYSS)
            banished(DNGN_ENTER_ABYSS, "wizard command");
        else
            down_stairs(you.your_level, DNGN_EXIT_ABYSS);
        break;

    case CONTROL('A'):
        if (you.level_type == LEVEL_ABYSS)
            abyss_teleport(true);
        else
            mpr("You can only abyss_teleport() inside the Abyss.");
        break;

    case ']':
        if (!wizard_add_mutation())
            mpr( "Failure to give mutation." );
        break;

    case '=':
        mprf( "Cost level: %d  Skill points: %d  Next cost level: %d",
              you.skill_cost_level, you.total_skill_points,
              skill_cost_needed( you.skill_cost_level + 1 ) );
        break;

    case 'X':
    {
        int result = 0;
        do
        {
            if (you.religion == GOD_XOM)
                result = xom_acts(abs(you.piety - HALF_MAX_PIETY));
            else
                result = xom_acts(coinflip(), random_range(0, HALF_MAX_PIETY));
        }
        while (result == 0);
        break;
    }
    case 'p':
        dungeon_terrain_changed(you.pos(), DNGN_ENTER_PANDEMONIUM, false);
        break;

    case 'l':
        dungeon_terrain_changed(you.pos(), DNGN_ENTER_LABYRINTH, false);
        break;

    case 'k':
        if (you.level_type == LEVEL_LABYRINTH)
            change_labyrinth(true);
        else
            mpr("This only makes sense in a labyrinth!");
        break;


    default:
        if (!silent_fail)
        {
            formatted_mpr(formatted_string::parse_string(
                              "Not a <magenta>Wizard</magenta> Command."));
        }
        break;
    }
    // Force the placement of any delayed monster gifts.
    you.turn_is_over = true;
    religion_turn_end();

    you.turn_is_over = false;
}

static void _handle_wizard_command( void )
{
    int wiz_command;

    // WIZ_NEVER gives protection for those who have wiz compiles,
    // and don't want to risk their characters.
    if (Options.wiz_mode == WIZ_NEVER)
        return;

    if (!you.wizard)
    {
        mpr("WARNING: ABOUT TO ENTER WIZARD MODE!", MSGCH_WARN);

#ifndef SCORE_WIZARD_MODE
        mpr("If you continue, your game will not be scored!", MSGCH_WARN);
#endif

        if (!yesno( "Do you really want to enter wizard mode?", false, 'n' ))
            return;

        take_note(Note(NOTE_MESSAGE, 0, 0, "Entered wizard mode."));

        you.wizard = true;
        redraw_screen();

        if (crawl_state.cmd_repeat_start)
        {
            crawl_state.cancel_cmd_repeat("Can't repeat entering wizard "
                                          "mode.");
            return;
        }
    }

    {
        mpr("Enter Wizard Command (? - help): ", MSGCH_PROMPT);
        cursor_control con(true);
        wiz_command = getch();
    }

    if (crawl_state.cmd_repeat_start)
    {
        // Easiest to list which wizard commands *can* be repeated.
        switch (wiz_command)
        {
        case 'x':
        case '$':
        case 'a':
        case 'c':
        case 'h':
        case 'H':
        case 'm':
        case 'M':
        case 'X':
        case '!':
        case '[':
        case ']':
        case '^':
        case '%':
        case 'o':
        case 'z':
        case 'Z':
            break;

        default:
            crawl_state.cant_cmd_repeat("You cannot repeat that "
                                        "wizard command.");
            return;
        }
    }

    _do_wizard_command(wiz_command, false);
}
#endif

// Set up the running variables for the current run.
static void _start_running( int dir, int mode )
{
    if (Tutorial.tutorial_events[TUT_SHIFT_RUN] && mode == RMODE_START)
        Tutorial.tutorial_events[TUT_SHIFT_RUN] = false;

    if (i_feel_safe(true))
        you.running.initialise(dir, mode);
}

static bool _cmd_is_repeatable(command_type cmd, bool is_again = false)
{
    switch (cmd)
    {
    // Informational commands
    case CMD_LOOK_AROUND:
    case CMD_INSPECT_FLOOR:
    case CMD_EXAMINE_OBJECT:
    case CMD_LIST_WEAPONS:
    case CMD_LIST_ARMOUR:
    case CMD_LIST_JEWELLERY:
    case CMD_LIST_EQUIPMENT:
    case CMD_LIST_GOLD:
    case CMD_CHARACTER_DUMP:
    case CMD_DISPLAY_COMMANDS:
    case CMD_DISPLAY_INVENTORY:
    case CMD_DISPLAY_KNOWN_OBJECTS:
    case CMD_DISPLAY_MUTATIONS:
    case CMD_DISPLAY_SKILLS:
    case CMD_DISPLAY_OVERMAP:
    case CMD_DISPLAY_RELIGION:
    case CMD_DISPLAY_CHARACTER_STATUS:
    case CMD_DISPLAY_SPELLS:
    case CMD_EXPERIENCE_CHECK:
    case CMD_RESISTS_SCREEN:
    case CMD_READ_MESSAGES:
    case CMD_SEARCH_STASHES:
        mpr("You can't repeat informational commands.");
        return (false);

    // Multi-turn commands
    case CMD_PICKUP:
    case CMD_DROP:
    case CMD_BUTCHER:
    case CMD_GO_UPSTAIRS:
    case CMD_GO_DOWNSTAIRS:
    case CMD_WIELD_WEAPON:
    case CMD_WEAPON_SWAP:
    case CMD_WEAR_JEWELLERY:
    case CMD_REMOVE_JEWELLERY:
    case CMD_MEMORISE_SPELL:
    case CMD_EXPLORE:
    case CMD_INTERLEVEL_TRAVEL:
        mpr("You can't repeat multi-turn commands.");
        return (false);

    // Miscellaneous non-repeatable commands.
    case CMD_TOGGLE_AUTOPICKUP:
    case CMD_TOGGLE_FRIENDLY_PICKUP:
    case CMD_ADJUST_INVENTORY:
    case CMD_QUIVER_ITEM:
    case CMD_REPLAY_MESSAGES:
    case CMD_REDRAW_SCREEN:
    case CMD_MACRO_ADD:
    case CMD_SAVE_GAME:
    case CMD_SAVE_GAME_NOW:
    case CMD_SUSPEND_GAME:
    case CMD_QUIT:
    case CMD_DESTROY_ITEM:
    case CMD_FORGET_STASH:
    case CMD_FIX_WAYPOINT:
    case CMD_CLEAR_MAP:
    case CMD_INSCRIBE_ITEM:
    case CMD_MAKE_NOTE:
    case CMD_CYCLE_QUIVER_FORWARD:
#ifdef USE_TILE
    case CMD_EDIT_PLAYER_TILE:
#endif
        mpr("You can't repeat that command.");
        return (false);

    case CMD_DISPLAY_MAP:
        mpr("You can't repeat map commands.");
        return (false);

    case CMD_MOUSE_MOVE:
    case CMD_MOUSE_CLICK:
        mpr("You can't repeat mouse clicks or movements.");
        return (false);

    case CMD_REPEAT_CMD:
        mpr("You can't repeat the repeat command!");
        return (false);

    case CMD_RUN_LEFT:
    case CMD_RUN_DOWN:
    case CMD_RUN_UP:
    case CMD_RUN_RIGHT:
    case CMD_RUN_UP_LEFT:
    case CMD_RUN_DOWN_LEFT:
    case CMD_RUN_UP_RIGHT:
    case CMD_RUN_DOWN_RIGHT:
        mpr("Why would you want to repeat a run command?");
        return (false);

    case CMD_PREV_CMD_AGAIN:
        ASSERT(!is_again);
        if (crawl_state.prev_cmd == CMD_NO_CMD)
        {
            mpr("No previous command to repeat.");
            return (false);
        }

        return _cmd_is_repeatable(crawl_state.prev_cmd, true);

    case CMD_MOVE_NOWHERE:
    case CMD_REST:
    case CMD_SEARCH:
        return (i_feel_safe(true));

    case CMD_MOVE_LEFT:
    case CMD_MOVE_DOWN:
    case CMD_MOVE_UP:
    case CMD_MOVE_RIGHT:
    case CMD_MOVE_UP_LEFT:
    case CMD_MOVE_DOWN_LEFT:
    case CMD_MOVE_UP_RIGHT:
    case CMD_MOVE_DOWN_RIGHT:
        if (!i_feel_safe())
        {
            return yesno("Really repeat movement command while monsters "
                         "are nearby?", false, 'n');
        }

        return (true);

    case CMD_NO_CMD:
    case CMD_NO_CMD_DEFAULT:
        mpr("Unknown command, not repeating.");
        return (false);

    default:
        return (true);
    }

    return (false);
}

// Used to determine whether to apply the berserk penalty at end of round.
bool apply_berserk_penalty = false;

static void _center_cursor()
{
#ifndef USE_TILE
    const coord_def cwhere = grid2view(you.pos());
    cgotoxy(cwhere.x, cwhere.y);
#endif
}

//
//  This function handles the player's input. It's called from main(),
//  from inside an endless loop.
//
static void _input()
{
#if defined(USE_UNIX_SIGNALS) && defined(SIGHUP_SAVE) && defined(USE_CURSES)
    if (crawl_state.seen_hups)
        sighup_save_and_exit();
#endif

    crawl_state.clear_mon_acting();

    religion_turn_start();
    you.update_beholders();

    // Currently only set if Xom accidentally kills the player.
    you.reset_escaped_death();
    flush_prev_message();

    if (crawl_state.is_replaying_keys() && crawl_state.is_repeating_cmd()
        && kbhit())
    {
        // User pressed a key, so stop repeating commands and discard
        // the keypress.
        crawl_state.cancel_cmd_repeat("Key pressed, interrupting command "
                                      "repetition.");
        crawl_state.prev_cmd = CMD_NO_CMD;
        getchm();
        return;
    }

    update_monsters_in_view();

    you.turn_is_over = false;
    _prep_input();

    const bool player_feels_safe = i_feel_safe();

    if (Tutorial.tutorial_left)
    {
        Tutorial.tut_just_triggered = false;

        if (you.attribute[ATTR_HELD])
            learned_something_new(TUT_CAUGHT_IN_NET);
        else if (player_feels_safe && you.level_type != LEVEL_ABYSS)
        {
            // We don't want those "Whew, it's safe to rest now" messages
            // if you were just cast into the Abyss. Right?

            if (2 * you.hp < you.hp_max
                || 2 * you.magic_points < you.max_magic_points)
            {
                tutorial_healing_reminder();
            }
            else if (!you.running
                     && Tutorial.tutorial_events[TUT_SHIFT_RUN]
                     && you.num_turns >= 200
                     && you.hp == you.hp_max
                     && you.magic_points == you.max_magic_points)
            {
                learned_something_new(TUT_SHIFT_RUN);
            }
            else if (!you.running
                     && Tutorial.tutorial_events[TUT_MAP_VIEW]
                     && you.num_turns >= 500
                     && you.hp == you.hp_max
                     && you.magic_points == you.max_magic_points)
            {
                learned_something_new(TUT_MAP_VIEW);
            }
            else if (!you.running
                     && Tutorial.tutorial_events[TUT_AUTO_EXPLORE]
                     && you.num_turns >= 700
                     && you.hp == you.hp_max
                     && you.magic_points == you.max_magic_points)
            {
                learned_something_new(TUT_AUTO_EXPLORE);
            }
        }
        else
        {
            if (2*you.hp < you.hp_max)
                learned_something_new(TUT_RUN_AWAY);

            if (Tutorial.tutorial_type == TUT_MAGIC_CHAR && you.magic_points < 1)
                learned_something_new(TUT_RETREAT_CASTER);
        }
    }

    if (you.cannot_act())
    {
        if (crawl_state.repeat_cmd != CMD_WIZARD)
        {
            crawl_state.cancel_cmd_repeat("Cannot move, cancelling command "
                                          "repetition.");
        }
        world_reacts();
        return;
    }

    // Stop autoclearing more now that we have control back.
    if (!you_are_delayed())
        set_more_autoclear(false);

    if (need_to_autopickup())
        autopickup();

    if (need_to_autoinscribe())
        autoinscribe();

    handle_delay();

    if (you_are_delayed() && current_delay_action() != DELAY_MACRO_PROCESS_KEY)
    {
        if (you.time_taken)
            world_reacts();
        return;
    }

    if (you.turn_is_over)
    {
        world_reacts();
        return;
    }

    crawl_state.check_term_size();
    if (crawl_state.terminal_resized)
        handle_terminal_resize();

    repeat_again_rec.paused = (crawl_state.is_replaying_keys()
                               && !crawl_state.cmd_repeat_start);

    crawl_state.input_line_curr = 0;

    {
        flush_prev_message();

        clear_macro_process_key_delay();

        crawl_state.waiting_for_command = true;
        c_input_reset(true);

        _center_cursor();

#ifdef USE_TILE
        cursor_control con(false);
#endif
        const command_type cmd = _get_next_cmd();

#if defined(USE_UNIX_SIGNALS) && defined(SIGHUP_SAVE) && defined(USE_CURSES)
        if (crawl_state.seen_hups)
            sighup_save_and_exit();
#endif

        crawl_state.waiting_for_command = false;

        if (cmd != CMD_PREV_CMD_AGAIN && cmd != CMD_REPEAT_CMD
            && cmd != CMD_NO_CMD
            && !crawl_state.is_replaying_keys())
        {
            crawl_state.prev_cmd = cmd;
            crawl_state.input_line_strs.clear();
        }

        if (cmd != CMD_MOUSE_MOVE)
            c_input_reset(false);

        // [dshaligram] If get_next_cmd encountered a Lua macro
        // binding, your turn may be ended by the first invoke of the
        // macro.
        if (!you.turn_is_over && cmd != CMD_NEXT_CMD)
            process_command( cmd );

        repeat_again_rec.paused = true;

        if (cmd != CMD_MOUSE_MOVE)
            c_input_reset(false, true);

        // If the command was CMD_REPEAT_CMD, then the key for the
        // command to repeat has been placed into the macro buffer,
        // so return now to let input() be called again while
        // the keys to repeat are recorded.
        if (cmd == CMD_REPEAT_CMD)
            return;

        // If the command was CMD_PREV_CMD_AGAIN then _input() has been
        // recursively called by _do_prev_cmd_again() via process_command()
        // to re-do the command, so there's nothing more to do.
        if (cmd == CMD_PREV_CMD_AGAIN)
            return;
    }

    if (need_to_autoinscribe())
        autoinscribe();

    if (you.turn_is_over)
    {
        if (apply_berserk_penalty)
            _do_berserk_no_combat_penalty();

        world_reacts();
    }
    else
        viewwindow(false);

    _update_replay_state();

    if (you.num_turns != -1)
    {
        PlaceInfo& curr_PlaceInfo = you.get_place_info();
        PlaceInfo  delta;

        delta.turns_total++;
        delta.elapsed_total += you.time_taken;

        switch (you.running)
        {
        case RMODE_INTERLEVEL:
            delta.turns_interlevel++;
            delta.elapsed_interlevel += you.time_taken;
            break;

        case RMODE_EXPLORE_GREEDY:
        case RMODE_EXPLORE:
            delta.turns_explore++;
            delta.elapsed_explore += you.time_taken;
            break;

        case RMODE_TRAVEL:
            delta.turns_travel++;
            delta.elapsed_travel += you.time_taken;
            break;

        default:
            // prev_was_rest is needed so that the turn in which
            // a player is interrupted from resting is counted
            // as a resting turn, rather than "other".
            static bool prev_was_rest = false;

            if (!you.delay_queue.empty()
                && you.delay_queue.front().type == DELAY_REST)
            {
                prev_was_rest = true;
            }

            if (prev_was_rest)
            {
                delta.turns_resting++;
                delta.elapsed_resting += you.time_taken;
            }
            else
            {
                delta.turns_other++;
                delta.elapsed_other += you.time_taken;
            }

            if (you.delay_queue.empty()
                || you.delay_queue.front().type != DELAY_REST)
            {
                prev_was_rest = false;
            }
            break;
        }

        you.global_info += delta;
        you.global_info.assert_validity();

        curr_PlaceInfo += delta;
        curr_PlaceInfo.assert_validity();
    }

    crawl_state.clear_god_acting();
}

static bool _stairs_check_mesmerised()
{
    if (you.beheld() && !you.confused())
    {
        const monsters* beholder = you.get_any_beholder();
        mprf("You cannot move away from %s!",
             beholder->name(DESC_NOCAP_THE, true).c_str());
        return (true);
    }

    return (false);
}

static bool _marker_vetoes_stair()
{
    return marker_vetoes_operation("veto_stair");
}

// Maybe prompt to enter a portal, return true if we should enter the
// portal, false if the user said no at the prompt.
static bool _prompt_dangerous_portal(dungeon_feature_type ftype)
{
    switch(ftype)
    {
    case DNGN_ENTER_PANDEMONIUM:
    case DNGN_ENTER_ABYSS:
        return yesno("If you enter this portal you will not be able to return "
                     "immediately. Continue?", false, 'n');

    default:
        return (true);
    }
}

static void _go_downstairs();
static void _go_upstairs()
{
    ASSERT(!crawl_state.arena && !crawl_state.arena_suspended);

    const dungeon_feature_type ygrd = grd(you.pos());

    if (_stairs_check_mesmerised())
        return;

    if (you.attribute[ATTR_HELD])
    {
        mpr("You're held in a net!");
        return;
    }

    if (ygrd == DNGN_ENTER_SHOP)
    {
        if (you.berserk())
            canned_msg(MSG_TOO_BERSERK);
        else
            shop();
        return;
    }
    else if (ygrd == DNGN_ENTER_HELL && you.level_type != LEVEL_DUNGEON)
    {
        mpr("You can't enter Hell from outside the dungeon!",
            MSGCH_ERROR);
        return;
    }
    // Up and down both work for portals.
    else if (get_feature_dchar(ygrd) == DCHAR_ARCH
             && feat_stair_direction(ygrd) != CMD_NO_CMD
             && ygrd != DNGN_ENTER_ZOT)
    {
        ;
    }
    else if (feat_stair_direction(ygrd) != CMD_GO_UPSTAIRS)
    {
        if (ygrd == DNGN_STONE_ARCH)
            mpr("There is nothing on the other side of the stone arch.");
        else if (ygrd == DNGN_ABANDONED_SHOP)
            mpr("This shop appears to be closed.");
        else
            mpr("You can't go up here!");
        return;
    }

    if (!_prompt_dangerous_portal(ygrd))
        return;

    // Does the next level have a warning annotation?
    if (!check_annotation_exclusion_warning())
        return;

    if (_marker_vetoes_stair())
        return;

    if (you.duration[DUR_MISLED])
    {
        mpr("Away from their source, illusions no longer mislead you.", MSGCH_DURATION);
        you.duration[DUR_MISLED] = 0;
    }

    tag_followers(); // Only those beside us right now can follow.
    start_delay(DELAY_ASCENDING_STAIRS,
                1 + (you.burden_state > BS_UNENCUMBERED));
}

static void _go_downstairs()
{
    ASSERT(!crawl_state.arena && !crawl_state.arena_suspended);

    const dungeon_feature_type ygrd = grd(you.pos());

    const bool shaft = (get_trap_type(you.pos()) == TRAP_SHAFT
                        && ygrd != DNGN_UNDISCOVERED_TRAP);

    if (_stairs_check_mesmerised())
        return;

    if (shaft && you.flight_mode() == FL_LEVITATE)
    {
        mpr("You can't fall through a shaft while levitating.");
        return;
    }

    // Up and down both work for shops.
    if (ygrd == DNGN_ENTER_SHOP)
    {
        if (you.berserk())
            canned_msg(MSG_TOO_BERSERK);
        else
            shop();
        return;
    }
    else if (ygrd == DNGN_ENTER_HELL && you.level_type != LEVEL_DUNGEON)
    {
        mpr("You can't enter Hell from outside the dungeon!",
            MSGCH_ERROR);
        return;
    }
    // Up and down both work for portals.
    else if (get_feature_dchar(ygrd) == DCHAR_ARCH
             && feat_stair_direction(ygrd) != CMD_NO_CMD
             && ygrd != DNGN_ENTER_ZOT)
    {
        ;
    }
    else if (feat_stair_direction(ygrd) != CMD_GO_DOWNSTAIRS
             && !shaft)
    {
        if (ygrd == DNGN_STONE_ARCH)
            mpr("There is nothing on the other side of the stone arch.");
        else if (ygrd == DNGN_ABANDONED_SHOP)
            mpr("This shop appears to be closed.");
        else
            mpr( "You can't go down here!" );
        return;
    }

    if (you.attribute[ATTR_HELD])
    {
        mpr("You're held in a net!");
        return;
    }

    if (!_prompt_dangerous_portal(ygrd))
        return;

    // Does the next level have a warning annotation?
    // Also checks for entering a labyrinth with teleportitis.
    if (!check_annotation_exclusion_warning())
        return;

    if (you.duration[DUR_MISLED])
    {
        mpr("Away from their source, illusions no longer mislead you.", MSGCH_DURATION);
        you.duration[DUR_MISLED] = 0;
    }

    if (shaft)
    {
        start_delay( DELAY_DESCENDING_STAIRS, 0, you.your_level );
    }
    else
    {
        if (_marker_vetoes_stair())
            return;

        tag_followers(); // Only those beside us right now can follow.
        start_delay(DELAY_DESCENDING_STAIRS,
                    1 + (you.burden_state > BS_UNENCUMBERED),
                    you.your_level);
    }
}

static void _experience_check()
{
    mprf("You are a level %d %s %s.",
         you.experience_level,
         species_name(you.species,you.experience_level).c_str(),
         you.class_name);

    if (you.experience_level < 27)
    {
        int xp_needed = (exp_needed(you.experience_level+2)-you.experience)+1;
        mprf( "Level %d requires %ld experience (%d point%s to go!)",
              you.experience_level + 1,
              exp_needed(you.experience_level + 2) + 1,
              xp_needed,
              (xp_needed > 1) ? "s" : "");
    }
    else
    {
        mpr( "I'm sorry, level 27 is as high as you can go." );
        mpr( "With the way you've been playing, I'm surprised you got this far." );
    }

    if (you.real_time != -1)
    {
        const time_t curr = you.real_time + (time(NULL) - you.start_time);
        msg::stream << "Play time: " << make_time_string(curr)
                    << " (" << you.num_turns << " turns)"
                    << std::endl;
    }
#ifdef DEBUG_DIAGNOSTICS
    if (wearing_amulet(AMU_THE_GOURMAND))
        mprf(MSGCH_DIAGNOSTICS, "Gourmand charge: %d",
             you.duration[DUR_GOURMAND]);

    mprf(MSGCH_DIAGNOSTICS, "Turns spent on this level: %d",
         env.turns_on_level);
#endif
}

static void _print_friendly_pickup_setting(bool was_changed)
{
    std::string now = (was_changed? "now " : "");

    if (you.friendly_pickup == FRIENDLY_PICKUP_NONE)
    {
        mprf("Your intelligent allies are %sforbidden to pick up anything at all.",
             now.c_str());
    }
    else if (you.friendly_pickup == FRIENDLY_PICKUP_FRIEND)
    {
        mprf("Your intelligent allies may %sonly pick up items dropped by allies.",
             now.c_str());
    }
    else if (you.friendly_pickup == FRIENDLY_PICKUP_PLAYER)
    {
        mprf("Your intelligent allies may %sonly pick up items dropped by you "
             "and your allies.", now.c_str());
    }
    else if (you.friendly_pickup == FRIENDLY_PICKUP_ALL)
    {
        mprf("Your intelligent allies may %spick up anything they need.",
             now.c_str());
    }
    else
        mprf(MSGCH_ERROR, "Your allies%s are collecting bugs!", now.c_str());
}

// Note that in some actions, you don't want to clear afterwards.
// e.g. list_jewellery, etc.
void process_command( command_type cmd )
{
    apply_berserk_penalty = true;
    switch (cmd)
    {
#ifdef USE_TILE
    case CMD_EDIT_PLAYER_TILE:
        tiles.draw_doll_edit();
        break;

    case CMD_TOGGLE_SPELL_DISPLAY:
        if (Options.tile_display == TDSP_SPELLS)
            Options.tile_display = TDSP_INVENT;
        else
            Options.tile_display = TDSP_SPELLS;

        tiles.update_inventory();
        break;
#endif

    case CMD_OPEN_DOOR_UP_RIGHT:   _open_door( 1, -1); break;
    case CMD_OPEN_DOOR_UP:         _open_door( 0, -1); break;
    case CMD_OPEN_DOOR_UP_LEFT:    _open_door(-1, -1); break;
    case CMD_OPEN_DOOR_RIGHT:      _open_door( 1,  0); break;
    case CMD_OPEN_DOOR_DOWN_RIGHT: _open_door( 1,  1); break;
    case CMD_OPEN_DOOR_DOWN:       _open_door( 0,  1); break;
    case CMD_OPEN_DOOR_DOWN_LEFT:  _open_door(-1,  1); break;
    case CMD_OPEN_DOOR_LEFT:       _open_door(-1,  0); break;

    case CMD_MOVE_DOWN_LEFT:  _move_player(-1,  1); break;
    case CMD_MOVE_DOWN:       _move_player( 0,  1); break;
    case CMD_MOVE_UP_RIGHT:   _move_player( 1, -1); break;
    case CMD_MOVE_UP:         _move_player( 0, -1); break;
    case CMD_MOVE_UP_LEFT:    _move_player(-1, -1); break;
    case CMD_MOVE_LEFT:       _move_player(-1,  0); break;
    case CMD_MOVE_DOWN_RIGHT: _move_player( 1,  1); break;
    case CMD_MOVE_RIGHT:      _move_player( 1,  0); break;

    case CMD_REST:
        if (you.hunger_state == HS_STARVING && !you_min_hunger())
        {
            mpr("You are too hungry to rest.");
            break;
        }
        if (i_feel_safe())
        {
            if ((you.hp == you.hp_max || you.species == SP_VAMPIRE
                                         && you.hunger_state == HS_STARVING)
                && you.magic_points == you.max_magic_points )
            {
                mpr("You start searching.");
            }
            else
                mpr("You start resting.");
        }
        _start_running( RDIR_REST, RMODE_REST_DURATION );
        break;

    case CMD_RUN_DOWN_LEFT:
        _start_running( RDIR_DOWN_LEFT, RMODE_START );
        break;
    case CMD_RUN_DOWN:
        _start_running( RDIR_DOWN, RMODE_START );
        break;
    case CMD_RUN_UP_RIGHT:
        _start_running( RDIR_UP_RIGHT, RMODE_START );
        break;
    case CMD_RUN_UP:
        _start_running( RDIR_UP, RMODE_START );
        break;
    case CMD_RUN_UP_LEFT:
        _start_running( RDIR_UP_LEFT, RMODE_START );
        break;
    case CMD_RUN_LEFT:
        _start_running( RDIR_LEFT, RMODE_START );
        break;
    case CMD_RUN_DOWN_RIGHT:
        _start_running( RDIR_DOWN_RIGHT, RMODE_START );
        break;
    case CMD_RUN_RIGHT:
        _start_running( RDIR_RIGHT, RMODE_START );
        break;

    case CMD_DISABLE_MORE:
        Options.show_more_prompt = false;
        break;

    case CMD_ENABLE_MORE:
        Options.show_more_prompt = true;
        break;

    case CMD_REPEAT_KEYS:
        ASSERT(crawl_state.is_repeating_cmd());

        if (crawl_state.prev_repetition_turn == you.num_turns
            && crawl_state.repeat_cmd != CMD_WIZARD
            && (crawl_state.repeat_cmd != CMD_PREV_CMD_AGAIN
                || crawl_state.prev_cmd != CMD_WIZARD))
        {
            // This is a catch-all that shouldn't really happen.
            // If the command always takes zero turns, then it
            // should be prevented in cmd_is_repeatable().  If
            // a command sometimes takes zero turns (because it
            // can't be done, for instance), then
            // crawl_state.zero_turns_taken() should be called when
            // it does take zero turns, to cancel command repetition
            // before we reach here.
#ifdef WIZARD
            crawl_state.cant_cmd_repeat("Can't repeat a command which "
                                        "takes no turns (unless it's a "
                                        "wizard command), cancelling ");
#else
            crawl_state.cant_cmd_repeat("Can't repeat a command which "
                                        "takes no turns, cancelling "
                                        "repetitions.");
#endif
            crawl_state.cancel_cmd_repeat();
            return;
        }

        crawl_state.cmd_repeat_count++;
        if (crawl_state.cmd_repeat_count >= crawl_state.cmd_repeat_goal)
        {
            crawl_state.cancel_cmd_repeat();
            return;
        }

        ASSERT(crawl_state.repeat_cmd_keys.size() > 0);
        repeat_again_rec.paused = true;
        macro_buf_add(crawl_state.repeat_cmd_keys);
        macro_buf_add(KEY_REPEAT_KEYS);

        crawl_state.prev_repetition_turn = you.num_turns;

        break;

    case CMD_TOGGLE_AUTOPICKUP:
        if (Options.autopickup_on < 1)
            Options.autopickup_on = 1;
        else
            Options.autopickup_on = 0;
        mprf("Autopickup is now %s.", Options.autopickup_on > 0 ? "on" : "off");
        break;

    case CMD_TOGGLE_FRIENDLY_PICKUP:
    {
        // Toggle pickup mode for friendlies.
        _print_friendly_pickup_setting(false);

        mpr("Change to (d)efault, (n)othing, (f)riend-dropped, (p)layer, "
            "or (a)ll? ", MSGCH_PROMPT);
        char type;
        {
            cursor_control con(true);
            type = (char) getchm(KMC_DEFAULT);
        }
        type = tolower(type);

        if (type == 'd')
            you.friendly_pickup = Options.default_friendly_pickup;
        else if (type == 'n')
            you.friendly_pickup = FRIENDLY_PICKUP_NONE;
        else if (type == 'f')
            you.friendly_pickup = FRIENDLY_PICKUP_FRIEND;
        else if (type == 'p')
            you.friendly_pickup = FRIENDLY_PICKUP_PLAYER;
        else if (type == 'a')
            you.friendly_pickup = FRIENDLY_PICKUP_ALL;
        else
        {
            canned_msg( MSG_OK );
            break;
        }
        _print_friendly_pickup_setting(true);
        break;
    }
    case CMD_MAKE_NOTE:
        make_user_note();
        break;

    case CMD_READ_MESSAGES:
#ifdef DGL_SIMPLE_MESSAGING
        if (SysEnv.have_messages)
            _read_messages();
#endif
        break;

    case CMD_CLEAR_MAP:
        if (player_in_mappable_area())
        {
            mpr("Clearing level map.");
            clear_map();
            crawl_view.set_player_at(you.pos());
        }
        break;

    case CMD_DISPLAY_OVERMAP: display_overmap(); break;
    case CMD_GO_UPSTAIRS:     _go_upstairs();    break;
    case CMD_GO_DOWNSTAIRS:   _go_downstairs();  break;
    case CMD_OPEN_DOOR:       _open_door(0, 0);  break;
    case CMD_CLOSE_DOOR:      _close_door(coord_def(0, 0)); break;

    case CMD_DROP:
        drop();
        if (Options.stash_tracking >= STM_DROPPED)
            StashTrack.add_stash();
        break;

    case CMD_SEARCH_STASHES:
        if (Tutorial.tut_stashes)
            Tutorial.tut_stashes = 0;
        StashTrack.search_stashes();
        break;

    case CMD_FORGET_STASH:
        if (Options.stash_tracking >= STM_EXPLICIT)
            StashTrack.no_stash();
        break;

    case CMD_BUTCHER:
        butchery();
        break;

    case CMD_DISPLAY_INVENTORY:
        get_invent(OSEL_ANY);
        break;

    case CMD_EVOKE:
        if (!evoke_item())
            flush_input_buffer( FLUSH_ON_FAILURE );
        break;

    case CMD_EVOKE_WIELDED:
        if (!evoke_item(you.equip[EQ_WEAPON]))
            flush_input_buffer( FLUSH_ON_FAILURE );
        break;

    case CMD_PICKUP:
        pickup();
        break;

    case CMD_INSPECT_FLOOR:
        request_autopickup();
        break;

    case CMD_FULL_VIEW:
        full_describe_view();
        break;

    case CMD_WIELD_WEAPON:
        wield_weapon(false);
        break;

    case CMD_FIRE:
        fire_thing();
        break;

    case CMD_QUIVER_ITEM:
        choose_item_for_quiver();
        break;

    case CMD_THROW_ITEM_NO_QUIVER:
        throw_item_no_quiver();
        break;

    case CMD_WEAR_ARMOUR:
        wear_armour();
        break;

    case CMD_REMOVE_ARMOUR:
    {
        if (player_in_bat_form())
        {
            mpr("You can't wear or remove anything in your present form.");
            break;
        }
        int index = 0;

        if (armour_prompt("Take off which item?", &index, OPER_TAKEOFF))
            takeoff_armour(index);
    }
    break;

    case CMD_REMOVE_JEWELLERY:
        remove_ring();
        break;

    case CMD_WEAR_JEWELLERY:
        puton_ring(-1);
        break;

    case CMD_ADJUST_INVENTORY:
        adjust();
        break;

    case CMD_MEMORISE_SPELL:
        if (!learn_spell())
            flush_input_buffer( FLUSH_ON_FAILURE );
        break;

    case CMD_ZAP_WAND:
        zap_wand();
        break;

    case CMD_EAT:
        eat_food();
        break;

    case CMD_USE_ABILITY:
        if (!activate_ability())
            flush_input_buffer( FLUSH_ON_FAILURE );
        break;

    case CMD_DISPLAY_MUTATIONS:
        display_mutations();
        redraw_screen();
        break;

    case CMD_EXAMINE_OBJECT:
        examine_object();
        break;

    case CMD_PRAY:
        pray();
        break;

    case CMD_DISPLAY_RELIGION:
        describe_god( you.religion, true );
        redraw_screen();
        break;

    case CMD_MOVE_NOWHERE:
    case CMD_SEARCH:
        search_around();
        you.turn_is_over = true;
        break;

    case CMD_QUAFF:
        drink();
        break;

    case CMD_READ:
        read_scroll();
        break;

    case CMD_LOOK_AROUND:
    {
        mpr("Move the cursor around to observe a square "
            "(v - describe square, ? - help)", MSGCH_PROMPT);

        struct dist lmove;   // Will be initialised by direction().
        direction(lmove, DIR_TARGET, TARG_ANY, -1, true, false);
        if (lmove.isValid && lmove.isTarget && !lmove.isCancel
            && !crawl_state.arena_suspended)
        {
            start_travel( lmove.target );
        }
        break;
    }

    case CMD_CAST_SPELL:
    case CMD_FORCE_CAST_SPELL:
        if (player_in_bat_form()
            || you.attribute[ATTR_TRANSFORMATION] == TRAN_PIG)
        {
           canned_msg(MSG_PRESENT_FORM);
           break;
        }

        // Randart weapons.
        if (scan_artefacts(ARTP_PREVENT_SPELLCASTING))
        {
            mpr("Something interferes with your magic!");
            flush_input_buffer( FLUSH_ON_FAILURE );
            break;
        }

        if (Tutorial.tutorial_left)
            Tutorial.tut_spell_counter++;
        if (!cast_a_spell(cmd == CMD_CAST_SPELL))
            flush_input_buffer( FLUSH_ON_FAILURE );
        break;

    case CMD_DISPLAY_SPELLS:
        inspect_spells();
        break;

    case CMD_WEAPON_SWAP:
        wield_weapon(true);
        break;

        // [ds] Waypoints can be added from the level-map, and we need
        // Ctrl+F for nobler things. Who uses waypoints, anyway?
        // Update: Appears people do use waypoints. Reinstating, on
        // CONTROL('W'). This means Ctrl+W is no longer a wizmode
        // trigger, but there's always '&'. :-)
    case CMD_FIX_WAYPOINT:
        travel_cache.add_waypoint();
        break;

    case CMD_INTERLEVEL_TRAVEL:
        if (Tutorial.tut_travel)
            Tutorial.tut_travel = 0;

        if (!can_travel_interlevel())
        {
            if (you.running.pos == you.pos())
            {
                mpr("You're already here.");
                break;
            }
            else if (!you.running.pos.x || !you.running.pos.y)
            {
                mpr("Sorry, you can't auto-travel out of here.");
                break;
            }

            // Don't ask for a destination if you can only travel
            // within level anyway.
            start_travel(you.running.pos);
        }
        else
            start_translevel_travel_prompt();

        if (you.running)
            mesclr();
        break;

    case CMD_ANNOTATE_LEVEL:
        annotate_level();
        break;

    case CMD_EXPLORE:
        if (you.hunger_state == HS_STARVING && !you_min_hunger())
        {
            mpr("You need to eat something NOW!");
            break;
        }
        else if (you.level_type == LEVEL_LABYRINTH)
        {
            mpr("No exploration algorithm can help you here.");
            break;
        }
        // Start exploring
        start_explore(Options.explore_greedy);
        break;

    case CMD_DISPLAY_MAP:
        if (Tutorial.tutorial_events[TUT_MAP_VIEW])
            Tutorial.tutorial_events[TUT_MAP_VIEW] = false;

#if (!DEBUG_DIAGNOSTICS)
        if (!player_in_mappable_area())
        {
            mpr("It would help if you knew where you were, first.");
            break;
        }
#endif
        {
            level_pos pos;
#ifdef USE_TILE
            // Since there's no actual overview map, but the functionality
            // exists, give a message to explain what's going on.
            std::string str = "Move the cursor to view the level map, or "
                              "type <w>?</w> for a list of commands.";
            print_formatted_paragraph(str);
#endif

            show_map(pos, true);
            redraw_screen();

#ifdef USE_TILE
            mpr("Returning to the game...");
#endif
            if (pos.pos.x > 0)
                start_translevel_travel(pos);
        }
        break;

    case CMD_DISPLAY_KNOWN_OBJECTS:
        check_item_knowledge();
        break;

    case CMD_REPLAY_MESSAGES:
        replay_messages();
        redraw_screen();
        break;

    case CMD_REDRAW_SCREEN:
        redraw_screen();
        break;

    case CMD_SAVE_GAME_NOW:
        mpr("Saving game... please wait.");
        save_game(true);
        break;

#ifdef USE_UNIX_SIGNALS
    case CMD_SUSPEND_GAME:
        // CTRL-Z suspend behaviour is implemented here,
        // because we want to have CTRL-Y available...
        // and unfortunately they tend to be stuck together.
        clrscr();
#ifndef USE_TILE
        unixcurses_shutdown();
        kill(0, SIGTSTP);
        unixcurses_startup();
#endif
        redraw_screen();
        break;
#endif

    case CMD_DISPLAY_COMMANDS:
        list_commands(0, true);
        break;

    case CMD_EXPERIENCE_CHECK:
        _experience_check();
        break;

    case CMD_SHOUT:
        yell();
        break;

    case CMD_MOUSE_MOVE:
    {
        const coord_def dest = view2grid(crawl_view.mousep);
        if (in_bounds(dest))
            terse_describe_square(dest);
        break;
    }

    case CMD_MOUSE_CLICK:
    {
        // XXX: We should probably use specific commands such as
        // CMD_MOUSE_TRAVEL and get rid of CMD_MOUSE_CLICK and
        // CMD_MOUSE_MOVE.
        c_mouse_event cme = get_mouse_event();
        if (cme && crawl_view.in_view_viewport(cme.pos))
        {
            const coord_def dest = view2grid(cme.pos);
            if (cme.left_clicked())
            {
                if (in_bounds(dest))
                    start_travel(dest);
            }
            else if (cme.right_clicked())
            {
                if (you.see_cell(dest))
                    full_describe_square(dest);
                else
                    mpr("You can't see that place.");
            }
        }
        break;
    }

    case CMD_DISPLAY_CHARACTER_STATUS:
        display_char_status();
        break;

    case CMD_RESISTS_SCREEN:
        print_overview_screen();
        break;

    case CMD_DISPLAY_SKILLS:
        show_skills();
        redraw_screen();
        break;

    case CMD_CHARACTER_DUMP:
        if (dump_char(you.your_name, false))
            mpr("Char dumped successfully.");
        else
            mpr("Char dump unsuccessful! Sorry about that.");
        break;

    case CMD_MACRO_ADD:
        macro_add_query();
        break;

    case CMD_CYCLE_QUIVER_FORWARD:
    case CMD_CYCLE_QUIVER_BACKWARD:
    {
        int dir = (cmd == CMD_CYCLE_QUIVER_FORWARD ? +1 : -1);
        int cur = you.m_quiver->get_fire_item();
        const int next = get_next_fire_item(cur, dir);
#ifdef DEBUG_QUIVER
        mprf(MSGCH_DIAGNOSTICS, "next slot: %d, item: %s", next,
             next == -1 ? "none" : you.inv[next].name(DESC_PLAIN).c_str());
#endif
        if (next != -1)
        {
            // Kind of a hacky way to get quiver to change.
            you.m_quiver->on_item_fired(you.inv[next], true);

            if (next == cur)
                mpr("No other missiles available. Use F to throw any item.");
        }
        else if (cur == -1)
            mpr("No missiles available. Use F to throw any item.");
        break;
    }

    case CMD_LIST_WEAPONS:
        list_weapons();
        break;

    case CMD_LIST_ARMOUR:
        list_armour();
        break;

    case CMD_LIST_JEWELLERY:
        list_jewellery();
        break;

    case CMD_LIST_EQUIPMENT:
        get_invent(OSEL_EQUIP);
        break;

    case CMD_LIST_GOLD:
        if (shopping_list.size() == 0)
            mprf("You have %d gold piece%s.",
                 you.gold, you.gold != 1 ? "s" : "");
        else
            shopping_list.display();

        break;

    case CMD_INSCRIBE_ITEM:
        prompt_inscribe_item();
        break;

#ifdef WIZARD
    case CMD_WIZARD:
        _handle_wizard_command();
        break;
#endif

    case CMD_SAVE_GAME:
        if (yesno("Save game and exit?", true, 'n'))
            save_game(true);
        break;

    case CMD_QUIT:
        if (yes_or_no("Are you sure you want to quit"))
            ouch(INSTANT_DEATH, NON_MONSTER, KILLED_BY_QUITTING);
        else
            canned_msg(MSG_OK);
        break;

    case CMD_REPEAT_CMD:
        _setup_cmd_repeat();
        break;

    case CMD_PREV_CMD_AGAIN:
        _do_prev_cmd_again();
        break;

    case CMD_NO_CMD:
    default:
        if (Tutorial.tutorial_left)
        {
           std::string msg = "Unknown command. (For a list of commands type "
                             "<w>?\?<lightgrey>.)";
           print_formatted_paragraph(msg);
        }
        else // well, not examine, but...
           mpr("Unknown command.", MSGCH_EXAMINE_FILTER);
        break;
    }

    flush_prev_message();
}

static void _prep_input()
{
    you.time_taken = player_speed();
    you.shield_blocks = 0;              // no blocks this round

    textcolor(LIGHTGREY);

    set_redraw_status( REDRAW_LINE_2_MASK | REDRAW_LINE_3_MASK );
    print_stats();
}

// Decrement a single duration. Print the message if the duration runs out.
// Returns true if the duration ended.
// At midpoint (defined by get_expiration_threshold() in player.cc)
// print midmsg and decrease duration by midloss (a randomised amount so as
// to make it impossible to know the exact remaining duration for sure).
// NOTE: The maximum possible midloss should be smaller than midpoint,
//       otherwise the duration may end in the same turn the warning
//       message is printed which would be a bit late.
static bool _decrement_a_duration(duration_type dur, int delay,
                                  const char* endmsg = NULL, int midloss = 0,
                                  const char* midmsg = NULL,
                                  msg_channel_type chan = MSGCH_DURATION)
{
    if (you.duration[dur] < 1)
        return (false);

    const int midpoint = get_expiration_threshold(dur);


    int old_dur = you.duration[dur];

    you.duration[dur] -= delay;
    if (you.duration[dur] < 0)
        you.duration[dur] = 0;

    // Did we cross the mid point? (No longer likely to hit it exactly) -cao
    if (you.duration[dur] <= midpoint && old_dur > midpoint)
    {
        if (midmsg)
            mpr(midmsg, chan);
        you.duration[dur] -= midloss * BASELINE_DELAY;
    }

    // allow fall-through in case midloss ended the duration (it shouldn't)
    if (you.duration[dur] == 0)
    {
        if (endmsg)
            mpr(endmsg, chan);
        return true;
    }

    return false;
}

//  Perhaps we should write functions like: update_liquid_flames(), etc.
//  Even better, we could have a vector of callback functions (or
//  objects) which get installed at some point.
static void _decrement_durations()
{
    int delay = you.time_taken;

    if (wearing_amulet(AMU_THE_GOURMAND))
    {
        if (you.duration[DUR_GOURMAND] < GOURMAND_MAX && coinflip())
            you.duration[DUR_GOURMAND] += delay;
    }
    else
        you.duration[DUR_GOURMAND] = 0;

    if (you.duration[DUR_ICEMAIL_DEPLETED] > 0)
    {
        if(delay > you.duration[DUR_ICEMAIL_DEPLETED])
            you.duration[DUR_ICEMAIL_DEPLETED] = 0;
        else
            you.duration[DUR_ICEMAIL_DEPLETED] -= delay;

        if (!you.duration[DUR_ICEMAIL_DEPLETED])
            mpr("Your icy envelope is fully restored.", MSGCH_DURATION);

        you.redraw_armour_class = true;
    }

    // Must come before might/haste/berserk.
    if (_decrement_a_duration(DUR_BUILDING_RAGE, delay))
        go_berserk(false);

    if (_decrement_a_duration(DUR_SLEEP, delay))
        you.awake();

    dec_napalm_player(delay);

    if (_decrement_a_duration(DUR_ICY_ARMOUR, delay,
                              "Your icy armour evaporates.", coinflip(),
                              "Your icy armour starts to melt."))
    {
        you.redraw_armour_class = true;
    }

    if (_decrement_a_duration(DUR_SILENCE, delay, "Your hearing returns."))
        you.attribute[ATTR_WAS_SILENCED] = 0;

    _decrement_a_duration(DUR_REPEL_MISSILES, delay,
                          "You feel less protected from missiles.",
                          coinflip(),
                          "Your repel missiles spell is about to expire...");

    _decrement_a_duration(DUR_DEFLECT_MISSILES, delay,
                          "You feel less protected from missiles.",
                          coinflip(),
                          "Your deflect missiles spell is about to expire...");

    if (_decrement_a_duration(DUR_REGENERATION, delay,
                              NULL, coinflip(),
                              "Your skin is crawling a little less now."))
    {
        remove_regen(you.attribute[ATTR_DIVINE_REGENERATION]);
    }

    if (you.duration[DUR_PRAYER] > 1)
        you.duration[DUR_PRAYER]--;
    else if (you.duration[DUR_PRAYER] == 1)
        end_prayer();

    if (you.duration[DUR_DIVINE_SHIELD] > 0)
    {
        if (you.duration[DUR_DIVINE_SHIELD] > 1)
        {
            you.duration[DUR_DIVINE_SHIELD] -= delay;
            if(you.duration[DUR_DIVINE_SHIELD] <= 1)
            {
                you.duration[DUR_DIVINE_SHIELD] = 1;
                mpr("Your divine shield starts to fade.", MSGCH_DURATION);
            }
        }

        if (you.duration[DUR_DIVINE_SHIELD] == 1 && !one_chance_in(3))
        {
            you.redraw_armour_class = true;
            if (--you.attribute[ATTR_DIVINE_SHIELD] == 0)
            {
                you.duration[DUR_DIVINE_SHIELD] = 0;
                mpr("Your divine shield fades away.", MSGCH_DURATION);
            }
        }
    }

    //jmf: More flexible weapon branding code.
    int last_value = you.duration[DUR_WEAPON_BRAND];

    if (last_value > 0)
    {
        you.duration[DUR_WEAPON_BRAND] -= delay;

        if (you.duration[DUR_WEAPON_BRAND] <= 0)
        {
            you.duration[DUR_WEAPON_BRAND] = 0;
            item_def& weapon = *you.weapon();
            const int temp_effect = get_weapon_brand(weapon);

            set_item_ego_type(weapon, OBJ_WEAPONS, SPWPN_NORMAL);
            std::string msg = weapon.name(DESC_CAP_YOUR);

            switch (temp_effect)
            {
            case SPWPN_VORPAL:
                if (get_vorpal_type(weapon) == DVORP_SLICING)
                    msg += " seems blunter.";
                else
                    msg += " feels lighter.";
                break;
            case SPWPN_FLAME:
            case SPWPN_FLAMING:
                msg += " goes out.";
                break;
            case SPWPN_FREEZING:
                msg += " stops glowing.";
                break;
            case SPWPN_FROST:
                msg += "'s frost melts away.";
                break;
            case SPWPN_VENOM:
                msg += " stops dripping with poison.";
                break;
            case SPWPN_DRAINING:
                msg += " stops crackling.";
                break;
            case SPWPN_DISTORTION:
                msg += " seems straighter.";
                break;
            case SPWPN_PAIN:
                msg += " seems less pained.";
                break;
            default:
                msg += " seems inexplicably less special.";
                break;
            }

            mpr(msg.c_str(), MSGCH_DURATION);
            you.wield_change = true;
        }
    }

    // FIXME: [ds] Remove this once we've ensured durations can never go < 0?
    if (you.duration[DUR_TRANSFORMATION] <= 0
        && you.attribute[ATTR_TRANSFORMATION] != TRAN_NONE)
    {
        you.duration[DUR_TRANSFORMATION] = 1;
    }

    // Vampire bat transformations are permanent (until ended).
    if (you.species != SP_VAMPIRE || !player_in_bat_form()
        || you.duration[DUR_TRANSFORMATION] <= 5 * BASELINE_DELAY)
    {
        if (_decrement_a_duration(DUR_TRANSFORMATION, delay, NULL, random2(3),
                                  "Your transformation is almost over."))
        {
            untransform();
            you.duration[DUR_BREATH_WEAPON] = 0;
        }
    }

    // Must come after transformation duration.
    _decrement_a_duration(DUR_BREATH_WEAPON, delay,
                          "You have got your breath back.", 0, NULL,
                          MSGCH_RECOVERY);

    _decrement_a_duration(DUR_SWIFTNESS, delay,
                          "You feel sluggish.", coinflip(),
                          "You start to feel a little slower.");
    _decrement_a_duration(DUR_INSULATION, delay,
                          "You feel conductive.", coinflip(),
                          "You start to feel a little less insulated.");

    if (_decrement_a_duration(DUR_STONEMAIL, delay,
                              "Your scaly stone armour disappears.",
                              coinflip(),
                              "Your scaly stone armour is starting "
                              "to flake away."))
    {
        you.redraw_armour_class = true;
        burden_change();
    }

    if (_decrement_a_duration(DUR_PHASE_SHIFT, delay,
                    "You are firmly grounded in the material plane once more.",
                    coinflip(),
                    "You feel closer to the material plane."))
    {
        you.redraw_evasion = true;
    }

    if (_decrement_a_duration(DUR_SEE_INVISIBLE, delay)
        && !you.can_see_invisible())
    {
        mpr("Your eyesight blurs momentarily.", MSGCH_DURATION);
    }

    _decrement_a_duration(DUR_TELEPATHY, delay, "You feel less empathic.");

    if (_decrement_a_duration(DUR_CONDENSATION_SHIELD, delay))
        remove_condensation_shield();

    if (you.duration[DUR_CONDENSATION_SHIELD] && player_res_cold() < 0)
    {
        mpr("You feel very cold.");
        ouch(2 + random2avg(13, 2), NON_MONSTER, KILLED_BY_FREEZING);
    }

    if (_decrement_a_duration(DUR_MAGIC_SHIELD, delay,
                              "Your magical shield disappears."))
    {
        you.redraw_armour_class = true;
    }

    if (_decrement_a_duration(DUR_STONESKIN, delay, "Your skin feels tender."))
        you.redraw_armour_class = true;

    if (_decrement_a_duration(DUR_TELEPORT, delay))
    {
        // Only to a new area of the abyss sometimes (for abyss teleports).
        you_teleport_now(true, one_chance_in(5));
        untag_followers();
    }

    _decrement_a_duration(DUR_CONTROL_TELEPORT, delay,
                          "You feel uncertain.", coinflip(),
                          "You start to feel a little uncertain.");

    if (_decrement_a_duration(DUR_DEATH_CHANNEL, delay,
                              "Your unholy channel expires.", coinflip(),
                              "Your unholy channel is weakening."))
    {
        you.attribute[ATTR_DIVINE_DEATH_CHANNEL] = 0;
    }

    _decrement_a_duration(DUR_SAGE, delay, "You feel less studious.");
    _decrement_a_duration(DUR_STEALTH, delay, "You feel less stealthy.");
    _decrement_a_duration(DUR_RESIST_FIRE, delay, "Your fire resistance expires.");
    _decrement_a_duration(DUR_RESIST_COLD, delay, "Your cold resistance expires.");
    _decrement_a_duration(DUR_RESIST_POISON, delay, "Your poison resistance expires.");
    _decrement_a_duration(DUR_SLAYING, delay, "You feel less lethal.");

    _decrement_a_duration(DUR_INVIS, delay, "You flicker back into view.",
                          coinflip(), "You flicker for a moment.");

    _decrement_a_duration(DUR_BARGAIN, delay, "You feel less charismatic.");
    _decrement_a_duration(DUR_CONF, delay, "You feel less confused.");
    _decrement_a_duration(DUR_LOWERED_MR, delay, "You feel more resistant to magic.");
    _decrement_a_duration(DUR_SLIMIFY, delay, "You feel less slimy.",
                          coinflip(), "Your slime is starting to congeal.");
    _decrement_a_duration(DUR_MISLED, delay, "Your thoughts are your own once more.");

    if (you.duration[DUR_PARALYSIS] || you.petrified())
    {
        _decrement_a_duration(DUR_PARALYSIS, delay);
        _decrement_a_duration(DUR_PETRIFIED, delay);

        if (!you.duration[DUR_PARALYSIS] && !you.petrified())
        {
            mpr("You can move again.", MSGCH_DURATION);
            you.redraw_evasion = true;
        }
    }

    _decrement_a_duration(DUR_EXHAUSTED, delay, "You feel less fatigued.");

    _decrement_a_duration(DUR_CONFUSING_TOUCH, delay,
                          ((std::string("Your ") + your_hand(true)) +
                          " stop glowing.").c_str());

    _decrement_a_duration(DUR_SURE_BLADE, delay,
                          "The bond with your blade fades away.");

    if (_decrement_a_duration(DUR_MESMERISED, delay,
                              "You break out of your daze.",
                              0, NULL, MSGCH_RECOVERY))
    {
        you.clear_beholders();
    }

    dec_slow_player(delay);
    dec_haste_player(delay);

    if (_decrement_a_duration(DUR_MIGHT, delay,
                              "You feel a little less mighty now."))
    {
        modify_stat(STAT_STRENGTH, -5, true, "might running out");
    }

    if (_decrement_a_duration(DUR_AGILITY, delay,
                              "You feel a little less agile now."))
    {
        modify_stat(STAT_DEXTERITY, -5, true, "agility running out");
    }

    if (_decrement_a_duration(DUR_BRILLIANCE, delay,
                              "You feel a little less clever now."))
    {
        modify_stat(STAT_INTELLIGENCE, -5, true, "brilliance running out");
    }

    if (_decrement_a_duration(DUR_BERSERKER, delay,
                              "You are no longer berserk."))
    {
        //jmf: Guilty for berserking /after/ berserk.
        did_god_conduct(DID_STIMULANTS, 6 + random2(6));

        // Sometimes berserk leaves us physically drained.
        //
        // Chance of passing out:
        //     - mutation gives a large plus in order to try and
        //       avoid the mutation being a "death sentence" to
        //       certain characters.
        //     - knowing the spell gives an advantage just
        //       so that people who have invested 3 spell levels
        //       are better off than the casual potion drinker...
        //       this should make it a bit more interesting for
        //       Crusaders again.
        //     - similarly for the amulet

        if (you.berserk_penalty != NO_BERSERK_PENALTY)
        {
            const int chance =
                10 + player_mutation_level(MUT_BERSERK) * 25
                + (wearing_amulet(AMU_RAGE) ? 10 : 0)
                + (you.has_spell(SPELL_BERSERKER_RAGE) ? 5 : 0);

            // Note the beauty of Trog!  They get an extra save that's at
            // the very least 20% and goes up to 100%.
            if (you.religion == GOD_TROG && x_chance_in_y(you.piety, 150)
                && !player_under_penance())
            {
                mpr("Trog's vigour flows through your veins.");
            }
            else if (one_chance_in(chance))
            {
                mpr("You pass out from exhaustion.", MSGCH_WARN);
                you.increase_duration(DUR_PARALYSIS, roll_dice(1,4));
            }
        }

        if (!you.duration[DUR_PARALYSIS] && !you.petrified())
            mpr("You are exhausted.", MSGCH_WARN);

        // This resets from an actual penalty or from NO_BERSERK_PENALTY.
        you.berserk_penalty = 0;

        int dur = 12 + roll_dice(2, 12);
        // For consistency with slow give exhaustion 2 times the nominal
        // duration.
        you.increase_duration(DUR_EXHAUSTED, dur * 2);

        // Don't trigger too many tutorial messages.
        const bool tut_slow = Tutorial.tutorial_events[TUT_YOU_ENCHANTED];
        Tutorial.tutorial_events[TUT_YOU_ENCHANTED] = false;

        {
            // Don't give duplicate 'You feel yourself slow down' messages.
            no_messages nm;

            // While the amulet of resist slowing does prevent the post-berserk
            // slowing, exhaustion still ends haste.
            if (you.duration[DUR_HASTE] > 0)
            {
                // Silently cancel haste, then slow player.
                you.duration[DUR_HASTE] = 0;
            }
            slow_player(dur);
        }

        make_hungry(700, true);
        you.hunger = std::max(50, you.hunger);

        // 1KB: No berserk healing.
        you.hp = (you.hp + 1) * 2 / 3;
        calc_hp();

        learned_something_new(TUT_POSTBERSERK);
        Tutorial.tutorial_events[TUT_YOU_ENCHANTED] = tut_slow;
    }

    if (you.duration[DUR_CORONA])
    {
        you.duration[DUR_CORONA] -= delay;
        if (you.duration[DUR_CORONA] < 1)
            you.duration[DUR_CORONA] = 0;

        if (!you.duration[DUR_CORONA] && !you.backlit())
            mpr("You are no longer glowing.", MSGCH_DURATION);
    }

    // Leak piety from the piety pool into actual piety.
    // Note that changes of religious status without corresponding actions
    // (killing monsters, offering items, ...) might be confusing for characters
    // of other religions.
    // For now, though, keep information about what happened hidden.
    if (you.piety < MAX_PIETY && you.duration[DUR_PIETY_POOL] > 0
        && one_chance_in(5))
    {
        you.duration[DUR_PIETY_POOL]--;
        gain_piety(1);

#if DEBUG_DIAGNOSTICS || DEBUG_SACRIFICE || DEBUG_PIETY
        mpr("Piety increases by 1 due to piety pool.", MSGCH_DIAGNOSTICS);

        if (you.duration[DUR_PIETY_POOL] == 0)
            mpr("Piety pool is now empty.", MSGCH_DIAGNOSTICS);
#endif
    }

    if (!you.permanent_levitation() && !you.permanent_flight())
    {
        if (_decrement_a_duration(DUR_LEVITATION, delay,
                                  "You float gracefully downwards.",
                                  random2(6),
                                  "You are starting to lose your buoyancy!"))
        {
            burden_change();
            // Landing kills controlled flight.
            you.duration[DUR_CONTROLLED_FLIGHT] = 0;
            // Re-enter the terrain.
            move_player_to_grid(you.pos(), false, true, true);
        }
    }

    if (!you.permanent_flight()
        && _decrement_a_duration(DUR_CONTROLLED_FLIGHT, delay)
        && you.airborne())
    {
            mpr("You lose control over your flight.", MSGCH_DURATION);
    }

    if (you.rotting > 0)
    {
        // XXX: Mummies have an ability (albeit an expensive one) that
        // can fix rotted HPs now... it's probably impossible for them
        // to even start rotting right now, but that could be changed. -- bwr
        // It's not normal biology, so Cheibriados won't help.
        if (you.species == SP_MUMMY)
            you.rotting = 0;
        else if (x_chance_in_y(you.rotting, 20))
        {
            mpr("You feel your flesh rotting away.", MSGCH_WARN);
            ouch(1, NON_MONSTER, KILLED_BY_ROTTING);
            rot_hp(1);
            you.rotting--;
        }
    }

    // ghoul rotting is special, but will deduct from you.rotting
    // if it happens to be positive - because this is placed after
    // the "normal" rotting check, rotting attacks can be somewhat
    // more painful on ghouls - reversing order would make rotting
    // attacks somewhat less painful, but that seems wrong-headed {dlb}:
    if (you.species == SP_GHOUL)
    {
        if (one_chance_in((you.religion == GOD_CHEIBRIADOS && you.piety >
                           piety_breakpoint(0)) ? 600 : 400))
        {
            mpr("You feel your flesh rotting away.", MSGCH_WARN);
            ouch(1, NON_MONSTER, KILLED_BY_ROTTING);
            rot_hp(1);

            if (you.rotting > 0)
                you.rotting--;
        }
    }

    dec_disease_player(delay);

    dec_poison_player();

    if (you.duration[DUR_DEATHS_DOOR])
    {
        if (you.hp > allowed_deaths_door_hp())
        {
            mpr("Your life is in your own hands once again.", MSGCH_DURATION);
            you.increase_duration(DUR_PARALYSIS, 5 + random2(5));
            confuse_player(10 + random2(10));
            you.hp_max--;
            deflate_hp(you.hp_max, false);
            you.duration[DUR_DEATHS_DOOR] = 0;
        }
        else
        {
            _decrement_a_duration(DUR_DEATHS_DOOR, delay,
                                  "Your life is in your own hands again!",
                                  random2(6),
                                  "Your time is quickly running out!");
        }
    }

    if (_decrement_a_duration(DUR_DIVINE_VIGOUR, delay))
        remove_divine_vigour();

    if (_decrement_a_duration(DUR_DIVINE_STAMINA, delay))
        remove_divine_stamina();

    _decrement_a_duration(DUR_REPEL_STAIRS_MOVE, 1);
    _decrement_a_duration(DUR_REPEL_STAIRS_CLIMB, 1);
}

static void _check_banished()
{
    if (you.banished)
    {
        you.banished = false;
        if (you.level_type != LEVEL_ABYSS)
        {
            mpr("You are cast into the Abyss!", MSGCH_BANISHMENT);
            more();
            banished(DNGN_ENTER_ABYSS, you.banished_by);
        }
        you.banished_by.clear();
    }
}

static void _check_shafts()
{
    for (int i = 0; i < MAX_TRAPS; ++i)
    {
        trap_def &trap = env.trap[i];

        if (trap.type != TRAP_SHAFT)
            continue;

        ASSERT(in_bounds(trap.pos));

        handle_items_on_shaft(trap.pos, true);
    }
}

static void _check_sanctuary()
{
    if (env.sanctuary_time <= 0)
        return;

    decrease_sanctuary_radius();
}

static void _regenerate_hp_and_mp(int delay)
{
    // XXX: using an int tmp to fix the fact that hit_points_regeneration
    // is only an unsigned char and is thus likely to overflow. -- bwr
    int tmp = you.hit_points_regeneration;

    if (you.hp < you.hp_max && !you.disease && !you.duration[DUR_DEATHS_DOOR])
    {
        int base_val = player_regen();
        tmp += div_rand_round(base_val * delay, BASELINE_DELAY);
    }

    while (tmp >= 100)
    {
        inc_hp(1, false);
        tmp -= 100;
    }

    // XXX: Don't let DD use guardian spirit for free HP. (due, dpeg)
    if (player_spirit_shield() && you.species == SP_DEEP_DWARF)
        return;

    ASSERT( tmp >= 0 && tmp < 100 );
    you.hit_points_regeneration = static_cast<unsigned char>(tmp);

    // XXX: Doing the same as the above, although overflow isn't an
    // issue with magic point regeneration, yet. -- bwr
    tmp = you.magic_points_regeneration;

    if (you.magic_points < you.max_magic_points)
    {
        int base_val = 7 + you.max_magic_points / 2;
        tmp += div_rand_round(base_val * delay, BASELINE_DELAY);
    }

    while (tmp >= 100)
    {
        inc_mp(1, false);
        tmp -= 100;
    }

    ASSERT( tmp >= 0 && tmp < 100 );
    you.magic_points_regeneration = static_cast<unsigned char>(tmp);
}

void world_reacts()
{
    crawl_state.clear_mon_acting();

    if (!crawl_state.arena)
    {
        you.turn_is_over = true;
        religion_turn_end();
        crawl_state.clear_god_acting();
    }

#ifdef USE_TILE
    if (Tutorial.tutorial_left)
    {
        tiles.clear_text_tags(TAG_TUTORIAL);
        tiles.place_cursor(CURSOR_TUTORIAL, Region::NO_CURSOR);
    }
#endif

    if (you.num_turns != -1)
    {
        if (you.num_turns < LONG_MAX)
            you.num_turns++;
        if (env.turns_on_level < INT_MAX)
            env.turns_on_level++;
        update_turn_count();
    }

    _check_banished();
    _check_shafts();
    _check_sanctuary();

    run_environment_effects();

    if (!you.cannot_act() && !player_mutation_level(MUT_BLURRY_VISION)
        && x_chance_in_y(you.skills[SK_TRAPS_DOORS], 50))
    {
        search_around(false); // Check nonadjacent squares too.
    }

    if (!crawl_state.arena)
        stealth = check_stealth();

#ifdef DEBUG_STEALTH
    // Too annoying for regular diagnostics.
    mprf(MSGCH_DIAGNOSTICS, "stealth: %d", stealth );
#endif

    if (you.attribute[ATTR_NOISES])
        noisy_equipment();

    if (you.attribute[ATTR_SHADOWS])
        shadow_lantern_effect();

    if (you.unrand_reacts != 0)
        unrand_reacts();

    if (!crawl_state.arena && one_chance_in(10))
    {
        // this is instantaneous
        if (player_teleport() > 0 && one_chance_in(100 / player_teleport()))
            you_teleport_now( true );
        else if (you.level_type == LEVEL_ABYSS && one_chance_in(30))
            you_teleport_now( false, true ); // to new area of the Abyss
    }

    if (!crawl_state.arena && env.cgrid(you.pos()) != EMPTY_CLOUD)
        in_a_cloud();

    if (you.level_type == LEVEL_DUNGEON && you.duration[DUR_TELEPATHY])
        detect_creatures( 1 + you.duration[DUR_TELEPATHY] /
                         (2 * BASELINE_DELAY), true );

    _decrement_durations();

    int food_use = player_hunger_rate();
    food_use = div_rand_round(food_use * you.time_taken, BASELINE_DELAY);

    if (food_use > 0 && you.hunger >= 40)
        make_hungry(food_use, true);

    _regenerate_hp_and_mp(you.time_taken);

    // If you're wielding a rod, it'll gradually recharge.
    recharge_rods(you.time_taken, false);

    viewwindow(true);

    maybe_update_stashes();
    handle_monsters();

    _check_banished();

    ASSERT(you.time_taken >= 0);
    // Make sure we don't overflow.
    ASSERT(DBL_MAX - you.elapsed_time > you.time_taken);

    you.elapsed_time += you.time_taken;

    handle_time();
    manage_clouds();

    if (you.duration[DUR_FIRE_SHIELD] > 0)
        manage_fire_shield(you.time_taken);

    // Food death check.
    if (you.is_undead != US_UNDEAD && you.hunger <= 500)
    {
        if (!you.cannot_act() && one_chance_in(40))
        {
            mpr("You lose consciousness!", MSGCH_FOOD);
            stop_running();

            you.increase_duration(DUR_PARALYSIS, 5 + random2(8), 13);
            if (you.religion == GOD_XOM)
                xom_is_stimulated(get_tension() > 0 ? 255 : 128);
        }

        if (you.hunger <= 100)
        {
            mpr("You have starved to death.", MSGCH_FOOD);
            ouch(INSTANT_DEATH, NON_MONSTER, KILLED_BY_STARVATION);
        }
    }

    viewwindow(false);

    if (you.cannot_act() && any_messages()
        && crawl_state.repeat_cmd != CMD_WIZARD)
    {
        more();
    }

#if defined(DEBUG_TENSION) || defined(DEBUG_RELIGION)
    if (you.religion != GOD_NO_GOD)
        mprf(MSGCH_DIAGNOSTICS, "TENSION = %d", get_tension());
#endif
}

#ifdef DGL_SIMPLE_MESSAGING

static struct stat mfilestat;

static void _show_message_line(std::string line)
{
    const std::string::size_type sender_pos = line.find(":");
    if (sender_pos == std::string::npos)
        mpr(line.c_str());
    else
    {
        std::string sender = line.substr(0, sender_pos);
        line = line.substr(sender_pos + 1);
        trim_string(line);
        formatted_string fs;
        fs.textcolor(WHITE);
        fs.cprintf("%s: ", sender.c_str());
        fs.textcolor(LIGHTGREY);
        fs.cprintf("%s", line.c_str());
        formatted_mpr(fs, MSGCH_PLAIN, 0);
        take_note(Note( NOTE_MESSAGE, MSGCH_PLAIN, 0,
                        (sender + ": " + line).c_str() ));
    }
}

static void _kill_messaging(FILE *mf)
{
    if (mf)
        fclose(mf);
    SysEnv.have_messages = false;
    Options.messaging = false;
}

static void _read_each_message()
{
    bool say_got_msg = true;
    FILE *mf = fopen(SysEnv.messagefile.c_str(), "r+");
    if (!mf)
    {
        mprf(MSGCH_ERROR, "Couldn't read %s: %s", SysEnv.messagefile.c_str(),
             strerror(errno));
        _kill_messaging(mf);
        return;
    }

    // Read messages, code borrowed from the SIMPLEMAIL patch.
    char line[120];

    if (!lock_file_handle(mf, F_RDLCK))
    {
        mprf(MSGCH_ERROR, "Failed to lock %s: %s", SysEnv.messagefile.c_str(),
             strerror(errno));
        _kill_messaging(mf);
        return;
    }

    while (fgets(line, sizeof line, mf))
    {
        unlock_file_handle(mf);

        const int len = strlen(line);
        if (len)
        {
            if (line[len - 1] == '\n')
                line[len - 1] = 0;

            if (say_got_msg)
            {
                mprf(MSGCH_PROMPT, "Your messages:");
                say_got_msg = false;
            }

            _show_message_line(line);
        }

        if (!lock_file_handle(mf, F_RDLCK))
        {
            mprf(MSGCH_ERROR, "Failed to lock %s: %s",
                 SysEnv.messagefile.c_str(),
                 strerror(errno));
            _kill_messaging(mf);
            return;
        }
    }
    if (!lock_file_handle(mf, F_WRLCK))
    {
        mprf(MSGCH_ERROR, "Unable to write lock %s: %s",
             SysEnv.messagefile.c_str(),
             strerror(errno));
    }
    if (!ftruncate(fileno(mf), 0))
        mfilestat.st_mtime = 0;
    unlock_file_handle(mf);
    fclose(mf);

    SysEnv.have_messages = false;
}

static void _read_messages()
{
    _read_each_message();
    update_message_status();
}

static void _announce_messages()
{
    // XXX: We could do a NetHack-like mail daemon here at some point.
    mprf("Beep! Your pager goes off! Use _ to check your messages.");
}

static void _check_messages()
{
    if (!Options.messaging
        || SysEnv.have_messages
        || SysEnv.messagefile.empty()
        || kbhit()
        || (SysEnv.message_check_tick++ % DGL_MESSAGE_CHECK_INTERVAL))
    {
        return;
    }

    const bool had_messages = SysEnv.have_messages;
    struct stat st;
    if (stat(SysEnv.messagefile.c_str(), &st))
    {
        mfilestat.st_mtime = 0;
        return;
    }

    if (st.st_mtime > mfilestat.st_mtime)
    {
        if (st.st_size)
            SysEnv.have_messages = true;
        mfilestat.st_mtime = st.st_mtime;
    }

    if (SysEnv.have_messages && !had_messages)
    {
        _announce_messages();
        update_message_status();
        // Recenter the cursor on the player.
        _center_cursor();
    }
}
#endif

static command_type _get_next_cmd()
{
#ifdef DGL_SIMPLE_MESSAGING
    _check_messages();
#endif

#if DEBUG_DIAGNOSTICS
    // Save hunger at start of round for use with hunger "delta-meter"
    // in output.cc.
    you.old_hunger = you.hunger;
#endif

#if DEBUG_ITEM_SCAN
    debug_item_scan();
#endif
#if DEBUG_MONS_SCAN
    debug_mons_scan();
#endif

    const time_t before = time(NULL);
    keycode_type keyin = _get_next_keycode();

    const time_t after = time(NULL);

    // Clamp idle time so that play time is more meaningful.
    if (after - before > IDLE_TIME_CLAMP)
    {
        you.real_time  += int(before - you.start_time) + IDLE_TIME_CLAMP;
        you.start_time  = after;
    }

    if (is_userfunction(keyin))
    {
        run_macro(get_userfunction(keyin).c_str());
        return (CMD_NEXT_CMD);
    }

    return _keycode_to_command(keyin);
}

// We handle the synthetic keys, key_to_command() handles the
// real ones.
static command_type _keycode_to_command( keycode_type key )
{
    switch ( key )
    {
#ifdef USE_TILE
    case CK_MOUSE_CMD:    return CMD_NEXT_CMD;
#endif

    case KEY_MACRO_DISABLE_MORE: return CMD_DISABLE_MORE;
    case KEY_MACRO_ENABLE_MORE:  return CMD_ENABLE_MORE;
    case KEY_REPEAT_KEYS:        return CMD_REPEAT_KEYS;

    default:
        return key_to_command(key, KMC_DEFAULT);
    }
}

static keycode_type _get_next_keycode()
{
    keycode_type keyin;

    flush_input_buffer( FLUSH_BEFORE_COMMAND );

    mouse_control mc(MOUSE_MODE_COMMAND);
    keyin = unmangle_direction_keys(getch_with_command_macros());

    if (!is_synthetic_key(keyin))
        mesclr();

    return (keyin);
}

// Check squares adjacent to player for given feature and return how
// many there are.  If there's only one, return the dx and dy.
static int _check_adjacent(dungeon_feature_type feat, coord_def& delta)
{
    int num = 0;

    for (adjacent_iterator ai(you.pos(), false); ai; ++ai)
    {
        if (grd(*ai) == feat)
        {
            num++;
            delta = *ai - you.pos();
        }
    }

    return num;
}

// Handles some aspects of untrapping. Returns false if the target is a
// closed door that will need to be opened.
static bool _untrap_target(const coord_def move, bool check_confused)
{
    const coord_def target = you.pos() + move;
    monsters* mon = monster_at(target);
    if (mon && player_can_hit_monster(mon))
    {
        if (mon->caught() && mon->friendly()
            && player_can_open_doors() && !you.confused())
        {
            const std::string prompt =
                make_stringf("Do you want to try to take the net off %s?",
                             mon->name(DESC_NOCAP_THE).c_str());

            if (yesno(prompt.c_str(), true, 'n'))
            {
                remove_net_from(mon);
                return (true);
            }
        }

        you.turn_is_over = true;
        you_attack(mon->mindex(), true);

        if (you.berserk_penalty != NO_BERSERK_PENALTY)
            you.berserk_penalty = 0;

        return (true);
    }

    if (find_trap(target) && grd(target) != DNGN_UNDISCOVERED_TRAP)
    {
        if (!you.confused())
        {
            if (!player_can_open_doors())
            {
                mpr("You can't disarm traps in your present form.");
                return (true);
            }

            const int cloud = env.cgrid(target);
            if (cloud != EMPTY_CLOUD
                && is_damaging_cloud(env.cloud[ cloud ].type, true))
            {
                mpr("You can't get to that trap right now.");
                return (true);
            }
        }

        // If you're confused, you may attempt it and stumble into the trap.
        disarm_trap(target);
        return (true);
    }

    const dungeon_feature_type feat = grd(target);
    if (!feat_is_closed_door(feat) || you.confused())
    {
        switch (feat)
        {
        case DNGN_OPEN_DOOR:
            _close_door(move); // for convenience
            return (true);
        default:
        {
            bool do_msg = true;

            // Press trigger/switch/button in wall.
            if (feat_is_solid(feat))
            {
                dgn_event event(DET_WALL_HIT, target);
                event.arg1  = NON_MONSTER;

                // Listener can veto the event to prevent the "You swing at
                // nothing" message.
                do_msg =
                    dungeon_events.fire_vetoable_position_event(event,
                                                                target);
            }
            if (do_msg)
                mpr("You swing at nothing.");
            make_hungry(3, true);
            you.turn_is_over = true;
            return (true);
        }
        }
    }

    // Else it's a closed door and needs further handling.
    return (false);
}

// Opens doors and may also handle untrapping/attacking, etc.
// If either move_x or move_y are non-zero, the pair carries a specific
// direction for the door to be opened (eg if you type ctrl + dir).
static void _open_door(coord_def move, bool check_confused)
{
    ASSERT(!crawl_state.arena && !crawl_state.arena_suspended);

    if (you.attribute[ATTR_HELD])
    {
        free_self_from_net();
        you.turn_is_over = true;
        return;
    }

    // The player used Ctrl + dir or a variant thereof.
    if (!move.origin())
    {
        if (check_confused && you.confused() && !one_chance_in(3))
        {
            do
                move = coord_def(random2(3) - 1, random2(3) - 1);
            while (move.origin());
        }
        if (_untrap_target(move, check_confused))
            return;
    }

    // If we get here, the player either hasn't picked a direction yet,
    // or the chosen direction actually contains a closed door.
    if (!player_can_open_doors())
    {
        mpr("You can't open doors in your present form.");
        return;
    }

    dist door_move;

    // The player hasn't picked a direction yet.
    if (move.origin())
    {
        const int num = _check_adjacent(DNGN_CLOSED_DOOR, move)
                        + _check_adjacent(DNGN_DETECTED_SECRET_DOOR, move);

        if (num == 0)
        {
            mpr("There's nothing to open nearby.");
            return;
        }

        // If there's only one door to open, don't ask.
        if (num == 1)
            door_move.delta = move;
        else
        {
            mpr("Which direction? ", MSGCH_PROMPT);
            direction(door_move, DIR_DIR);

            if (!door_move.isValid)
                return;
        }
    }
    else
        door_move.delta = move;

    if (check_confused && you.confused() && !one_chance_in(3))
    {
        do
            door_move.delta = coord_def(random2(3) - 1, random2(3) - 1);
        while (door_move.delta.origin());
    }

    // We got a valid direction.
    const coord_def doorpos = you.pos() + door_move.delta;
    const dungeon_feature_type feat = (in_bounds(doorpos) ? grd(doorpos)
                                                          : DNGN_UNSEEN);
    std::string door_already_open = "";
    if (in_bounds(doorpos))
        door_already_open = env.markers.property_at(doorpos, MAT_ANY,
                                "door_verb_already_open");

    if (!feat_is_closed_door(feat))
    {
        if (you.confused())
        {
            mpr("You swing at nothing.");
            make_hungry(3, true);
            you.turn_is_over = true;
            return;
        }
        switch (feat)
        {
        // This doesn't ever sem to be triggered.
        case DNGN_OPEN_DOOR:
            if (!door_already_open.empty())
                mpr(door_already_open.c_str());
            else
                mpr("It's already open!");
            break;
        default:
            mpr("There isn't anything that you can open there!");
            break;
        }
        // Don't lose a turn.
        return;
    }

    // Finally, open the closed door!
    std::set<coord_def> all_door;
    find_connected_range(doorpos, DNGN_CLOSED_DOOR, DNGN_SECRET_DOOR, all_door);
    const char *adj, *noun;
    get_door_description(all_door.size(), &adj, &noun);

    const std::string door_desc_adj  =
        env.markers.property_at(doorpos, MAT_ANY,
                                "door_description_adjective");
    const std::string door_desc_noun =
        env.markers.property_at(doorpos, MAT_ANY,
                                "door_description_noun");
    if (!door_desc_adj.empty())
        adj = door_desc_adj.c_str();
    if (!door_desc_noun.empty())
        noun = door_desc_noun.c_str();

    if (!(check_confused && you.confused()))
    {
        std::string door_open_prompt =
            env.markers.property_at(doorpos, MAT_ANY, "door_open_prompt");

        bool ignore_exclude = false;

        if (!door_open_prompt.empty())
        {
            door_open_prompt += " (y/N)";
            if (!yesno(door_open_prompt.c_str(), true, 'n', true, false))
            {
                if (is_exclude_root(doorpos))
                    canned_msg(MSG_OK);
                else
                {
                    if (yesno("Put travel exclusion on door? (Y/n)",
                              true, 'y'))
                    {
                        // Zero radius exclusion right on top of door.
                        set_exclude(doorpos, 0);
                    }
                }
                interrupt_activity(AI_FORCE_INTERRUPT);
                return;
            }
            ignore_exclude = true;
        }

        if (!ignore_exclude && is_exclude_root(doorpos))
        {
            std::string prompt =
                make_stringf("This %s%s is marked as excluded! Open it "
                             "anyway?", adj, noun);

            if (!yesno(prompt.c_str(), true, 'n', true, false))
            {
                canned_msg(MSG_OK);
                interrupt_activity(AI_FORCE_INTERRUPT);
                return;
            }
        }
    }

    int skill = you.dex
                + (you.skills[SK_TRAPS_DOORS] + you.skills[SK_STEALTH]) / 2;

    std::string berserk_open = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_berserk_verb_open");
    std::string berserk_adjective = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_berserk_adjective");
    std::string door_open_creak = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_noisy_verb_open");
    std::string door_airborne = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_airborne_verb_open");
    std::string door_open_verb = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_verb_open");

    if (you.berserk())
    {
        // XXX: Better flavour for larger doors?
        if (silenced(you.pos()))
        {
            if (!berserk_open.empty())
            {
                berserk_open += ".";
                mprf(berserk_open.c_str(), adj, noun);
            }
            else
                mprf("The %s%s flies open!", adj, noun);
        }
        else
        {
            if (!berserk_open.empty())
            {
                if (!berserk_adjective.empty())
                    berserk_open += " " + berserk_adjective;
                else
                    berserk_open += ".";
                mprf(MSGCH_SOUND, berserk_open.c_str(), adj, noun);
            }
            else
                mprf(MSGCH_SOUND, "The %s%s flies open with a bang!", adj, noun);
            noisy(15, you.pos());
        }
    }
    else if (one_chance_in(skill) && !silenced(you.pos()))
    {
        if (!door_open_creak.empty())
            mprf(MSGCH_SOUND, door_open_creak.c_str(), adj, noun);
        else
            mprf(MSGCH_SOUND, "As you open the %s%s, it creaks loudly!",
                 adj, noun);
        noisy(10, you.pos());
    }
    else
    {
        const char* verb;
        if (you.airborne())
        {
            if (!door_airborne.empty())
                verb = door_airborne.c_str();
            else
                verb = "You reach down and open the %s%s.";
        }
        else
        {
            if (!door_open_verb.empty())
               verb = door_open_verb.c_str();
            else
               verb = "You open the %s%s.";
        }

        mprf(verb, adj, noun);
    }

    bool seen_secret = false;
    std::vector<coord_def> excludes;
    for (std::set<coord_def>::iterator i = all_door.begin();
         i != all_door.end(); ++i)
    {
        const coord_def& dc = *i;
        // Even if some of the door is out of LOS, we want the entire
        // door to be updated.  Hitting this case requires a really big
        // door!
        if (is_terrain_seen(dc))
        {
            set_map_knowledge_obj(dc, DNGN_OPEN_DOOR);
#ifdef USE_TILE
            env.tile_bk_bg(dc) = TILE_DNGN_OPEN_DOOR;
#endif
            if (!seen_secret && grd(dc) == DNGN_SECRET_DOOR)
            {
                seen_secret = true;
                dungeon_feature_type secret
                    = grid_secret_door_appearance(dc);
                mprf("That %s was a secret door!",
                     feature_description(secret, NUM_TRAPS, false,
                                         DESC_PLAIN, false).c_str());
            }
        }
        grd(dc) = DNGN_OPEN_DOOR;
        if (is_excluded(dc))
            excludes.push_back(dc);
    }

    update_exclusion_los(excludes);

    you.turn_is_over = true;
}

static void _close_door(coord_def move)
{
    if (!player_can_open_doors())
    {
        mpr("You can't close doors in your present form.");
        return;
    }

    if (you.attribute[ATTR_HELD])
    {
        mpr("You can't close doors while held in a net.");
        return;
    }

    dist door_move;

    // The player hasn't yet told us a direction.
    if (move.origin())
    {
        // If there's only one door to close, don't ask.
        int num = _check_adjacent(DNGN_OPEN_DOOR, move);
        if (num == 0)
        {
            mpr("There's nothing to close nearby.");
            return;
        }
        else if (num == 1)
            door_move.delta = move;
        else
        {
            mpr("Which direction? ", MSGCH_PROMPT);
            direction(door_move, DIR_DIR);

            if (!door_move.isValid)
                return;
        }
    }
    else
        door_move.delta = move;

    if (you.confused() && !one_chance_in(3))
    {
        do
            door_move.delta = coord_def(random2(3) - 1, random2(3) - 1);
        while (door_move.delta.origin());
    }

    if (door_move.delta.origin())
    {
        mpr("You can't close doors on yourself!");
        return;
    }

    const coord_def doorpos = you.pos() + door_move.delta;
    const dungeon_feature_type feat = (in_bounds(doorpos) ? grd(doorpos)
                                                          : DNGN_UNSEEN);

    std::string berserk_close = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_berserk_verb_close");
    std::string berserk_adjective = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_berserk_adjective");
    std::string door_close_creak = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_noisy_verb_close");
    std::string door_airborne = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_airborne_verb_close");
    std::string door_close_verb = env.markers.property_at(doorpos, MAT_ANY,
                                        "door_verb_close");

    if (feat == DNGN_OPEN_DOOR)
    {
        std::set<coord_def> all_door;
        find_connected_identical(doorpos, grd(doorpos), all_door);
        const char *adj, *noun;
        get_door_description(all_door.size(), &adj, &noun);
        const std::string waynoun_str = make_stringf("%sway", noun);
        const char *waynoun = waynoun_str.c_str();

        const std::string door_desc_adj  =
            env.markers.property_at(doorpos, MAT_ANY,
                                    "door_description_adjective");
        const std::string door_desc_noun =
            env.markers.property_at(doorpos, MAT_ANY,
                                    "door_description_noun");
        if (!door_desc_adj.empty())
            adj = door_desc_adj.c_str();
        if (!door_desc_noun.empty())
        {
            noun = door_desc_noun.c_str();
            waynoun = noun;
        }

        for (std::set<coord_def>::const_iterator i = all_door.begin();
             i != all_door.end(); ++i)
        {
            const coord_def& dc = *i;
            if (monsters* mon = monster_at(dc))
            {
                // Need to make sure that turn_is_over is set if
                // creature is invisible.
                if (!you.can_see(mon))
                {
                    mprf("Something is blocking the %s!", waynoun);
                    you.turn_is_over = true;
                }
                else
                    mprf("There's a creature in the %s!", waynoun);
                return;
            }

            if (igrd(dc) != NON_ITEM)
            {
                mprf("There's something blocking the %s.", waynoun);
                return;
            }

            if (you.pos() == dc)
            {
                mprf("There's a thick-headed creature in the %s!", waynoun);
                return;
            }
        }

        int skill = you.dex
                    + (you.skills[SK_TRAPS_DOORS] + you.skills[SK_STEALTH]) / 2;

        if (you.berserk())
        {
            if (silenced(you.pos()))
            {
                if (!berserk_close.empty())
                {
                    berserk_close += ".";
                    mprf(berserk_close.c_str(), adj, noun);
                }
                else
                    mprf("You slam the %s%s shut!", adj, noun);
            }
            else
            {
                if (!berserk_close.empty())
                {
                    if (!berserk_adjective.empty())
                        berserk_close += " " + berserk_adjective;
                    else
                        berserk_close += ".";
                    mprf(MSGCH_SOUND, berserk_close.c_str(), adj, noun);
                }
                else
                    mprf(MSGCH_SOUND, "You slam the %s%s shut with a bang!", adj, noun);
                noisy(15, you.pos());
            }
        }
        else if (one_chance_in(skill) && !silenced(you.pos()))
        {
            if (!door_close_creak.empty())
                mprf(MSGCH_SOUND, door_close_creak.c_str(), adj, noun);
            else
                mprf(MSGCH_SOUND, "As you close the %s%s, it creaks loudly!",
                     adj, noun);
            noisy(10, you.pos());
        }
        else
        {
            const char* verb;
            if (you.airborne())
            {
                if (!door_airborne.empty())
                    verb = door_airborne.c_str();
                else
                    verb = "You reach down and close the %s%s.";
            }
            else
            {
                if (!door_close_verb.empty())
                   verb = door_close_verb.c_str();
                else
                    verb = "You close the %s%s.";
            }

            mprf(verb, adj, noun);
        }

        std::vector<coord_def> excludes;
        for (std::set<coord_def>::const_iterator i = all_door.begin();
             i != all_door.end(); ++i)
        {
            const coord_def& dc = *i;
            // Once opened, formerly secret doors become normal doors.
            grd(dc) = DNGN_CLOSED_DOOR;

            // Even if some of the door is out of LOS once it's closed
            // (or even if some of it is out of LOS when it's open), we
            // want the entire door to be updated.
            if (is_terrain_seen(dc))
            {
                set_map_knowledge_obj(dc, DNGN_CLOSED_DOOR);
#ifdef USE_TILE
                env.tile_bk_bg(dc) = TILE_DNGN_CLOSED_DOOR;
#endif
            }
            if (is_excluded(dc))
                excludes.push_back(dc);
        }

        update_exclusion_los(excludes);

        you.turn_is_over = true;
    }
    else if (you.confused())
        _open_door(door_move.delta);
    else
    {
        switch (feat)
        {
        case DNGN_CLOSED_DOOR:
        case DNGN_DETECTED_SECRET_DOOR:
            mpr("It's already closed!");
            break;
        default:
            mpr("There isn't anything that you can close there!");
            break;
        }
    }
}

// Initialise a whole lot of stuff...
// Returns true if a new character.
static bool _initialise(void)
{
    Options.fixup_options();

    // Read the options the player used last time they created a new
    // character.
    read_startup_prefs();

    you.symbol = '@';
    you.colour = LIGHTGREY;

    seed_rng();
    get_typeid_array().init(ID_UNKNOWN_TYPE);
    init_char_table(Options.char_set);
    init_show_table();
    init_monster_symbols();
    init_spell_descs();        // This needs to be way up top. {dlb}
    init_mon_name_cache();
    init_mons_spells();

    // init_item_name_cache() needs to be redone after init_char_table()
    // and init_show_table() have been called, so that the glyphs will
    // be set to use with item_names_by_glyph_cache.
    init_item_name_cache();

    msg::initialise_mpr_streams();

    // Init item array.
    for (int i = 0; i < MAX_ITEMS; ++i)
        init_item(i);

    // Empty messaging string.
    info[0] = 0;

    for (int i = 0; i < MAX_MONSTERS; ++i)
        menv[i].reset();

    igrd.init(NON_ITEM);
    mgrd.init(NON_MONSTER);
    env.map_knowledge.init(map_cell());
    env.pgrid.init(0);

    you.unique_creatures.init(false);
    you.unique_items.init(UNIQ_NOT_EXISTS);

    // Set up the Lua interpreter for the dungeon builder.
    init_dungeon_lua();

#ifdef USE_TILE
    // Draw the splash screen before the database gets initialised as that
    // may take awhile and it's better if the player can look at a pretty
    // screen while this happens.
    if (!crawl_state.map_stat_gen && !crawl_state.test
        && Options.tile_title_screen)
    {
        tiles.draw_title();
    }
#endif

    // Initialise internal databases.
    databaseSystemInit();

    init_feat_desc_cache();
    init_spell_name_cache();
    init_spell_rarities();

    // Read special levels and vaults.
    read_maps();

    if (crawl_state.build_db)
        end(0);

    cio_init();

    // System initialisation stuff.
    textbackground(0);

    clrscr();

#ifdef DEBUG_DIAGNOSTICS
    if (crawl_state.map_stat_gen)
    {
        generate_map_stats();
        end(0, false);
    }
#endif

    if (crawl_state.test)
    {
#if DEBUG_TESTS && !DEBUG
#error "DEBUG must be defined if DEBUG_TESTS is defined"
#endif

#if DEBUG_DIAGNOSTICS || DEBUG_TESTS
#ifdef USE_TILE
        init_player_doll();
        tiles.initialise_items();
#endif
        Options.show_more_prompt = false;
        crawl_tests::run_tests(true);
        // Superfluous, just to make it clear that this is the end of
        // the line.
        end(0, false);
#else
        end(1, false, "Non-debug Crawl cannot run tests. "
            "Please use a debug build (defined FULLDEBUG, DEBUG_DIAGNOSTIC "
            "or DEBUG_TESTS)");
#endif
    }


    if (crawl_state.arena)
    {
        run_map_preludes();
        initialise_item_descriptions();
#ifdef USE_TILE
        tiles.initialise_items();
#endif

        run_arena();
        end(0, false);
    }

    // Sets up a new game.
    const bool newc = new_game();
    if (!newc)
        restore_game();

    // Fix the mutation definitions for the species we're playing.
    fixup_mutations();

    // Load macros
    macro_init();

    crawl_state.need_save = true;

    calc_hp();
    calc_mp();

    run_map_preludes();

    if (newc && you.char_direction == GDT_GAME_START)
    {
        // Chaos Knights of Lugonu start out in the Abyss.
        you.level_type  = LEVEL_ABYSS;
        you.entry_cause = EC_UNKNOWN;
    }

    load(you.entering_level ? you.transit_stair : DNGN_STONE_STAIRS_DOWN_I,
         you.entering_level ? LOAD_ENTER_LEVEL :
         newc               ? LOAD_START_GAME : LOAD_RESTART_GAME,
         NUM_LEVEL_AREA_TYPES, -1, you.where_are_you);

    // Make sure monsters have a valid LOS before the first turn starts.
    // This prevents mesmerization from being broken when a game is
    // restored.
    for (monster_iterator mi; mi; ++mi)
        mi->update_los();

    if (newc && you.char_direction == GDT_GAME_START)
    {
        // Randomise colours properly for the Abyss.
        init_pandemonium();
    }

#if DEBUG_DIAGNOSTICS
    // Debug compiles display a lot of "hidden" information, so we auto-wiz.
    you.wizard = true;
#endif

    init_properties();
    burden_change();
    make_hungry(0, true);

    you.redraw_strength     = true;
    you.redraw_intelligence = true;
    you.redraw_dexterity    = true;
    you.redraw_armour_class = true;
    you.redraw_evasion      = true;
    you.redraw_experience   = true;
    you.redraw_quiver       = true;
    you.wield_change        = true;

    // Start timer on session.
    you.start_time = time( NULL );

#ifdef CLUA_BINDINGS
    clua.runhook("chk_startgame", "b", newc);
    std::string yname = you.your_name; // XXX: what's this for?
    read_init_file(true);
    Options.fixup_options();
    you.your_name = yname;

    // In case Lua changed the character set.
    init_char_table(Options.char_set);
    init_show_table();
    init_monster_symbols();
#endif

#ifdef USE_TILE
    // Override inventory weights options for tiled menus.
    if (Options.tile_menu_icons && Options.show_inventory_weights)
        Options.show_inventory_weights = false;

    init_player_doll();

    tiles.resize();
#endif

    draw_border();
    new_level();
    update_turn_count();

    // Reset lava/water nearness check to unknown, so it'll be
    // recalculated for the next monster that tries to reach us.
    you.lava_in_sight = you.water_in_sight = -1;

    // Set vision radius to player's current vision.
    set_los_radius(you.current_vision);
    init_exclusion_los();

    trackers_init_new_level(false);

    if (newc) // start a new game
    {
        you.friendly_pickup = Options.default_friendly_pickup;

        // Mark items in inventory as of unknown origin.
        origin_set_inventory(origin_set_unknown);

        // For a new game, wipe out monsters in LOS, and
        // for new tutorial games also the items.
        zap_los_monsters(Tutorial.tutorial_events[TUT_SEEN_FIRST_OBJECT]);

        // For a newly started tutorial, turn secret doors into normal ones.
        if (Tutorial.tutorial_left)
            tutorial_zap_secret_doors();
    }

#ifdef USE_TILE
    tiles.initialise_items();
    // Must re-run as the feature table wasn't initialised yet.
    TileNewLevel(newc);
#endif

    maybe_update_stashes();

    // This just puts the view up for the first turn.
    viewwindow(false);

    activate_notes(true);

    add_key_recorder(&repeat_again_rec);

    return (newc);
}

// An attempt to tone down berserk a little bit. -- bwross
//
// This function does the accounting for not attacking while berserk
// This gives a triangular number function for the additional penalty
// Turn:    1  2  3   4   5   6   7   8
// Penalty: 1  3  6  10  15  21  28  36
//
// Total penalty (including the standard one during upkeep is:
//          2  5  9  14  20  27  35  44
//
static void _do_berserk_no_combat_penalty(void)
{
    // Butchering/eating a corpse will maintain a blood rage.
    const int delay = current_delay_action();
    if (delay == DELAY_BUTCHER || delay == DELAY_EAT)
        return;

    if (you.berserk_penalty == NO_BERSERK_PENALTY)
        return;

    if (you.berserk())
    {
        you.berserk_penalty++;

        switch (you.berserk_penalty)
        {
        case 2:
            mpr("You feel a strong urge to attack something.", MSGCH_DURATION);
            break;
        case 4:
            mpr("You feel your anger subside.", MSGCH_DURATION);
            break;
        case 6:
            mpr("Your blood rage is quickly leaving you.", MSGCH_DURATION);
            break;
        }

        // I do these three separately, because the might and
        // haste counters can be different.
        int berserk_delay_penalty = you.berserk_penalty * BASELINE_DELAY;
        you.duration[DUR_BERSERKER] -= berserk_delay_penalty;
        if (you.duration[DUR_BERSERKER] < 1)
            you.duration[DUR_BERSERKER] = 1;

        you.duration[DUR_MIGHT] -= berserk_delay_penalty;
        if (you.duration[DUR_MIGHT] < 1)
            you.duration[DUR_MIGHT] = 1;

        you.duration[DUR_HASTE] -= berserk_delay_penalty;
        if (you.duration[DUR_HASTE] < 1)
            you.duration[DUR_HASTE] = 1;
    }
    return;
}                               // end do_berserk_no_combat_penalty()


// Called when the player moves by walking/running. Also calls attack
// function etc when necessary.
static void _move_player(int move_x, int move_y)
{
    _move_player( coord_def(move_x, move_y) );
}

static void _move_player(coord_def move)
{
    ASSERT(!crawl_state.arena && !crawl_state.arena_suspended);

    bool attacking = false;
    bool moving = true;         // used to prevent eventual movement (swap)
    bool swap = false;

    if (you.attribute[ATTR_HELD])
    {
        free_self_from_net();
        you.turn_is_over = true;
        return;
    }

    // When confused, sometimes make a random move
    if (you.confused())
    {
        dungeon_feature_type dangerous = DNGN_FLOOR;
        for (adjacent_iterator ai(you.pos(), false); ai; ++ai)
        {
            if (is_feat_dangerous(grd(*ai))
                && (dangerous == DNGN_FLOOR || grd(*ai) == DNGN_LAVA))
            {
                dangerous = grd(*ai);
            }
        }
        if (dangerous != DNGN_FLOOR)
        {
            std::string prompt = "Are you sure you want to move while confused "
                                 "and next to ";
                        prompt += (dangerous == DNGN_LAVA ? "lava"
                                                          : "deep water");
                        prompt += "? ";

            if (!yesno(prompt.c_str(), false, 'n'))
            {
                canned_msg(MSG_OK);
                return;
            }
        }

        if (!one_chance_in(3))
        {
            move.x = random2(3) - 1;
            move.y = random2(3) - 1;
            you.reset_prev_move();
        }

        const coord_def& new_targ = you.pos() + move;
        if (!in_bounds(new_targ) || !you.can_pass_through(new_targ))
        {
            you.turn_is_over = true;
            mpr("Ouch!");
            apply_berserk_penalty = true;
            crawl_state.cancel_cmd_repeat();

            return;
        }
    }

    const coord_def& targ = you.pos() + move;

    // You can't walk out of bounds!
    if (!in_bounds(targ))
        return;

    const dungeon_feature_type targ_grid  =  grd(targ);

          monsters*      targ_monst = monster_at(targ);
          if (fedhas_passthrough(targ_monst))
          {
              // Moving on a plant takes 1.5 x normal move delay. We
              // will print a message about it but only when moving
              // from open space->plant (hopefully this will cut down
              // on the message spam).
              you.time_taken = div_rand_round(you.time_taken * 3, 2);

              monsters * current = monster_at(you.pos());
              if(!current || !fedhas_passthrough(current))
              {
                  // Probably need better messages. -cao
                  if(mons_genus(targ_monst->type) == MONS_FUNGUS)
                  {
                      mprf("You walk carefully through the fungus.");
                  }
                  else
                      mprf("You walk carefully through the plants.");
              }
              targ_monst = NULL;
          }

    const bool           targ_pass  = you.can_pass_through(targ);

    // You can swap places with a friendly or good neutral monster if
    // you're not confused, or if both of you are inside a sanctuary.
    const bool can_swap_places = targ_monst
                                 && !mons_is_stationary(targ_monst)
                                 && (targ_monst->wont_attack()
                                       && !you.confused()
                                     || is_sanctuary(you.pos())
                                        && is_sanctuary(targ));

    // You cannot move away from a mermaid but you CAN fight monsters on
    // neighbouring squares.
    monsters *beholder = NULL;
    if (!you.confused())
        beholder = you.get_beholder(targ);

    if (you.running.check_stop_running())
    {
        // [ds] Do we need this? Shouldn't it be false to start with?
        you.turn_is_over = false;
        return;
    }

    coord_def mon_swap_dest;

    if (targ_monst && !targ_monst->submerged())
    {
        if (can_swap_places && !beholder)
        {
            if (swap_check(targ_monst, mon_swap_dest))
                swap = true;
            else
                moving = false;
        }
        else if (!can_swap_places) // attack!
        {
            // XXX: Moving into a normal wall does nothing and uses no
            // turns or energy, but moving into a wall which contains
            // an invisible monster attacks the monster, thus allowing
            // the player to figure out which adjacent wall an invis
            // monster is in "for free".
            you.turn_is_over = true;
            you_attack(targ_monst->mindex(), true);

            // We don't want to create a penalty if there isn't
            // supposed to be one.
            if (you.berserk_penalty != NO_BERSERK_PENALTY)
                you.berserk_penalty = 0;

            attacking = true;
        }
    }

    if (!attacking && targ_pass && moving && !beholder)
    {
        you.time_taken *= player_movement_speed();
        you.time_taken /= 10;
        if (!move_player_to_grid(targ, true, false, false, swap))
            return;

        if (swap)
            swap_places(targ_monst, mon_swap_dest);

        you.prev_move = move;
        move.reset();
        you.turn_is_over = true;
        request_autopickup();
    }

    // BCR - Easy doors single move
    if (Options.easy_open && !attacking && feat_is_closed_door(targ_grid))
    {
        _open_door(move.x, move.y, false);
        you.prev_move = move;
    }
    else if (!targ_pass && !attacking)
    {
        if (grd(targ) == DNGN_OPEN_SEA)
            mpr("You can't go out to sea!");

        stop_running();
        move.reset();
        you.turn_is_over = false;
        crawl_state.cancel_cmd_repeat();
        return;
    }
    else if (beholder && !attacking)
    {
        mprf("You cannot move away from %s!",
            beholder->name(DESC_NOCAP_THE, true).c_str());
        return;
    }

    if (you.running == RMODE_START)
        you.running = RMODE_CONTINUE;

    if (you.level_type == LEVEL_ABYSS
        && (you.pos().x <= 15 || you.pos().x >= (GXM - 16)
            || you.pos().y <= 15 || you.pos().y >= (GYM - 16)))
    {
        area_shift();
        if (you.pet_target != MHITYOU)
            you.pet_target = MHITNOT;

#if DEBUG_DIAGNOSTICS
        mpr("Shifting.", MSGCH_DIAGNOSTICS);
        int j = 0;
        for (int i = 0; i < MAX_ITEMS; ++i)
            if (mitm[i].is_valid())
                ++j;

        mprf(MSGCH_DIAGNOSTICS, "Number of items present: %d", j);

        j = 0;
        for (monster_iterator mi; mi; ++mi)
            ++j;

        mprf(MSGCH_DIAGNOSTICS, "Number of monsters present: %d", j);
        mprf(MSGCH_DIAGNOSTICS, "Number of clouds present: %d", env.cloud_no);
#endif
    }

    apply_berserk_penalty = !attacking;

    if (!attacking && you.religion == GOD_CHEIBRIADOS && one_chance_in(10)
        && player_equip_ego_type(EQ_BOOTS, SPARM_RUNNING))
    {
        did_god_conduct(DID_HASTY, 1, true);
    }
}


static int _get_num_and_char_keyfun(int &ch)
{
    if (ch == CK_BKSP || isdigit(ch) || ch >= 128)
        return 1;

    return -1;
}

static int _get_num_and_char(const char* prompt, char* buf, int buf_len)
{
    if (prompt != NULL)
        mpr(prompt, MSGCH_PROMPT);

    line_reader reader(buf, buf_len);

    reader.set_keyproc(_get_num_and_char_keyfun);

    return reader.read_line(true);
}

static void _cancel_cmd_repeat()
{
    crawl_state.cancel_cmd_again();
    crawl_state.cancel_cmd_repeat();
    flush_input_buffer(FLUSH_REPLAY_SETUP_FAILURE);
}

static void _setup_cmd_repeat()
{
    if (is_processing_macro())
    {
        flush_input_buffer(FLUSH_ABORT_MACRO);
        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        return;
    }

    ASSERT(!crawl_state.is_repeating_cmd());

    char buf[80];

    // Function ensures that the buffer contains only digits.
    int ch = _get_num_and_char("Number of times to repeat, then command key: ",
                               buf, 80);

    if (ch == ESCAPE)
    {
        // This *might* be part of the trigger for a macro.
        keyseq trigger;
        trigger.push_back(ch);

        if (get_macro_buf_size() == 0)
        {
            // Was just a single ESCAPE key, so not a macro trigger.
            canned_msg( MSG_OK );
            _cancel_cmd_repeat();
            return;
        }
        ch = getchm();
        trigger.push_back(ch);

        // Now that we have the entirety of the (possible) macro trigger,
        // clear out the keypress recorder so that we won't have recorded
        // the trigger twice.
        repeat_again_rec.clear();

        insert_macro_into_buff(trigger);

        ch = getchm();
        if (ch == ESCAPE)
        {
            if (get_macro_buf_size() > 0)
                // User pressed an Alt key which isn't bound to a macro.
                mpr("That key isn't bound to a macro.");
            else
                // Wasn't a macro trigger, just an ordinary escape.
                canned_msg( MSG_OK );

            _cancel_cmd_repeat();
            return;
        }
        // *WAS* a macro trigger, keep going.
    }

    if (strlen(buf) == 0)
    {
        mpr("You must enter the number of times for the command to repeat.");
        _cancel_cmd_repeat();
        return;
    }

    int count = atoi(buf);

    if (crawl_state.doing_prev_cmd_again)
        count = crawl_state.prev_cmd_repeat_goal;

    if (count <= 0)
    {
        canned_msg( MSG_OK );
        _cancel_cmd_repeat();
        return;
    }

    if (crawl_state.doing_prev_cmd_again)
    {
        // If a "do previous command again" caused a command
        // repetition to be redone, the keys to be repeated are
        // already in the key recording buffer, so we just need to
        // discard all the keys saying how many times the command
        // should be repeated.
        do
        {
            repeat_again_rec.keys.pop_front();
        }
        while (repeat_again_rec.keys.size() > 0
               && repeat_again_rec.keys[0] != ch);

        repeat_again_rec.keys.pop_front();
    }

    // User can type space or enter and then the command key, in case
    // they want to repeat a command bound to a number key.
    c_input_reset(true);
    if (ch == ' ' || ch == CK_ENTER)
    {
        if (!crawl_state.doing_prev_cmd_again)
            repeat_again_rec.keys.pop_back();

        mpr("Enter command to be repeated: ", MSGCH_PROMPT);
        // Enable the cursor to read input.
        cursor_control con(true);

        crawl_state.waiting_for_command = true;

        ch = _get_next_keycode();

        crawl_state.waiting_for_command = false;
    }

    command_type cmd = _keycode_to_command( (keycode_type) ch);

    if (cmd != CMD_MOUSE_MOVE)
        c_input_reset(false);

    if (!is_processing_macro() && !_cmd_is_repeatable(cmd))
    {
        _cancel_cmd_repeat();
        return;
    }

    if (!crawl_state.doing_prev_cmd_again && cmd != CMD_PREV_CMD_AGAIN)
        crawl_state.prev_cmd_keys = repeat_again_rec.keys;

    if (is_processing_macro())
    {
        // Put back in first key of the expanded macro, which
        // get_next_keycode() fetched
        repeat_again_rec.paused = true;
        macro_buf_add(ch, true);

        // If we're repeating a macro, get rid the keys saying how
        // many times to repeat, because the way that macros are
        // repeated means that the number keys will be repeated if
        // they aren't discarded.
        keyseq &keys = repeat_again_rec.keys;
        ch = keys[keys.size() - 1];
        while (isdigit(ch) || ch == ' ' || ch == CK_ENTER)
        {
            keys.pop_back();
            // Handle the case where the user has macroed enter to itself.
            if (keys.empty())
            {
                _cancel_cmd_repeat();
                return;
            }
            ch = keys[keys.size() - 1];
        }
    }

    repeat_again_rec.paused = false;
    // Discard the setup for the command repetition, since what's
    // going to be repeated is yet to be typed, except for the fist
    // key typed which has to be put back in (unless we're repeating a
    // macro, in which case everything to be repeated is already in
    // the macro buffer).
    if (!is_processing_macro())
    {
        repeat_again_rec.clear();
        macro_buf_add(ch, crawl_state.doing_prev_cmd_again);
    }

    crawl_state.cmd_repeat_start     = true;
    crawl_state.cmd_repeat_count     = 0;
    crawl_state.repeat_cmd           = cmd;
    crawl_state.cmd_repeat_goal      = count;
    crawl_state.prev_cmd_repeat_goal = count;
    crawl_state.prev_repetition_turn = you.num_turns;

    crawl_state.cmd_repeat_started_unsafe = !i_feel_safe();

    crawl_state.input_line_strs.clear();
}

static void _do_prev_cmd_again()
{
    if (is_processing_macro())
    {
        mpr("Can't re-do previous command from within a macro.");
        flush_input_buffer(FLUSH_ABORT_MACRO);
        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        return;
    }

    if (crawl_state.prev_cmd == CMD_NO_CMD)
    {
        mpr("No previous command to re-do.");
        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        repeat_again_rec.clear();
        return;
    }

    ASSERT(!crawl_state.doing_prev_cmd_again
           || (crawl_state.is_repeating_cmd()
               && crawl_state.repeat_cmd == CMD_PREV_CMD_AGAIN));

    crawl_state.doing_prev_cmd_again = true;
    repeat_again_rec.paused          = false;

    if (crawl_state.prev_cmd == CMD_REPEAT_CMD)
    {
        crawl_state.cmd_repeat_start     = true;
        crawl_state.cmd_repeat_count     = 0;
        crawl_state.cmd_repeat_goal      = crawl_state.prev_cmd_repeat_goal;
        crawl_state.prev_repetition_turn = you.num_turns;
    }

    const keyseq &keys = crawl_state.prev_cmd_keys;
    ASSERT(keys.size() > 0);

    // Insert keys at front of input buffer, rather than at the end,
    // since if the player holds down the "`" key, then the buffer
    // might get two "`" in a row, and if the keys to be replayed go after
    // the second "`" then we get an assertion.
    macro_buf_add(keys, true);

    bool was_doing_repeats = crawl_state.is_repeating_cmd();

    _input();

    // crawl_state.doing_prev_cmd_again can be set to false
    // while input() does its stuff if something causes
    // crawl_state.cancel_cmd_again() to be called.
    while (!was_doing_repeats && crawl_state.is_repeating_cmd()
           && crawl_state.doing_prev_cmd_again)
    {
        _input();
    }

    if (!was_doing_repeats && crawl_state.is_repeating_cmd()
        && !crawl_state.doing_prev_cmd_again)
    {
        crawl_state.cancel_cmd_repeat();
    }

    crawl_state.doing_prev_cmd_again = false;
}

static void _update_replay_state()
{
    if (crawl_state.is_repeating_cmd())
    {
        // First repeat is to copy down the keys the user enters,
        // grab them so we can go on autopilot for the remaining
        // iterations.
        if (crawl_state.cmd_repeat_start)
        {
            ASSERT(repeat_again_rec.keys.size() > 0);

            crawl_state.cmd_repeat_start = false;
            crawl_state.repeat_cmd_keys  = repeat_again_rec.keys;

            // Setting up the "previous command key sequence"
            // for a repeated command is different from normal,
            // since in addition to all of the keystrokes for
            // the command, it needs the repeat command plus the
            // number of repeats at the very beginning of the
            // sequence.
            if (!crawl_state.doing_prev_cmd_again)
            {
                keyseq &prev = crawl_state.prev_cmd_keys;
                keyseq &curr = repeat_again_rec.keys;

                if (is_processing_macro())
                    prev = curr;
                else
                {
                    // Skip first key, because that's command key that's
                    // being repeated, which crawl_state.prev_cmd_keys
                    // aleardy contains.
                    keyseq::iterator begin = curr.begin();
                    begin++;

                    prev.insert(prev.end(), begin, curr.end());
                }
            }

            repeat_again_rec.paused = true;
            macro_buf_add(KEY_REPEAT_KEYS);
        }
    }

    if (!crawl_state.is_replaying_keys() && !crawl_state.cmd_repeat_start
        && crawl_state.prev_cmd != CMD_NO_CMD
        && crawl_state.prev_cmd != CMD_NEXT_CMD)
    {
        if (repeat_again_rec.keys.size() > 0)
            crawl_state.prev_cmd_keys = repeat_again_rec.keys;
    }

    if (!is_processing_macro())
        repeat_again_rec.clear();
}


static void _compile_time_asserts()
{
    // Check that the numbering comments in enum.h haven't been
    // disturbed accidentally.
    COMPILE_CHECK(SK_UNARMED_COMBAT == 17       , c1);
    COMPILE_CHECK(SK_EVOCATIONS == 38           , c2);
    COMPILE_CHECK(SP_VAMPIRE == 30              , c3);
    COMPILE_CHECK(SPELL_DEBUGGING_RAY == 102    , c4);
    COMPILE_CHECK(SPELL_PETRIFY == 154          , c5);
    COMPILE_CHECK(NUM_SPELLS == 198             , c6);

    //jmf: NEW ASSERTS: we ought to do a *lot* of these
    COMPILE_CHECK(NUM_SPECIES < SP_UNKNOWN      , c7);
    COMPILE_CHECK(NUM_JOBS < JOB_UNKNOWN        , c8);

    // Make sure there's enough room in you.unique_items to hold all
    // the unrandarts.
    COMPILE_CHECK(NO_UNRANDARTS < MAX_UNRANDARTS, c9);

    // Non-artefact brands and unrandart indexes both go into
    // item.special, so make sure they don't overlap.
    COMPILE_CHECK((int) NUM_SPECIAL_WEAPONS < (int) UNRAND_START, c10);

    // We have space for 32 brands in the bitfield.
    COMPILE_CHECK((int) SP_UNKNOWN_BRAND < 8*sizeof(you.seen_weapon[0]), c11);
    COMPILE_CHECK((int) SP_UNKNOWN_BRAND < 8*sizeof(you.seen_armour[0]), c12);
    COMPILE_CHECK(NUM_SPECIAL_WEAPONS <= SP_UNKNOWN_BRAND, c13);
    COMPILE_CHECK(NUM_SPECIAL_ARMOURS <= SP_UNKNOWN_BRAND, c14);

    // Also some runtime stuff; I don't know if the order of branches[]
    // needs to match the enum, but it currently does.
    for (int i = 0; i < NUM_BRANCHES; ++i)
        ASSERT(branches[i].id == i);
}
