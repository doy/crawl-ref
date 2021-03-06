/*
 *  File:       message.cc
 *  Summary:    Functions used to print messages.
 *  Written by: Linley Henzell
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>
#include <streambuf>
#include <iostream>

#include "enum.h"
#include "mpr.h"

void mesclr(bool force = false);

void flush_prev_message();

void more(bool user_forced = false);


class formatted_string;

void formatted_mpr(const formatted_string& fs,
                   msg_channel_type channel = MSGCH_PLAIN, int param = 0);

void formatted_message_history(const std::string &st,
                               msg_channel_type channel = MSGCH_PLAIN,
                               int param = 0,
                               int wrap_col = 0);

// mpr() an arbitrarily long list of strings
void mpr_comma_separated_list(const std::string prefix,
                              const std::vector<std::string> list,
                              const std::string &andc = ", and ",
                              const std::string &comma = ", ",
                              const msg_channel_type channel = MSGCH_PLAIN,
                              const int param = 0);

class no_messages
{
public:
    no_messages();
    ~no_messages();
private:
    bool msuppressed;
};

void save_messages(writer& outf);
void load_messages(reader& inf);

bool any_messages();
void replay_messages();

void set_colour(char set_message_colour);

void set_more_autoclear(bool on);

std::string get_last_messages(int mcount);

std::vector<std::string> get_recent_messages(int &message_pos,
                                             bool dumpworthy_only = true,
                                             std::vector<int> *channels = NULL);

int channel_to_colour( msg_channel_type channel, int param = 0 );

namespace msg
{
    extern std::ostream stream;
    std::ostream& streams(msg_channel_type chan = MSGCH_PLAIN);

    struct setparam
    {
        setparam(int param);
        int m_param;
    };

    struct mute
    {
        mute(bool value = true);
        bool m_value;
    };

    class mpr_stream_buf : public std::streambuf
    {
    public:
        mpr_stream_buf(msg_channel_type chan);
        virtual ~mpr_stream_buf() {}
        void set_param(int p);
        void set_muted(bool m);
    protected:
        int overflow(int c);
    private:
        static const int INTERNAL_LENGTH = 500;
        char internal_buf[500]; // if your terminal is wider than this, too bad
        int internal_count;
        int param;
        bool muted;
        msg_channel_type channel;
    };

    void initialise_mpr_streams();
    void deinitialise_mpr_streams();
}

std::ostream& operator<<(std::ostream& os, const msg::setparam& sp);

void set_msg_dump_file(FILE* file);

#endif
