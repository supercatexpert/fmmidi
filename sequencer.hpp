#ifndef midisequencer_hpp
#define midisequencer_hpp

#include <stdint.h>
#include <cstdio>
#include <string>
#include <vector>

#define META_EVENT_ALL_NOTE_OFF		0x8888

namespace midisequencer{
    /*
    typedef unsigned long uint_least32_t;
    */
    struct midi_message{
        float time;
        uint_least32_t message;
        int port;
        int track;
    };

    class uncopyable{
    public:
        uncopyable(){}
    private:
        uncopyable(const uncopyable&);
        void operator=(const uncopyable&);
    };

    class output:uncopyable{
    public:
        virtual void midi_message(int port, uint_least32_t message) = 0;
        virtual void sysex_message(int port, const void* data, std::size_t size) = 0;
        virtual void meta_event(int type, const void* data, std::size_t size) = 0;
        virtual void reset() = 0;
    protected:
        ~output(){}
    };

    class sequencer:uncopyable{
    public:
        sequencer();
        void clear();
	void rewind();
        bool load(void* fp, int(*fgetc)(void*));
        bool load(std::FILE* fp);
        int get_num_ports()const;
        float get_total_time()const;
        std::string get_title()const;
        std::string get_copyright()const;
        std::string get_song()const;
        void play(float time, output* out);
	void set_time(float time, output* out);
    private:
        std::vector<midi_message> messages;
        std::vector<midi_message>::iterator position;
        std::vector<std::string> long_messages;
        void load_smf(void* fp, int(*fgetc)(void*));
    };
}

#endif
