/*
 *  File:       files.cc
 *  Summary:    Functions used to save and load levels/games.
 *  Written by: Linley Henzell and Alexey Guzeev
 */

#include "AppHdr.h"

#include "delay.h"
#include "files.h"
#include "mon-place.h"
#include "coord.h"

#include <errno.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <algorithm>
#include <functional>

#ifdef TARGET_OS_DOS
#include <file.h>
#endif

#ifdef UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef TARGET_COMPILER_MINGW
#include <io.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include "externs.h"

#include "artefact.h"
#include "chardump.h"
#include "cloud.h"
#include "clua.h"
#include "coordit.h"
#include "debug.h"
#include "directn.h"
#include "dungeon.h"
#include "effects.h"
#include "env.h"
#include "ghost.h"
#include "initfile.h"
#include "items.h"
#include "jobs.h"
#include "kills.h"
#include "libutil.h"
#include "mapmark.h"
#include "message.h"
#include "misc.h"
#include "mon-act.h"
#include "mon-stuff.h"
#include "mon-util.h"
#include "mon-transit.h"
#include "newgame.h"
#include "notes.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "place.h"
#include "player.h"
#include "random.h"
#include "stash.h"
#include "state.h"
#include "stuff.h"
#include "tags.h"
#ifdef USE_TILE
#include "tiles.h"
#include "tiledef-player.h"
#endif
#include "terrain.h"
#include "travel.h"
#include "tutorial.h"
#include "view.h"
#include "viewgeom.h"

#ifdef TARGET_COMPILER_VC
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

#ifndef HAVE_STAT
#if defined(UNIX) || defined(TARGET_COMPILER_MINGW) || defined(TARGET_OS_DOS)
#define HAVE_STAT
#endif
#endif

static std::vector<SavefileCallback::callback>* _callback_list = NULL;

static void _save_level( int level_saved, level_area_type lt,
                         branch_type where_were_you);

static bool _get_and_validate_version( FILE *restoreFile, char& major,
                                       char& minor, std::string* reason = 0);


static bool _determine_ghost_version( FILE *ghostFile,
                                      char &majorVersion, char &minorVersion );

static void _restore_ghost_version( FILE *ghostFile, char major, char minor );

static void _restore_tagged_file( FILE *restoreFile, int fileType,
                                  char minorVersion );

const short GHOST_SIGNATURE = short( 0xDC55 );

static void _redraw_all(void)
{
    you.redraw_hit_points   = true;
    you.redraw_magic_points = true;
    you.redraw_strength     = true;
    you.redraw_intelligence = true;
    you.redraw_dexterity    = true;
    you.redraw_armour_class = true;
    you.redraw_evasion      = true;
    you.redraw_experience   = true;

    you.redraw_status_flags =
        REDRAW_LINE_1_MASK | REDRAW_LINE_2_MASK | REDRAW_LINE_3_MASK;
}

static std::string _uid_as_string()
{
#ifdef MULTIUSER
    return make_stringf("-%d", static_cast<int>(getuid()));
#else
    return std::string();
#endif
}

static bool _is_uid_file(const std::string &name, const std::string &ext)
{
    std::string save_suffix = get_savedir_filename("", "", "");
    save_suffix += ext;
#ifdef TARGET_OS_DOS
    // Grumble grumble. Hang all retarded operating systems.
    uppercase(save_suffix);
#endif

    save_suffix = save_suffix.substr(Options.save_dir.length());

    std::string::size_type suffix_pos = name.find(save_suffix);
    return (suffix_pos != std::string::npos
            && suffix_pos == name.length() - save_suffix.length()
            && suffix_pos != 0);
}

bool is_save_file_name(const std::string &name)
{
    return _is_uid_file(name, ".sav");
}

#ifdef LOAD_UNPACKAGE_CMD
bool is_packed_save(const std::string &name)
{
    return _is_uid_file(name, PACKAGE_SUFFIX);
}
#endif

// Returns the save_info from the save.
player_save_info read_character_info(const std::string &savefile)
{
    player_save_info fromfile;
    FILE *charf = fopen(savefile.c_str(), "rb");
    if (!charf)
        return fromfile;

    char majorVersion;
    char minorVersion;

    if (_get_and_validate_version(charf, majorVersion, minorVersion))
    {
        // Backup before we clobber "you".
        const player backup(you);

        _restore_tagged_file(charf, TAGTYPE_PLAYER_NAME, minorVersion);

        fromfile = you;
        you.copy_from(backup);
    }
    fclose(charf);

    return fromfile;
}

static inline bool _is_good_filename(const std::string &s)
{
    return (s != "." && s != "..");
}

#if defined(TARGET_OS_DOS)
// Abbreviates a given file name to DOS style "xxxxxx~1.txt".
// Does not take into account files with differing suffixes or files
// with a prepended path with more than one separator.
// (It does handle all files included with the distribution except
//  changes.stone_soup.)
bool get_dos_compatible_file_name(std::string *fname)
{
    std::string::size_type pos1 = fname->find("\\");
    if (pos1 == std::string::npos)
        pos1 = 0;

    const std::string::size_type pos2 = fname->find(".txt");
    // Name already fits DOS requirements, nothing to be done.
    if (fname->substr(pos1, pos2).length() <= 8)
        return (false);

    *fname = fname->substr(0,pos1) + fname->substr(pos1, pos1 + 6) + "~1.txt";

    return (true);
}
#endif

// Returns the names of all files in the given directory. Note that the
// filenames returned are relative to the directory.
std::vector<std::string> get_dir_files(const std::string &dirname)
{
    std::vector<std::string> files;

#ifdef TARGET_COMPILER_VC
    WIN32_FIND_DATA lData;
    std::string dir = dirname;
    if (!dir.empty() && dir[dir.length() - 1] != FILE_SEPARATOR)
        dir += FILE_SEPARATOR;
    dir += "*";
    HANDLE hFind = FindFirstFile(dir.c_str(), &lData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        if (_is_good_filename(lData.cFileName))
            files.push_back(lData.cFileName);
        while (FindNextFile(hFind, &lData))
        {
            if (_is_good_filename(lData.cFileName))
                files.push_back(lData.cFileName);
        }
        FindClose(hFind);
    }
#else // non-MS VC++ compilers

    DIR *dir = opendir(dirname.c_str());
    if (!dir)
        return (files);

    while (dirent *entry = readdir(dir))
    {
        std::string name = entry->d_name;
        if (_is_good_filename(name))
            files.push_back(name);
    }
    closedir(dir);
#endif

    return (files);
}

std::vector<std::string> get_dir_files_ext(const std::string &dir,
                                           const std::string &ext)
{
    const std::vector<std::string> allfiles(get_dir_files(dir));
    std::vector<std::string> filtered;
    for (int i = 0, size = allfiles.size(); i < size; ++i)
        if (ends_with(allfiles[i], ext))
            filtered.push_back(allfiles[i]);
    return (filtered);
}

std::string get_parent_directory(const std::string &filename)
{
    std::string::size_type pos = filename.rfind(FILE_SEPARATOR);
    if (pos != std::string::npos)
        return filename.substr(0, pos + 1);
#ifdef ALT_FILE_SEPARATOR
    pos = filename.rfind(ALT_FILE_SEPARATOR);
    if (pos != std::string::npos)
        return filename.substr(0, pos + 1);
#endif
    return ("");
}

std::string get_base_filename(const std::string &filename)
{
    std::string::size_type pos = filename.rfind(FILE_SEPARATOR);
    if (pos != std::string::npos)
        return filename.substr(pos + 1);
#ifdef ALT_FILE_SEPARATOR
    pos = filename.rfind(ALT_FILE_SEPARATOR);
    if (pos != std::string::npos)
        return filename.substr(pos + 1);
#endif
    return (filename);
}

bool is_absolute_path(const std::string &path)
{
    return (!path.empty()
            && (path[0] == FILE_SEPARATOR
#ifdef TARGET_OS_WINDOWS
                || path.find(':') != std::string::npos
#endif
                ));
}

// Concatenates two paths, separating them with FILE_SEPARATOR if necessary.
// Assumes that the second path is not absolute.
//
// If the first path is empty, returns the second unchanged. The second path
// may be absolute in this case.
std::string catpath(const std::string &first, const std::string &second)
{
    if (first.empty())
        return (second);

    std::string directory = first;
    if (directory[directory.length() - 1] != FILE_SEPARATOR)
        directory += FILE_SEPARATOR;
    directory += second;

    return (directory);
}

// Given a relative path and a reference file name, returns the relative path
// suffixed to the directory containing the reference file name. Assumes that
// the second path is not absolute.
std::string get_path_relative_to(const std::string &referencefile,
                                 const std::string &relativepath)
{
    return catpath(get_parent_directory(referencefile),
                   relativepath);
}

std::string change_file_extension(const std::string &filename,
                                  const std::string &ext)
{
    const std::string::size_type pos = filename.rfind('.');
    return ((pos == std::string::npos? filename : filename.substr(0, pos))
            + ext);
}

time_t file_modtime(const std::string &file)
{
    struct stat filestat;
    if (stat(file.c_str(), &filestat))
        return (0);

    return (filestat.st_mtime);
}

// Returns true if file a is newer than file b.
bool is_newer(const std::string &a, const std::string &b)
{
    return (file_modtime(a) > file_modtime(b));
}

void check_newer(const std::string &target,
                 const std::string &dependency,
                 void (*action)())
{
    if (is_newer(dependency, target))
        action();
}

bool file_exists(const std::string &name)
{
#ifdef HAVE_STAT
    struct stat st;
    const int err = ::stat(name.c_str(), &st);
    return (!err);
#else
    FILE *f = fopen(name.c_str(), "r");
    const bool exists = !!f;
    if (f)
        fclose(f);
    return (exists);
#endif
}

// Low-tech existence check.
bool dir_exists(const std::string &dir)
{
#ifdef TARGET_COMPILER_VC
    DWORD lAttr = GetFileAttributes(dir.c_str());
    return (lAttr != INVALID_FILE_ATTRIBUTES
            && (lAttr & FILE_ATTRIBUTE_DIRECTORY));
#elif defined(HAVE_STAT)
    struct stat st;
    const int err = ::stat(dir.c_str(), &st);
    return (!err && S_ISDIR(st.st_mode));
#else
    DIR *d = opendir(dir.c_str());
    const bool exists = !!d;
    if (d)
        closedir(d);

    return (exists);
#endif
}

static int _create_directory(const char *dir)
{
#if defined(MULTIUSER)
    return mkdir(dir, SHARED_FILES_CHMOD_PUBLIC | 0111);
#elif defined(TARGET_OS_DOS)
    return mkdir(dir, 0755);
#elif defined(TARGET_COMPILER_VC)
    return _mkdir(dir);
#else
    return mkdir(dir);
#endif
}

static bool _create_dirs(const std::string &dir)
{
    std::string sep = " ";
    sep[0] = FILE_SEPARATOR;
    std::vector<std::string> segments =
        split_string(
                sep.c_str(),
                dir,
                false,
                false);

    std::string path;
    for (int i = 0, size = segments.size(); i < size; ++i)
    {
        path += segments[i];

        // Handle absolute paths correctly.
        if (i == 0 && dir.size() && dir[0] == FILE_SEPARATOR)
            path = FILE_SEPARATOR + path;

        if (!dir_exists(path) && _create_directory(path.c_str()))
            return (false);

        path += FILE_SEPARATOR;
    }
    return (true);
}

// Checks whether the given path is safe to read from. A path is safe if:
// 1. If Unix: It contains no shell metacharacters.
// 2. If DATA_DIR_PATH is set: the path is not an absolute path.
// 3. If DATA_DIR_PATH is set: the path contains no ".." sequence.
void assert_read_safe_path(const std::string &path) throw (std::string)
{
    // Check for rank tomfoolery first:
    if (path.empty())
        throw "Empty file name.";

#ifdef UNIX
    if (!shell_safe(path.c_str()))
        throw make_stringf("\"%s\" contains bad characters.",
                           path.c_str());
#endif

#ifdef DATA_DIR_PATH
    if (is_absolute_path(path))
        throw make_stringf("\"%s\" is an absolute path.", path.c_str());

    if (path.find("..") != std::string::npos)
    {
        throw make_stringf("\"%s\" contains \"..\" sequences.",
                           path.c_str());
    }
#endif

    // Path is okay.
}

bool is_read_safe_path(const std::string &path)
{
    try
    {
        assert_read_safe_path(path);
    }
    catch (const std::string &)
    {
        return (false);
    }
    return (true);
}

std::string canonicalise_file_separator(const std::string &path)
{
#if FILE_SEPARATOR != '/'
    return (replace_all_of(path, "/", std::string(1, FILE_SEPARATOR)));
#else
    // No action needed here.
    return (path);
#endif
}

std::string datafile_path(std::string basename,
                          bool croak_on_fail,
                          bool test_base_path,
                          bool (*thing_exists)(const std::string&))
{
    basename = canonicalise_file_separator(basename);

    if (test_base_path && thing_exists(basename))
        return (basename);

    const std::string rawbases[] = {
#ifdef DATA_DIR_PATH
        DATA_DIR_PATH,
#else
        !SysEnv.crawl_dir.empty()? SysEnv.crawl_dir : "",
#endif
#ifdef TARGET_OS_MACOSX
        SysEnv.crawl_base + "../Resources/",
#endif
    };

    const std::string prefixes[] = {
        std::string("dat") + FILE_SEPARATOR,
#ifdef USE_TILE
        std::string("dat/tiles") + FILE_SEPARATOR,
#endif
        std::string("docs") + FILE_SEPARATOR,
        std::string("settings") + FILE_SEPARATOR,
#ifndef DATA_DIR_PATH
        std::string("..") + FILE_SEPARATOR + "docs" + FILE_SEPARATOR,
        std::string("..") + FILE_SEPARATOR + "dat" + FILE_SEPARATOR,
#ifdef USE_TILE
        std::string("..") + FILE_SEPARATOR + "dat/tiles" + FILE_SEPARATOR,
#endif
        std::string("..") + FILE_SEPARATOR + "settings" + FILE_SEPARATOR,
        std::string("..") + FILE_SEPARATOR,
#endif
        "",
    };

    std::vector<std::string> bases;
    for (unsigned i = 0; i < sizeof(rawbases) / sizeof(*rawbases); ++i)
    {
        std::string base = rawbases[i];
        if (base.empty())
            continue;

        if (base[base.length() - 1] != FILE_SEPARATOR)
            base += FILE_SEPARATOR;
        bases.push_back(base);
    }

#ifndef DATA_DIR_PATH
    if (!SysEnv.crawl_base.empty())
        bases.push_back(SysEnv.crawl_base);
    bases.push_back("");
#endif

    for (unsigned b = 0, size = bases.size(); b < size; ++b)
        for (unsigned p = 0; p < sizeof(prefixes) / sizeof(*prefixes); ++p)
        {
            std::string name = bases[b] + prefixes[p] + basename;
            if (thing_exists(name))
                return (name);
        }

    // Die horribly.
    if (croak_on_fail)
    {
        end(1, false, "Cannot find data file '%s' anywhere, aborting\n",
            basename.c_str());
    }

    return ("");
}

bool check_dir(const std::string &whatdir, std::string &dir, bool silent)
{
    if (dir.empty())
        return (true);

    const std::string sep(1, FILE_SEPARATOR);

    dir = replace_all_of(dir, "/", sep);
    dir = replace_all_of(dir, "\\", sep);

    // Suffix the separator if necessary
    if (dir[dir.length() - 1] != FILE_SEPARATOR)
        dir += FILE_SEPARATOR;

    if (!dir_exists(dir) && !_create_dirs(dir))
    {
        if (!silent)
            fprintf(stderr, "%s \"%s\" does not exist "
                    "and I can't create it.\n",
                    whatdir.c_str(), dir.c_str());
        return (false);
    }

    return (true);
}

// Given a simple (relative) name of a save file, returns the full path of
// the file in the Crawl saves directory. You can use path segments in
// shortpath (separated by /) and get_savedir_path will canonicalise them
// to the platform's native file separator.
std::string get_savedir_path(const std::string &shortpath)
{
    const std::string file = Options.save_dir + shortpath;
#if FILE_SEPARATOR != '/'
    return (replace_all(file, "/", std::string(1, FILE_SEPARATOR)));
#else
    return (file);
#endif
}

#ifdef USE_TILE
static void _fill_player_doll(player_save_info &p, const std::string &dollfile)
{
    dolls_data equip_doll;
    for (unsigned int j = 0; j < TILEP_PART_MAX; ++j)
        equip_doll.parts[j] = TILEP_SHOW_EQUIP;

    equip_doll.parts[TILEP_PART_BASE]
        = tilep_species_to_base_tile(p.species, p.experience_level);

    bool success = false;

    FILE *fdoll = fopen(dollfile.c_str(), "r");
    if (fdoll)
    {
        char fbuf[1024];
        memset(fbuf, 0, sizeof(fbuf));
        if (fscanf(fdoll, "%s", fbuf) != EOF)
        {
            tilep_scan_parts(fbuf, equip_doll);
            tilep_race_default(p.species,
                               get_gender_from_tile(equip_doll.parts),
                               p.experience_level,
                               equip_doll.parts);
            success = true;

            while (fscanf(fdoll, "%s", fbuf) != EOF)
            {
                if (strcmp(fbuf, "net") == 0)
                    p.held_in_net = true;
            }
        }
        fclose(fdoll);
    }

    if (!success) // Use default doll instead.
    {
        job_type job = get_class_by_name(p.class_name.c_str());
        if (job == -1)
            job = JOB_FIGHTER;

        int gender = coinflip();
        tilep_job_default(job, gender, equip_doll.parts);
    }
    p.doll = equip_doll;
}
#endif


/*
 * Returns a list of the names of characters that are already saved for the
 * current user.
 */

std::vector<player_save_info> find_saved_characters()
{
    std::vector<player_save_info> chars;
#ifndef DISABLE_SAVEGAME_LISTS
    std::string searchpath = Options.save_dir;

    if (searchpath.empty())
        searchpath = ".";

    std::vector<std::string> allfiles = get_dir_files(searchpath);
    for (unsigned int i = 0; i < allfiles.size(); ++i)
    {
        std::string filename = allfiles[i];

        std::string::size_type point_pos = filename.find_last_of('.');
        std::string basename = filename.substr(0, point_pos);

#ifdef LOAD_UNPACKAGE_CMD
        if (!is_packed_save(filename))
            continue;

        std::string zipname = get_savedir_path(basename);
        escape_path_spaces(zipname);

        // This is the filename we actually read ourselves.
        filename = basename + ".sav";
        escape_path_spaces(filename);

        std::string dir = get_savedir();
        escape_path_spaces(dir);

        char cmd_buff[1024];
        snprintf( cmd_buff, sizeof(cmd_buff), UNPACK_SPECIFIC_FILE_CMD,
                  zipname.c_str(),
                  dir.c_str(),
                  filename.c_str() );

        if (system(cmd_buff) != 0)
            continue;
#endif
        if (is_save_file_name(filename))
        {
            const std::string path = get_savedir_path(filename);
            player_save_info p = read_character_info(path);
            if (!p.name.empty())
            {
#ifdef USE_TILE
                if (Options.tile_menu_icons)
                {
 #ifndef LOAD_UNPACKAGE_CMD
                    basename = filename.substr(0,
                            filename.length() - strlen(".sav"));
 #endif
                    std::string dollname = basename + ".tdl";
                    const std::string dollpath = get_savedir_path(dollname);
 #ifdef LOAD_UNPACKAGE_CMD
                    escape_path_spaces(dollname);
                    snprintf( cmd_buff, sizeof(cmd_buff),
                              UNPACK_SPECIFIC_FILE_CMD,
                              zipname.c_str(),
                              dir.c_str(),
                              dollname.c_str() );
                    system(cmd_buff);
 #endif
                    _fill_player_doll(p, dollpath);
 #ifdef LOAD_UNPACKAGE_CMD
                    // Throw away doll file.
                    if (file_exists(dollpath.c_str()))
                        unlink( dollpath.c_str() );
 #endif
                }
#endif
                chars.push_back(p);
            }
        }

#ifdef LOAD_UNPACKAGE_CMD
        // If we unpacked the .sav file, throw it away now.
        unlink( get_savedir_path(filename).c_str() );
#endif
    }

    std::sort(chars.rbegin(), chars.rend());
#endif // !DISABLE_SAVEGAME_LISTS
    return (chars);
}

std::string get_savedir()
{
    const std::string &dir = Options.save_dir;
    return (dir.empty() ? "." : dir);
}

std::string get_savedir_filename(const std::string &prefix,
                                 const std::string &suffix,
                                 const std::string &extension,
                                 bool suppress_uid)
{
    std::string result = Options.save_dir;

    result += get_save_filename(prefix, suffix, extension, suppress_uid);

#ifdef TARGET_OS_DOS
    uppercase(result);
#endif

    return result;
}

std::string get_save_filename(const std::string &prefix,
                                 const std::string &suffix,
                                 const std::string &extension,
                                 bool suppress_uid)
{
    std::string result = "";

    // Shorten string as appropriate
    result += strip_filename_unsafe_chars(prefix).substr(0, kFileNameLen);

    // Technically we should shorten the string first.  But if
    // MULTIUSER is set we'll have long filenames anyway. Caveat
    // emptor.
    if ( !suppress_uid )
        result += _uid_as_string();

    result += suffix;

    if (!extension.empty())
    {
        result += '.';
        result += extension;
    }

#ifdef TARGET_OS_DOS
    uppercase(result);
#endif
    return result;
}

std::string get_prefs_filename()
{
#ifdef DGL_STARTUP_PREFS_BY_NAME
    return get_savedir_filename("start-" + Options.player_name + "-",
                                "ns", "prf", true);
#else
    return get_savedir_filename("start", "ns", "prf");
#endif
}

static std::string _get_level_suffix(int level, branch_type where,
                                     level_area_type lt)
{
    switch (lt)
    {
    default:
    case LEVEL_DUNGEON:
        return (make_stringf("%02d%c", subdungeon_depth(where, level),
                             where + 'a'));
    case LEVEL_LABYRINTH:
        return ("lab");
    case LEVEL_ABYSS:
        return ("abs");
    case LEVEL_PANDEMONIUM:
        return ("pan");
    case LEVEL_PORTAL_VAULT:
        return ("ptl");
    }
}

std::string make_filename(std::string prefix, int level, branch_type where,
                          level_area_type ltype, bool isGhost)
{
    return get_savedir_filename(prefix, "",
                                _get_level_suffix(level, where, ltype),
                                isGhost );
}

static void _write_version( FILE *dataFile, int majorVersion, int minorVersion,
                            bool extended_version )
{
    // write version
    writer outf(dataFile);

    marshallByte(outf, majorVersion);
    marshallByte(outf, minorVersion);

    // extended_version just pads the version out to four 32-bit words.
    // This makes the bones file compatible with Hearse with no extra
    // munging needed.
    if (extended_version)
    {
        // Use a single signature 16-bit word to indicate that this is
        // Stone Soup and to disambiguate this (unmunged) bones file
        // from the munged bones files offered by the old Crawl-aware
        // hearse.pl. Crawl-aware hearse.pl will prefix the bones file
        // with the first 16-bits of the Crawl version, and the following
        // 7 16-bit words set to 0.
        marshallShort(outf, GHOST_SIGNATURE);

        // Write the three remaining 32-bit words of padding.
        for (int i = 0; i < 3; ++i)
            marshallLong(outf, 0);
    }
}

static void _write_tagged_file( FILE *outf, int fileType,
                                bool extended_version = false )
{
    // find all relevant tags
    char tags[NUM_TAGS];
    tag_set_expected(tags, fileType);

    _write_version( outf, TAG_MAJOR_VERSION, TAG_MINOR_VERSION,
                    extended_version );

    // all other tags
    for (int i = 1; i < NUM_TAGS; i++)
        if (tags[i] == 1)
            tag_write(static_cast<tag_type>(i), outf);
}

bool travel_load_map( branch_type branch, int absdepth )
{
    // Try to open level savefile.
    FILE *levelFile = fopen(make_filename(you.your_name, absdepth, branch,
                                          LEVEL_DUNGEON, false).c_str(), "rb");
    if (!levelFile)
        return (false);

    char majorVersion;
    char minorVersion;

    if (!_get_and_validate_version( levelFile, majorVersion, minorVersion ))
    {
        fclose(levelFile);
        return (false);
    }

    tag_read(levelFile, minorVersion);

    fclose( levelFile );

    return (true);
}

static void _place_player_on_stair(level_area_type old_level_type,
                                   branch_type old_branch,
                                   int stair_taken)
{
    bool find_first = true;

    // Order is important here.
    if (stair_taken == DNGN_EXIT_PANDEMONIUM)
    {
        stair_taken = DNGN_ENTER_PANDEMONIUM;
        find_first = false;
    }
    else if (stair_taken == DNGN_EXIT_ABYSS)
    {
        stair_taken = DNGN_ENTER_ABYSS;
        find_first = false;
    }
    else if (stair_taken == DNGN_EXIT_HELL)
    {
        stair_taken = DNGN_ENTER_HELL;
    }
    else if (stair_taken == DNGN_ENTER_HELL)
    {
        // The vestibule and labyrinth always start from this stair.
        stair_taken = DNGN_EXIT_HELL;
    }
    else if (stair_taken == DNGN_EXIT_PORTAL_VAULT
             || ((old_level_type == LEVEL_LABYRINTH
                  || old_level_type == LEVEL_PORTAL_VAULT)
                 && (stair_taken == DNGN_ESCAPE_HATCH_DOWN
                     || stair_taken == DNGN_ESCAPE_HATCH_UP)))
    {
        stair_taken = DNGN_EXIT_PORTAL_VAULT;
    }
    else if (stair_taken >= DNGN_STONE_STAIRS_DOWN_I
             && stair_taken <= DNGN_ESCAPE_HATCH_DOWN)
    {
        // Look for corresponding up stair.
        stair_taken += (DNGN_STONE_STAIRS_UP_I - DNGN_STONE_STAIRS_DOWN_I);
    }
    else if (stair_taken >= DNGN_STONE_STAIRS_UP_I
             && stair_taken <= DNGN_ESCAPE_HATCH_UP)
    {
        // Look for coresponding down stair.
        stair_taken += (DNGN_STONE_STAIRS_DOWN_I - DNGN_STONE_STAIRS_UP_I);
    }
    else if (stair_taken >= DNGN_RETURN_FROM_FIRST_BRANCH
             && stair_taken < 150) // 20 slots reserved
    {
        // Find entry point to subdungeon when leaving.
        stair_taken += (DNGN_ENTER_FIRST_BRANCH
                        - DNGN_RETURN_FROM_FIRST_BRANCH);
    }
    else if (stair_taken >= DNGN_ENTER_FIRST_BRANCH
             && stair_taken < DNGN_RETURN_FROM_FIRST_BRANCH)
    {
        // Find exit staircase from subdungeon when entering.
        stair_taken += (DNGN_RETURN_FROM_FIRST_BRANCH
                        - DNGN_ENTER_FIRST_BRANCH);
    }
    else if (stair_taken >= DNGN_ENTER_DIS
             && stair_taken <= DNGN_ENTER_TARTARUS)
    {
        // Only when entering a hell - when exiting, go back to the
        // entry stair.
        if (player_in_hell())
            stair_taken = DNGN_STONE_STAIRS_UP_I;
    }
    else if (stair_taken == DNGN_EXIT_ABYSS)
    {
        stair_taken = DNGN_STONE_STAIRS_UP_I;
    }
    else if (stair_taken == DNGN_ENTER_PORTAL_VAULT)
    {
        stair_taken = DNGN_STONE_ARCH;
    }
    else if (stair_taken == DNGN_ENTER_LABYRINTH)
    {
        // dgn_find_nearby_stair uses special logic for labyrinths.
        stair_taken = DNGN_ENTER_LABYRINTH;
    }
    else // Note: stair_taken can equal things like DNGN_FLOOR
    {
        // Just find a nice empty square.
        stair_taken = DNGN_FLOOR;
        find_first = false;
    }

    const coord_def where_to_go =
        dgn_find_nearby_stair(static_cast<dungeon_feature_type>(stair_taken),
                              you.pos(), find_first);
    you.moveto(where_to_go);
}

static void _close_level_gates()
{
    for (rectangle_iterator ri(0); ri; ++ri)
    {
        if (you.char_direction == GDT_ASCENDING
            && you.level_type != LEVEL_PANDEMONIUM)
        {
            if (feat_sealable_portal(grd(*ri)))
            {
                grd(*ri) = DNGN_STONE_ARCH;
                env.markers.remove_markers_at(*ri, MAT_ANY);
            }
        }
    }
}

static void _clear_env_map()
{
    env.map_knowledge.init(map_cell());
}

static void _clear_clouds()
{
    for (int clouty = 0; clouty < MAX_CLOUDS; ++clouty)
        delete_cloud( clouty );
    env.cgrid.init(EMPTY_CLOUD);
}

static bool _grab_follower_at(const coord_def &pos)
{
    if (pos == you.pos())
        return (false);

    monsters *fmenv = monster_at(pos);
    if (!fmenv || !fmenv->alive())
        return (false);

    // The monster has to already be tagged in order to follow.
    if (!testbits(fmenv->flags, MF_TAKING_STAIRS))
        return (false);

    level_id dest = level_id::current();
    if (you.char_direction == GDT_GAME_START)
        dest.depth = 1;

#if DEBUG_DIAGNOSTICS
    mprf(MSGCH_DIAGNOSTICS, "%s is following to %s.",
         fmenv->name(DESC_CAP_THE, true).c_str(),
         dest.describe().c_str());
#endif
    fmenv->set_transit(dest);
    fmenv->destroy_inventory();
    monster_cleanup(fmenv);
    return (true);
}

static void _grab_followers()
{
    const bool can_follow = level_type_allows_followers(you.level_type);

    int non_stair_using_allies = 0;
    monsters *dowan = NULL;
    monsters *duvessa = NULL;

    // Handle nearby ghosts.
    for (adjacent_iterator ai(you.pos()); ai; ++ai)
    {
        monsters *fmenv = monster_at(*ai);
        if (fmenv == NULL)
            continue;

        if ((fmenv->type == MONS_DUVESSA
            || (fmenv->props.exists("original_name")
                && fmenv->props["original_name"].get_string() == "Duvessa"))
               && fmenv->alive())
        {
            duvessa = fmenv;
        }

        if ((fmenv->type == MONS_DOWAN
            || (fmenv->props.exists("original_name")
                && fmenv->props["original_name"].get_string() == "Dowan"))
               && fmenv->alive())
        {
            dowan = fmenv;
        }

        if (fmenv->wont_attack() && !mons_can_use_stairs(fmenv))
            non_stair_using_allies++;

        if (fmenv->type == MONS_PLAYER_GHOST
            && fmenv->hit_points < fmenv->max_hit_points / 2)
        {
            if (fmenv->visible_to(&you))
                mpr("The ghost fades into the shadows.");
            monster_teleport(fmenv, true);
        }
    }

    // Deal with Dowan and Duvessa here.
    if (dowan && duvessa)
    {
        if (!testbits(dowan->flags, MF_TAKING_STAIRS)
            || !testbits(duvessa->flags, MF_TAKING_STAIRS))
        {
            dowan->flags &= ~MF_TAKING_STAIRS;
            duvessa->flags &= ~MF_TAKING_STAIRS;
        }
    }
    else if (dowan && !duvessa)
    {
        if (!dowan->props.exists("can_climb"))
            dowan->flags &= ~MF_TAKING_STAIRS;
    }
    else if (!dowan && duvessa)
    {
        if (!duvessa->props.exists("can_climb"))
            duvessa->flags &= ~MF_TAKING_STAIRS;
    }

    if (can_follow)
    {
        if (non_stair_using_allies > 0)
        {
            // XXX: This assumes that the only monsters that are
            // incapable of using stairs are zombified.
            mprf("Your mindless thrall%s stay%s behind.",
                 non_stair_using_allies > 1 ? "s" : "",
                 non_stair_using_allies > 1 ? ""  : "s");
        }

        memset(travel_point_distance, 0, sizeof(travel_distance_grid_t));
        std::vector<coord_def> places[2];
        int place_set = 0;
        places[place_set].push_back(you.pos());
        while (!places[place_set].empty())
        {
            for (int i = 0, size = places[place_set].size(); i < size; ++i)
            {
                const coord_def &p = places[place_set][i];
                coord_def fp;
                for (fp.x = p.x - 1; fp.x <= p.x + 1; ++fp.x)
                    for (fp.y = p.y - 1; fp.y <= p.y + 1; ++fp.y)
                    {
                        if (!in_bounds(fp) || travel_point_distance[fp.x][fp.y])
                            continue;
                        travel_point_distance[fp.x][fp.y] = 1;
                        if (_grab_follower_at(fp))
                            places[!place_set].push_back(fp);
                    }
            }
            places[place_set].clear();
            place_set = !place_set;
        }
    }

    // Clear flags on the followers that didn't make it.
    for (int i = 0; i < MAX_MONSTERS; ++i)
    {
        monsters *mons = &menv[i];
        if (!mons->alive())
            continue;
        mons->flags &= ~MF_TAKING_STAIRS;
    }
}

// Should be called after _grab_followers(), so that items carried by
// followers won't be considered lost.
static void _do_lost_items(level_area_type old_level_type)
{
    if (old_level_type == LEVEL_DUNGEON)
        return;

    for (int i = 0; i < MAX_ITEMS; i++)
    {
        item_def& item(mitm[i]);

        if (!item.is_valid())
            continue;

        // Item is in player inventory, so it's not lost.
        if (item.pos == coord_def(-1,-1))
            continue;

        item_was_lost(item);
    }
}

bool load( dungeon_feature_type stair_taken, load_mode_type load_mode,
           level_area_type old_level_type, char old_level,
           branch_type old_branch )
{
    unwind_var<dungeon_feature_type> stair(
        you.transit_stair, stair_taken, DNGN_UNSEEN);
    unwind_bool ylev(you.entering_level, true, false);

#ifdef DEBUG_LEVEL_LOAD
    mprf(MSGCH_DIAGNOSTICS, "Loading... level type: %d, branch: %d, level: %d",
                            you.level_type, you.where_are_you, you.your_level);
#endif

    // Going up/down stairs, going through a portal, or being banished
    // means the previous x/y movement direction is no longer valid.
    you.reset_prev_move();
#ifdef USE_TILE
    you.last_clicked_grid = coord_def();
#endif

    const bool make_changes =
        (load_mode != LOAD_RESTART_GAME && load_mode != LOAD_VISITOR);

    bool just_created_level = false;

    std::string cha_fil = make_filename( you.your_name, you.your_level,
                                         you.where_are_you,
                                         you.level_type,
                                         false );

    if (you.level_type == LEVEL_DUNGEON && old_level_type == LEVEL_DUNGEON
        || load_mode == LOAD_START_GAME && you.char_direction != GDT_GAME_START)
    {
        const level_id current(level_id::current());
        if (Generated_Levels.find(current) == Generated_Levels.end())
        {
            // Make sure the old file is gone.
            unlink(cha_fil.c_str());

            // Save the information for later deletion -- DML 6/11/99
            Generated_Levels.insert(current);
        }
    }

    you.prev_targ     = MHITNOT;
    you.prev_grd_targ.reset();

    // We clear twice - on save and on load.
    // Once would be enough...
    if (make_changes)
        _clear_clouds();

    // Lose all listeners.
    dungeon_events.clear();

    // This block is to grab followers and save the old level to disk.
    if (load_mode == LOAD_ENTER_LEVEL && old_level != -1)
    {
        _grab_followers();

        if (old_level_type == LEVEL_DUNGEON
            || old_level_type != you.level_type)
        {
            _save_level( old_level, old_level_type, old_branch );
        }
    }

    if (make_changes)
        _do_lost_items(old_level_type);

#ifdef USE_TILE
    if (load_mode != LOAD_VISITOR)
    {
        tiles.clear_minimap();
        tiles.load_dungeon(NULL, crawl_view.vgrdc);
    }
#endif

    // Try to open level savefile.
#ifdef DEBUG_LEVEL_LOAD
    mprf(MSGCH_DIAGNOSTICS, "Try to open file %s", cha_fil.c_str());
#endif
    FILE *levelFile = fopen(cha_fil.c_str(), "rb");

    // GENERATE new level when the file can't be opened:
    if (levelFile == NULL)
    {
#ifdef DEBUG_LEVEL_LOAD
        mpr("Generating new file...", MSGCH_DIAGNOSTICS);
#endif
        ASSERT( load_mode != LOAD_VISITOR );
        env.turns_on_level = -1;

        if (you.char_direction == GDT_GAME_START
            && you.level_type == LEVEL_DUNGEON)
        {
            // If we're leaving the Abyss for the first time as a Chaos
            // Knight of Lugonu (who start out there), force a return
            // into the first dungeon level and enable normal monster
            // generation.
            you.your_level = 0;
            you.char_direction = GDT_DESCENDING;
            Generated_Levels.insert(level_id::current());
        }

#ifdef USE_TILE
        tile_init_default_flavour();
        tile_clear_flavour();
#endif

        _clear_env_map();
        builder(you.your_level, you.level_type);
        just_created_level = true;

        if ((you.your_level > 1 || you.level_type != LEVEL_DUNGEON)
            && one_chance_in(3))
        {
            load_ghost(true);
        }
        env.turns_on_level = 0;
        // sanctuary
        env.sanctuary_pos  = coord_def(-1, -1);
        env.sanctuary_time = 0;
    }
    else
    {
#ifdef DEBUG_LEVEL_LOAD
        mpr("Loading old file...", MSGCH_DIAGNOSTICS);
#endif
        // BEGIN -- must load the old level : pre-load tasks

        // LOAD various tags
        char majorVersion;
        char minorVersion;

        std::string reason;
        if (!_get_and_validate_version( levelFile, majorVersion, minorVersion,
                                        &reason ))
        {
            end(-1, false, "\nLevel file is invalid.  %s\n",
                reason.c_str());
        }

        _restore_tagged_file(levelFile, TAGTYPE_LEVEL, minorVersion);

        // Sanity check - EOF
        if (!feof( levelFile ))
        {
            end(-1, false, "\nIncomplete read of \"%s\" - aborting.\n",
                cha_fil.c_str());
        }

        fclose( levelFile );

        // POST-LOAD tasks :
        link_items();
        _redraw_all();
    }

    if (load_mode == LOAD_START_GAME)
        just_created_level = true;

    // Closes all the gates if you're on the way out.
    if (you.char_direction == GDT_ASCENDING
        && you.level_type != LEVEL_PANDEMONIUM)
    {
        _close_level_gates();
    }

    // Here's the second cloud clearing, on load (see above).
    if (make_changes)
    {
        _clear_clouds();
        if (you.level_type != LEVEL_ABYSS)
            _place_player_on_stair(old_level_type, old_branch, stair_taken);
        else
            you.moveto(coord_def(45, 35)); // FIXME: should be abyss_center
    }
    crawl_view.set_player_at(you.pos(), load_mode != LOAD_VISITOR);

    // This should fix the "monster occurring under the player" bug?
    if (make_changes && monster_at(you.pos()))
        monster_teleport(monster_at(you.pos()), true, true);

    // Actually "move" the followers if applicable.
    if (level_type_allows_followers(you.level_type)
        && load_mode == LOAD_ENTER_LEVEL)
    {
        place_followers();
    }

    // Load monsters in transit.
    if (load_mode == LOAD_ENTER_LEVEL)
    {
        place_transiting_monsters();
        place_transiting_items();
    }

    if (load_mode != LOAD_VISITOR)
    {
        // Tell stash-tracker and travel that we've changed levels.
        trackers_init_new_level(true);
#ifdef USE_TILE
        TileNewLevel(just_created_level);
#endif
    }

    _redraw_all();

    if (load_mode != LOAD_VISITOR)
        dungeon_events.fire_event(DET_ENTERING_LEVEL);

    // Things to update for player entering level
    if (load_mode == LOAD_ENTER_LEVEL)
    {
        if (just_created_level)
            level_welcome_messages();

        // Activate markers that want activating, but only when
        // entering a new level in an existing game. If we're starting
        // a new game, or reloading an existing game,
        // markers are activated in acr.cc.
        env.markers.activate_all();

        // Draw view window. update_level may cause monsters to
        // act and hence be redrawn.
        viewwindow(false, true);

        // update corpses and fountains
        if (env.elapsed_time != 0.0 && !just_created_level)
            update_level( you.elapsed_time - env.elapsed_time );

        // Centaurs have difficulty with stairs
        int timeval = ((you.species != SP_CENTAUR) ? player_movement_speed()
                                                   : 15);

        // new levels have less wary monsters:
        if (just_created_level)
            timeval /= 2;

        timeval -= (stepdown_value( check_stealth(), 50, 50, 150, 150 ) / 10);

        dprf("arrival time: %d", timeval );

        if (timeval > 0)
        {
            you.time_taken = timeval;
            handle_monsters();
        }
    }

    // Save the created/updated level out to disk:
    if (make_changes)
        _save_level( you.your_level, you.level_type, you.where_are_you );

    setup_environment_effects();

    setup_vault_mon_list();
    setup_feature_descs_short();

    // Inform user of level's annotation.
    if (load_mode != LOAD_VISITOR
        && !get_level_annotation().empty()
        && !crawl_state.level_annotation_shown)
    {
        mprf(MSGCH_PLAIN, YELLOW, "Level annotation: %s",
             get_level_annotation().c_str());
    }

    if (load_mode != LOAD_VISITOR)
        crawl_state.level_annotation_shown = false;

    if (make_changes)
    {
        // Update PlaceInfo entries
        PlaceInfo& curr_PlaceInfo = you.get_place_info();
        PlaceInfo  delta;

        if (load_mode == LOAD_START_GAME
            || (load_mode == LOAD_ENTER_LEVEL
                && (old_branch != you.where_are_you
                    || old_level_type != you.level_type)))
        {
            delta.num_visits++;
        }

        if (just_created_level)
            delta.levels_seen++;

        you.global_info += delta;
#ifdef DEBUG_LEVEL_LOAD
        mprf(MSGCH_DIAGNOSTICS,
             "global_info:: num_visits: %d, levels_seen: %d",
             you.global_info.num_visits, you.global_info.levels_seen);
#endif
        you.global_info.assert_validity();

        curr_PlaceInfo += delta;
#ifdef DEBUG_LEVEL_LOAD
        mprf(MSGCH_DIAGNOSTICS,
             "curr_PlaceInfo:: num_visits: %d, levels_seen: %d",
             curr_PlaceInfo.num_visits, curr_PlaceInfo.levels_seen);
#endif
        curr_PlaceInfo.assert_validity();
    }

    if (just_created_level)
        you.attribute[ATTR_ABYSS_ENTOURAGE] = 0;

    if (load_mode != LOAD_VISITOR)
        dungeon_events.fire_event(DET_ENTERED_LEVEL);

    if (load_mode == LOAD_ENTER_LEVEL)
    {
        // 50% chance of repelling the stair you just came through.
        if (you.duration[DUR_REPEL_STAIRS_MOVE]
            || you.duration[DUR_REPEL_STAIRS_CLIMB])
        {
            dungeon_feature_type feat = grd(you.pos());
            if (feat != DNGN_ENTER_SHOP
                && feat_stair_direction(feat) != CMD_NO_CMD
                && feat_stair_direction(stair_taken) != CMD_NO_CMD)
            {
                std::string stair_str =
                    feature_description(feat, NUM_TRAPS, false,
                                        DESC_CAP_THE, false);
                std::string verb = stair_climb_verb(feat);

                if (coinflip()
                    && slide_feature_over(you.pos(), coord_def(-1, -1), false))
                {
                    mprf("%s slides away from you right after you %s it!",
                         stair_str.c_str(), verb.c_str());
                }

                if (coinflip())
                {
                    // Stairs stop fleeing from you now you actually caught one.
                    mprf("%s settles down.", stair_str.c_str(), verb.c_str());
                    you.duration[DUR_REPEL_STAIRS_MOVE]  = 0;
                    you.duration[DUR_REPEL_STAIRS_CLIMB] = 0;
                }
            }
        }

        // If butchering was interrupted by switching levels (banishment)
        // then switch back from butchering tool if there's no hostiles
        // nearby.
        handle_interrupted_swap(true);

        // Forget about interrupted butchering, since we probably aren't going
        // to get back to the corpse in time to finish things.
        // But do not reset the weapon swap if we swapped weapons
        // because of a transformation.
        maybe_clear_weapon_swap();
    }

    return just_created_level;
}

void _save_level(int level_saved, level_area_type old_ltype,
                 branch_type where_were_you)
{
    std::string cha_fil = make_filename( you.your_name, level_saved,
                                         where_were_you, old_ltype,
                                         false );

    you.prev_targ     = MHITNOT;
    you.prev_grd_targ.reset();

    FILE *saveFile = fopen(cha_fil.c_str(), "wb");

    if (saveFile == NULL)
    {
        end(-1, true, "Unable to open \"%s\" for writing",
                 cha_fil.c_str());
    }

    // Nail all items to the ground.
    fix_item_coordinates();

    _write_tagged_file( saveFile, TAGTYPE_LEVEL );

    fclose(saveFile);

    DO_CHMOD_PRIVATE(cha_fil.c_str());
}

// Stack allocated std::string's go in seperate function, so Valgrind doesn't
// complain.
static void _save_game_base()
{
    /* Stashes */
    std::string stashFile = get_savedir_filename(you.your_name, "", "st");
    FILE *stashf = fopen(stashFile.c_str(), "wb");
    if (stashf)
    {
        writer outf(stashf);
        StashTrack.save(outf);
        fclose(stashf);
        DO_CHMOD_PRIVATE(stashFile.c_str());
    }

#ifdef CLUA_BINDINGS
    /* lua */
    std::string luaFile = get_savedir_filename(you.your_name, "", "lua");
    clua.save(luaFile.c_str());
    // Note that luaFile may not exist.
    DO_CHMOD_PRIVATE(luaFile.c_str());
#endif

    /* kills */
    std::string killFile = get_savedir_filename(you.your_name, "", "kil");
    FILE *killf = fopen(killFile.c_str(), "wb");
    if (killf)
    {
        writer outf(killf);
        you.kills->save(outf);
        fclose(killf);
        DO_CHMOD_PRIVATE(killFile.c_str());
    }

    /* travel cache */
    std::string travelCacheFile = get_savedir_filename(you.your_name,"","tc");
    FILE *travelf = fopen(travelCacheFile.c_str(), "wb");
    if (travelf)
    {
        writer outf(travelf);
        travel_cache.save(outf);
        fclose(travelf);
        DO_CHMOD_PRIVATE(travelCacheFile.c_str());
    }

    /* notes */
    std::string notesFile = get_savedir_filename(you.your_name, "", "nts");
    FILE *notesf = fopen(notesFile.c_str(), "wb");
    if (notesf)
    {
        writer outf(notesf);
        save_notes(outf);
        fclose(notesf);
        DO_CHMOD_PRIVATE(notesFile.c_str());
    }

    /* tutorial */
    std::string tutorFile = get_savedir_filename(you.your_name, "", "tut");
    FILE *tutorf = fopen(tutorFile.c_str(), "wb");
    if (tutorf)
    {
        writer outf(tutorf);
        save_tutorial(outf);
        fclose(tutorf);
        DO_CHMOD_PRIVATE(tutorFile.c_str());
    }

    /* messages */
    std::string msgFile = get_savedir_filename(you.your_name, "", "msg");
    FILE *msgf = fopen(msgFile.c_str(), "wb");
    if (msgf)
    {
        writer outf(msgf);
        save_messages(outf);
        fclose(msgf);
        DO_CHMOD_PRIVATE(msgFile.c_str());
    }

    /* tile dolls (empty for ASCII)*/
    std::string dollFile = get_savedir_filename(you.your_name, "", "tdl");
#ifdef USE_TILE
    // Save the current equipment into a file.
    FILE *dollf = fopen(dollFile.c_str(), "w+");
    if (dollf)
    {
        save_doll_file(dollf);
        fclose(dollf);
        DO_CHMOD_PRIVATE(dollFile.c_str());
    }
#endif

    std::string charFile = get_savedir_filename(you.your_name, "", "sav");
    FILE *charf = fopen(charFile.c_str(), "wb");
    if (!charf)
        end(-1, true, "Unable to open \"%s\" for writing!\n", charFile.c_str());

    _write_tagged_file( charf, TAGTYPE_PLAYER );

    fclose(charf);
    DO_CHMOD_PRIVATE(charFile.c_str());
}

// Stack allocated std::string's go in seperate function, so Valgrind doesn't
// complain.
static void _save_game_exit()
{
    // Must be exiting -- save level & goodbye!
    if (!you.entering_level)
        _save_level(you.your_level, you.level_type, you.where_are_you);

    clrscr();

#ifdef SAVE_PACKAGE_CMD
    std::string dirname = get_savedir();
    escape_path_spaces(dirname);
    std::string basename = get_save_filename(you.your_name, "", "");
    escape_path_spaces(basename);

    char cmd_buff[1024];

#ifdef USE_TAR
    snprintf( cmd_buff, sizeof(cmd_buff),
              "cd %s && "SAVE_PACKAGE_CMD" -zcf %s"PACKAGE_SUFFIX" --remove-files %s.*",
              dirname.c_str(), basename.c_str(), basename.c_str());
#else
# ifdef USE_ZIP
    snprintf( cmd_buff, sizeof(cmd_buff),
              SAVE_PACKAGE_CMD" %s"PACKAGE_SUFFIX" %s.*",
              (dirname+basename).c_str(), (dirname+basename).c_str());
# else
    No save package defined.
#endif
#endif

    if (system( cmd_buff ) != 0)
    {
        cprintf( EOL "Warning: Zip command (SAVE_PACKAGE_CMD) returned "
                     "non-zero value!" EOL );
    }
    DO_CHMOD_PRIVATE ( (dirname + basename + PACKAGE_SUFFIX).c_str() );
#endif

#ifdef DGL_WHEREIS
    whereis_record("saved");
#endif

    if (_callback_list != NULL)
    {
        delete _callback_list;
        _callback_list = NULL;
    }
}

void save_game(bool leave_game, const char *farewellmsg)
{
    unwind_bool saving_game(crawl_state.saving_game, true);

    SavefileCallback::pre_save();

    // Stack allocated std::string's go in seperate function,
    // so Valgrind doesn't complain.
    _save_game_base();

    // If just save, early out.
    if (!leave_game)
        return;

    // Stack allocated std::string's go in seperate function,
    // so Valgrind doesn't complain.
    _save_game_exit();

    end(0, false, farewellmsg? "%s" : "See you soon, %s!",
        farewellmsg? farewellmsg : you.your_name.c_str());
}                               // end save_game()

// Saves the game without exiting.
void save_game_state()
{
    save_game(false);
    if (crawl_state.seen_hups)
        save_game(true);
}

static std::string _make_portal_vault_ghost_suffix()
{
    return you.level_type_ext.empty()? "ptl" : you.level_type_ext;
}

static std::string _make_ghost_filename()
{
    if (you.level_type == LEVEL_PORTAL_VAULT)
    {
        const std::string suffix = _make_portal_vault_ghost_suffix();
        return get_savedir_filename("bones", "", suffix, true);
    }
    else
    {
        return make_filename("bones", you.your_level, you.where_are_you,
                             you.level_type, true);
    }
}

#define BONES_DIAGNOSTICS (WIZARD | DEBUG_BONES | DEBUG_DIAGNOSTICS)

bool load_ghost(bool creating_level)
{
    const bool wiz_cmd = (crawl_state.prev_cmd == CMD_WIZARD);

    ASSERT(you.transit_stair == DNGN_UNSEEN || creating_level);
    ASSERT(!you.entering_level || creating_level);
    ASSERT(!creating_level
           || (you.entering_level && you.transit_stair != DNGN_UNSEEN));
    // Only way to load a ghost without creating a level is via a wizard
    // command.
    ASSERT(creating_level || wiz_cmd);

#if !DEBUG && !WIZARD
    UNUSED(creating_level);
#endif

#if BONES_DIAGNOSTICS

    bool do_diagnostics = false;
#if WIZARD
    do_diagnostics = !creating_level;
#endif
#if DEBUG_BONES | DEBUG_DIAGNOSTICS
    do_diagnostics = true;
#endif

#endif // BONES_DIAGNOSTICS

    char majorVersion;
    char minorVersion;

    const std::string cha_fil = _make_ghost_filename();
    FILE *gfile = fopen(cha_fil.c_str(), "rb");

    if (gfile == NULL)
    {
        if (wiz_cmd && !creating_level)
            mpr("No ghost files for this level.", MSGCH_PROMPT);
        return (false);                 // no such ghost.
    }

    if (!_determine_ghost_version(gfile, majorVersion, minorVersion))
    {
        fclose(gfile);
#if BONES_DIAGNOSTICS
        if (do_diagnostics)
        {
            mprf(MSGCH_DIAGNOSTICS,
                 "Ghost file \"%s\" seems to be invalid.", cha_fil.c_str());
            more();
        }
#endif
        return (false);
    }

    if (majorVersion != TAG_MAJOR_VERSION || minorVersion > TAG_MINOR_VERSION)
    {
#if BONES_DIAGNOSTICS
        if (do_diagnostics)
        {
            if (majorVersion != TAG_MAJOR_VERSION)
                mprf(MSGCH_DIAGNOSTICS,
                     "Ghost file major version mismatch");
            else
                mprf(MSGCH_DIAGNOSTICS,
                     "Ghost file minor version is too high.");
            more();
        }
#endif
        fclose(gfile);
        unlink(cha_fil.c_str());
        return (false);
    }

    ghosts.clear();
    _restore_ghost_version(gfile, majorVersion, minorVersion);

    // Sanity check - EOF.
    if (!feof(gfile))
    {
        fclose(gfile);
#if BONES_DIAGNOSTICS
        if (do_diagnostics)
        {
            mprf(MSGCH_DIAGNOSTICS, "Incomplete read of \"%s\".",
                 cha_fil.c_str() );
            more();
        }
#endif
        return (false);
    }

    fclose(gfile);

    // FIXME: This message will have to be shortened again as trunk reaches
    //        0.6 state and players using old bones becomes increasingly less
    //        likely.
    if (!debug_check_ghosts())
    {
        mprf(MSGCH_DIAGNOSTICS,
             "Refusing to load buggy ghost from file \"%s\"! "
             "Note that all bones files from 0.4.x are invalid, so you should "
             "delete them. If this is a newer ghost, please submit a bug "
             "report.",
             cha_fil.c_str());

        return (false);
    }

#if BONES_DIAGNOSTICS
    if (do_diagnostics)
    {
        mprf(MSGCH_DIAGNOSTICS, "Loaded ghost file with %lu ghost(s)",
             ghosts.size());
    }
#endif

    // Remove bones file - ghosts are hardly permanent.
    unlink(cha_fil.c_str());

#if BONES_DIAGNOSTICS
    unsigned long unplaced_ghosts = ghosts.size();
    bool          ghost_errors    = false;
#endif

    // Translate ghost to monster and place.
    for (int imn = 0; imn < MAX_MONSTERS - 10 && !ghosts.empty(); ++imn)
    {
        if (menv[imn].alive())
            continue;

        menv[imn].set_ghost(ghosts[0]);
        menv[imn].ghost_init();

        ghosts.erase(ghosts.begin());
#if BONES_DIAGNOSTICS
        if (do_diagnostics)
        {
            unplaced_ghosts--;
            if (!menv[imn].alive())
            {
                mpr("Placed ghost is not alive.", MSGCH_DIAGNOSTICS);
                ghost_errors = true;
            }
            else if (menv[imn].type != MONS_PLAYER_GHOST)
            {
                mprf(MSGCH_DIAGNOSTICS,
                     "Placed ghost is not MONS_PLAYER_GHOST, but %s",
                     menv[imn].name(DESC_PLAIN, true).c_str());
                ghost_errors = true;
            }
        }
#endif
    }

#if BONES_DIAGNOSTICS
    if (do_diagnostics && unplaced_ghosts > 0)
    {
        mprf(MSGCH_DIAGNOSTICS, "Unable to place %lu ghost(s)",
             ghosts.size());
        ghost_errors = true;
    }
    if (ghost_errors)
        more();
#endif

    return (true);
}

void restore_game(void)
{
    std::string charFile = get_savedir_filename(you.your_name, "", "sav");
    FILE *charf = fopen(charFile.c_str(), "rb");
    if (!charf )
        end(-1, true, "Unable to open %s for reading!\n", charFile.c_str() );

    char majorVersion;
    char minorVersion;
    std::string reason;
    if (!_get_and_validate_version(charf, majorVersion, minorVersion, &reason))
        end(-1, false, "\nSave file %s is invalid.  %s\n", charFile.c_str(), reason.c_str());

    _restore_tagged_file(charf, TAGTYPE_PLAYER, minorVersion);

    // Sanity check - EOF
    if (!feof(charf))
    {
        end(-1, false, "\nIncomplete read of \"%s\" - aborting.\n",
            charFile.c_str());
    }
    fclose(charf);

    std::string stashFile = get_savedir_filename( you.your_name, "", "st" );
    FILE *stashf = fopen(stashFile.c_str(), "rb");
    if (stashf)
    {
        reader inf(stashf, minorVersion);
        StashTrack.load(inf);
        fclose(stashf);
    }

#ifdef CLUA_BINDINGS
    std::string luaFile = get_savedir_filename( you.your_name, "", "lua" );
    clua.execfile( luaFile.c_str() );
#endif

    std::string killFile = get_savedir_filename( you.your_name, "", "kil" );
    FILE *killf = fopen(killFile.c_str(), "rb");
    if (killf)
    {
        reader inf(killf, minorVersion);
        you.kills->load(inf);
        fclose(killf);
    }

    std::string travelCacheFile = get_savedir_filename(you.your_name,"","tc");
    FILE *travelf = fopen(travelCacheFile.c_str(), "rb");
    if (travelf)
    {
        reader inf(travelf, minorVersion);
        travel_cache.load(inf, minorVersion);
        fclose(travelf);
    }

    std::string notesFile = get_savedir_filename(you.your_name, "", "nts");
    FILE *notesf = fopen(notesFile.c_str(), "rb");
    if (notesf)
    {
        reader inf(notesf, minorVersion);
        load_notes(inf);
        fclose(notesf);
    }

    /* tutorial */
    std::string tutorFile = get_savedir_filename(you.your_name, "", "tut");
    FILE *tutorf = fopen(tutorFile.c_str(), "rb");
    if (tutorf)
    {
        reader inf(tutorf, minorVersion);
        load_tutorial(inf);
        fclose(tutorf);
    }

    /* messages */
    std::string msgFile = get_savedir_filename(you.your_name, "", "msg");
    FILE *msgf = fopen(msgFile.c_str(), "rb");
    if (msgf)
    {
        reader inf(msgf, minorVersion);
        load_messages(inf);
        fclose(msgf);
    }

    SavefileCallback::post_restore();
}

static void _restore_level(const level_id &original)
{
    // Reload the original level.
    you.where_are_you = original.branch;
    you.your_level = original.dungeon_absdepth();
    you.level_type = original.level_type;

    load( DNGN_STONE_STAIRS_DOWN_I, LOAD_VISITOR,
          you.level_type, you.your_level, you.where_are_you );

    // Rebuild the show grid, which was cleared out before.
    you.update_los();
}

// Given a level returns true if the level has been created already
// in this game.
bool is_existing_level(const level_id &level)
{
    return (Generated_Levels.find(level) != Generated_Levels.end());
}

// This class provides a way to walk the dungeon with a bit more flexibility
// than you get with apply_to_all_dungeons.
level_excursion::level_excursion()
    : original(level_id::current())
{
}

void level_excursion::go_to(const level_id& next)
{
    if (level_id::current() != next)
    {
        _save_level(you.your_level, you.level_type, you.where_are_you);
        _restore_level(next);

        LevelInfo &li = travel_cache.get_level_info(next);
        li.set_level_excludes();
    }

    you.on_current_level = (level_id::current() == original);
}

level_excursion::~level_excursion()
{
    go_to(original);
}


// Applies an operation (applicator) after switching to the specified level.
// Will reload the original level after modifying the target level.
//
// If the target level has not already been visited by the player, this
// function will assert.
bool apply_to_level(const level_id &level, bool (*applicator)())
{
    ASSERT(is_existing_level(level));

    level_excursion le;

    le.go_to(level);

    return applicator();
}

bool apply_to_all_dungeons(bool (*applicator)())
{
    level_excursion le;
    level_id original(level_id::current());

    bool success = applicator();

    for (int i = 0; i < MAX_LEVELS; ++i)
        for (int j = 0; j < NUM_BRANCHES; ++j)
        {
            const branch_type br = static_cast<branch_type>(j);
            const level_id thislevel(br, subdungeon_depth(br, i));

            if (!is_existing_level(thislevel))
                continue;

            // Don't apply to the original level - already done up top.
            if (original == thislevel)
                continue;

            le.go_to(thislevel);

            if (applicator())
                success = true;
        }

    return (success);
}

bool get_save_version(FILE *file, char &major, char &minor)
{
    // Read first two bytes.
    char buf[2];
    if (read2(file, buf, 2) != 2)
    {
        // Empty file?
        major = minor = -1;
        return (false);
    }

    major = buf[0];
    minor = buf[1];

    return (true);
}

static bool _get_and_validate_version(FILE *restoreFile, char &major,
                                      char &minor, std::string* reason)
{
    std::string dummy;
    if (reason == 0)
        reason = &dummy;

    if (!get_save_version(restoreFile, major, minor))
    {
        *reason = "File is corrupt.";
        return (false);
    }

    if (major != TAG_MAJOR_VERSION)
    {
        *reason = make_stringf("Major version mismatch: %d (want %d).",
                               major, TAG_MAJOR_VERSION);
        return (false);
    }

    if (minor < 0)
    {
        *reason = make_stringf("Minor version %d is negative!",
                               minor);
        return (false);
    }

    if (minor > TAG_MINOR_VERSION)
    {
        *reason = make_stringf("Minor version mismatch: %d (want <= %d).",
                               minor, TAG_MINOR_VERSION);
        return (false);
    }

    return (true);
}

static void _restore_tagged_file( FILE *restoreFile, int fileType,
                                  char minorVersion )
{
    char tags[NUM_TAGS];
    tag_set_expected(tags, fileType);

    while (true)
    {
        tag_type tt = tag_read(restoreFile, minorVersion);
        if (tt == TAG_NO_TAG)
            break;

        tags[tt] = 0;                // tag read
        if (fileType == TAGTYPE_PLAYER_NAME)
            break;
    }

    // Go through and init missing tags.
    for (int i = 0; i < NUM_TAGS; i++)
        if (tags[i] == 1)           // expected but never read
            tag_missing(i, minorVersion);
}

static bool _determine_ghost_version( FILE *ghostFile,
                                      char &majorVersion, char &minorVersion )
{
    // Read first two bytes.
    char buf[2];
    if (read2(ghostFile, buf, 2) != 2)
        return (false);               // empty file?

    // Otherwise, read version and validate.
    majorVersion = buf[0];
    minorVersion = buf[1];

    reader inf(ghostFile, minorVersion);
    // Check for the DCSS ghost signature.
    if (unmarshallShort(inf) != GHOST_SIGNATURE)
        return (false);

    if (majorVersion == TAG_MAJOR_VERSION
        && minorVersion <= TAG_MINOR_VERSION)
    {
        // Discard three more 32-bit words of padding.
        inf.read(NULL, 3*4);
        return !feof(ghostFile);
    }

    // If its not TAG_MAJOR_VERSION, no idea!
    return (false);
}

static void _restore_ghost_version( FILE *ghostFile,
                                    char majorVersion, char minorVersion )
{
    switch (majorVersion)
    {
    case TAG_MAJOR_VERSION:
        _restore_tagged_file(ghostFile, TAGTYPE_GHOST, minorVersion);
        break;
    default:
        break;
    }
}

void save_ghost( bool force )
{
#if BONES_DIAGNOSTICS

    bool do_diagnostics = false;
#if WIZARD
    do_diagnostics = you.wizard;
#endif
#if DEBUG_BONES | DEBUG_DIAGNOSTICS
    do_diagnostics = true;
#endif

#endif // BONES_DIAGNOSTICS

    // No ghosts on levels 1, 2, or the ET.
    if (!force && (you.your_level < 2
                   || you.where_are_you == BRANCH_ECUMENICAL_TEMPLE))
    {
        return;
    }

    const std::string cha_fil = _make_ghost_filename();
    FILE *gfile = fopen(cha_fil.c_str(), "rb");

    // Don't overwrite existing bones!
    if (gfile != NULL)
    {
#if BONES_DIAGNOSTICS
        if (do_diagnostics)
            mpr("Ghost file for this level already exists.",
                MSGCH_DIAGNOSTICS);
#endif
        fclose(gfile);
        return;
    }

    ghosts = ghost_demon::find_ghosts();

    if (ghosts.empty())
    {
#if BONES_DIAGNOSTICS
        if (do_diagnostics)
            mpr("Could not find any ghosts for this level.",
                MSGCH_DIAGNOSTICS);
#endif
         return;
    }

    gfile = lk_open("wb", cha_fil);

    if (gfile == NULL)
    {
        mprf(MSGCH_ERROR, "Error creating ghost file: %s", cha_fil.c_str());
        more();
        return;
    }

    _write_tagged_file( gfile, TAGTYPE_GHOST, true );

    lk_close(gfile, "wb", cha_fil);

#if BONES_DIAGNOSTICS
    if (do_diagnostics)
        mprf(MSGCH_DIAGNOSTICS, "Saved ghost (%s).", cha_fil.c_str() );
#endif

    DO_CHMOD_PRIVATE(cha_fil.c_str());
}

////////////////////////////////////////////////////////////////////////////
// Locking for multiuser systems

// first, some file locking stuff for multiuser crawl
#ifdef USE_FILE_LOCKING

bool lock_file_handle( FILE *handle, int type )
{
    struct flock  lock;
    int           status;

    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_type = type;

#ifdef USE_BLOCKING_LOCK
    status = fcntl( fileno( handle ), F_SETLKW, &lock );
#else
    for (int i = 0; i < 30; i++)
    {
        status = fcntl( fileno( handle ), F_SETLK, &lock );

        // success
        if (status == 0)
            break;

        // known failure
        if (status == -1 && (errno != EACCES && errno != EAGAIN))
            break;

        perror( "Problems locking file... retrying..." );
        delay( 1000 );
    }
#endif

    return (status == 0);
}

bool unlock_file_handle( FILE *handle )
{
    struct flock  lock;
    int           status;

    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_type = F_UNLCK;

#ifdef USE_BLOCKING_LOCK

    status = fcntl( fileno( handle ), F_SETLKW, &lock );

#else

    for (int i = 0; i < 30; i++)
    {
        status = fcntl( fileno( handle ), F_SETLK, &lock );

        // success
        if (status == 0)
            break;

        // known failure
        if (status == -1 && (errno != EACCES && errno != EAGAIN))
            break;

        perror( "Problems unlocking file... retrying..." );
        delay( 1000 );
    }

#endif

    return (status == 0);
}

#endif

FILE *lk_open(const char *mode, const std::string &file)
{
    FILE *handle = fopen(file.c_str(), mode);
#ifdef SHARED_FILES_CHMOD_PUBLIC
    chmod(file.c_str(), SHARED_FILES_CHMOD_PUBLIC);
#endif

#ifdef USE_FILE_LOCKING
    int locktype = F_RDLCK;
    if (mode && mode[0] != 'r')
        locktype = F_WRLCK;

    if (handle && !lock_file_handle( handle, locktype ))
    {
        perror( "Could not lock file... " );
        fclose( handle );
        handle = NULL;
    }
#endif
    return handle;
}

void lk_close(FILE *handle, const char *mode, const std::string &file)
{
    UNUSED( mode );

    if (handle == NULL || handle == stdin)
        return;

#ifdef USE_FILE_LOCKING
    unlock_file_handle( handle );
#endif

    // actually close
    fclose(handle);

#ifdef SHARED_FILES_CHMOD_PUBLIC
    if (mode && mode[0] == 'w')
        chmod(file.c_str(), SHARED_FILES_CHMOD_PUBLIC);
#endif
}

/////////////////////////////////////////////////////////////////////////////
// file_lock
//
// Locks a named file (usually an empty lock file), creating it if necessary.

file_lock::file_lock(const std::string &s, const char *_mode, bool die_on_fail)
    : handle(NULL), mode(_mode), filename(s)
{
#ifdef USE_FILE_LOCKING
    if (!(handle = lk_open(mode, filename)) && die_on_fail)
        end(1, true, "Unable to open lock file \"%s\"", filename.c_str());
#endif
}

file_lock::~file_lock()
{
#ifdef USE_FILE_LOCKING
    if (handle)
        lk_close(handle, mode, filename);
#endif
}

/////////////////////////////////////////////////////////////////////////////
// SavefileCallback
//
// Callbacks which are called before a save and after a restore.  Can be used
// to move stuff in and out of you.props, or on a restore to recalculate data
// which isn't stored in the savefile.  Declare a SavefileCallback variable
// using a C++ global constructor to register the callback.
//
// XXX: Due to some weirdness with C++ global constructors (see below) I'm
// not sure if this will work for all compiler/system combos, so make any
// code which uses this fail gracefully if the callbacks aren't called.

SavefileCallback::SavefileCallback(callback func)
{
    ASSERT(func != NULL);

    // XXX: For some reason (at least with GCC 4.3.2 on Linux) if the
    // callback list is made with a global contructor then it gets emptied
    // out by the time that pre_save() or post_restore() is called,
    // probably having something to do with the fact that global
    // contructors are also used to add the callbacks.  Thus we have to do
    // it this way.
    if (_callback_list == NULL)
        _callback_list = new std::vector<SavefileCallback::callback>();

    _callback_list->push_back(func);
}

void SavefileCallback::pre_save()
{
    ASSERT(crawl_state.saving_game);

    if (_callback_list == NULL)
        return;

    for (unsigned int i = 0; i < _callback_list->size(); i++)
    {
        callback func = (*_callback_list)[i];
        (*func)(true);
    }
}

void SavefileCallback::post_restore()
{
    ASSERT(!crawl_state.saving_game);

    if (_callback_list == NULL)
        return;

    for (unsigned int i = 0; i < _callback_list->size(); i++)
    {
        callback func = (*_callback_list)[i];
        (*func)(false);
    }
}
