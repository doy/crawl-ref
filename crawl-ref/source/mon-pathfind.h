#ifndef MON_PATHFIND_H
#define MON_PATHFIND_H

class monsters;

int mons_tracking_range(const monsters *mon);

class monster_pathfind
{
public:
    monster_pathfind();
    virtual ~monster_pathfind();

    // public methods
    void set_range(int r);
    coord_def next_pos(const coord_def &p) const;
    bool init_pathfind(const monsters *mon, coord_def dest,
                       bool diag = true, bool msg = false,
                       bool pass_unmapped = false);
    bool init_pathfind(coord_def src, coord_def dest,
                       bool diag = true, bool msg = false);
    bool start_pathfind(bool msg = false);
    std::vector<coord_def> backtrack(void);
    std::vector<coord_def> calc_waypoints(void);

protected:
    // protected methods
    bool calc_path_to_neighbours(void);
    bool traversable(coord_def p);
    int  travel_cost(coord_def npos);
    bool mons_traversable(coord_def p);
    int  mons_travel_cost(coord_def npos);
    int  estimated_cost(coord_def npos);
    void add_new_pos(coord_def pos, int total);
    void update_pos(coord_def pos, int total);
    bool get_best_position(void);


    // The monster trying to find a path.
    const monsters *mons;

    // Our destination, and the current position we're looking at.
    coord_def start, target, pos;

    // If false, do not move diagonally along the path.
    bool allow_diagonals;

    // If true, unmapped terrain is treated as traversable no matter the
    // monster involved.
    // (Used for player estimates of whether a monster can travel somewhere.)
    bool traverse_unmapped;

    // Maximum range to search between start and target. None, if zero.
    int range;

    // Currently shortest and longest possible total length of the path.
    int min_length;
    int max_length;

    // The array of distances from start to any already tried point.
    int dist[GXM][GYM];
    // An array to store where we came from on a given shortest path.
    int prev[GXM][GYM];

    FixedVector<std::vector<coord_def>, GXM * GYM> hash;
};

#endif

