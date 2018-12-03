#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include "sequencer.hpp"
#include "midisynth.hpp"

#ifdef USE_BSD_AUDIO
#include <sys/ioctl.h>
#include <sys/audioio.h>
#elif defined(USE_QNX_AUDIO)
#include <sys/asoundlib.h>
#else
#define USE_LIBAO_AUDIO
#include <ao/ao.h>

int driver_id;
#endif

#ifdef FMMIDI_CURSES_H

#include <curses.h>

// never use clear() but wclear(stdscr) in fmmidi
// some curses headers have a define for clear() that turns it 
// into wclear(stdscr), and this can give problems
// as we have a clear() method for the sequencer, too.

#ifdef clear
#warning "You have a Curses header with a define"
#warning "that turns every function called clear in wclear(stdscr)"
#warning "In C++, this is a real problem. A work around will be used."
#undef clear
#endif


WINDOW *cwin;
bool cursesok = true;
#endif

#ifdef FMMIDI_SIGNAL_H
#include <signal.h>
#endif

extern char *optarg;
extern int optind;

//static char **playlist;
static std::vector<const char*> playlist;
static std::map<int, bool> playerr;
static int playlistPos;

static int loopTimes = 0;
static bool loopForever = false;

#ifdef USE_LIBAO_AUDIO
char outname[4096];
char drvname[16];
#elif defined(USE_QNX_AUDIO)
// ...
#elif defined(USE_BSD_AUDIO)
char audevname[4096];
#else
#error "Unsupported sound output."
#endif

#define FMMIDI_VERSION		"1.0.1"

class fmOut : public midisequencer::output
{
	private:
		midisynth::synthesizer *synth;
		midisynth::fm_note_factory *note_factory;
		midisynth::DRUMPARAMETER p;

		void load_programs()
		{
			#include "program.h"
		}

	public:
		fmOut()
		{
			note_factory = new midisynth::fm_note_factory;
			synth = new midisynth::synthesizer(note_factory);
						
			load_programs();

		}
	
		void midi_message(int port, uint_least32_t message)
		{
			synth->midi_event(message);
		}
		
		void sysex_message(int port, const void* data, std::size_t size)
		{
			synth->sysex_message(data, size);
		}
		
		void meta_event(int type, const void* data, std::size_t size)
		{
			if(type == META_EVENT_ALL_NOTE_OFF)
				synth->all_note_off();
		}
		
		int synthesize(int_least16_t* output, std::size_t samples, float rate)
		{
			return synth->synthesize(output, samples, rate);
		}
		
		void set_mode(midisynth::system_mode_t mode)
		{
			synth->set_system_mode(mode);
		}

		void reset()
		{
			synth->reset();
		}

};

static void display_help(void)
{
	fprintf(stderr,
"fmmidi "FMMIDI_VERSION"\n"
"fmmidi [options] [ files ... ]\n"
"Play Standard MIDI music file(s) by emulating a Yamaha YM2608 FM synthesizer\n"
"Options:\n"
"		-B				\"Broken\" mode\n"
"		-L				Loop forever\n"
"		-s r			  Sound rate (default: 44100)\n"
"		-S r			  Playing sound rate (if not specified, same as above)\n"
"		-l n			  Loop n times\n"
	
#ifdef USE_LIBAO_AUDIO
"		-D driver		 Use libao driver \"driver\". \"list\" for list.\n"
"		-o file		   Output file. `-\' for standard output\n"
#elif defined(USE_BSD_AUDIO)
"		-a				Specify audio device (default: /dev/audio)\n"
#elif defined(USE_QNX_AUDIO)
"		-a <card>:<dev>   Specify audio card number and device\n"
#endif

#ifdef FMMIDI_CURSES_H
"		-t				Write on the terminal directly, do not use Curses\n"
#endif
	
"		-V				Display program version\n"
"		-h				Display program help (this screen)\n"
		);
}

#ifdef FMMIDI_SIGNAL_H

volatile int fmmidi_sigint_fired = 0;
volatile time_t fmmidi_sigint_time = 0;

void fmmidi_sigint_handler(int signal)
{
	fmmidi_sigint_fired = 1;
}

#ifdef FMMIDI_CURSES_H

volatile int fmmidi_sigwinch_fired = 0;

void fmmidi_sigwinch_handler(int signal)
{
	fmmidi_sigwinch_fired = 1;
}

#endif

#endif

#ifdef USE_BSD_AUDIO
int soundfd;
#elif defined(USE_QNX_AUDIO)
int acard = -1, adevice = -1;
snd_pcm_t *ahandle;
snd_pcm_channel_params_t cparams;
snd_pcm_channel_info_t apluginfo;
 snd_pcm_channel_status_t astatus;
#else
ao_device *audevice;
#endif

short *sampOut;
short *blankOut;
fmOut *out;
midisequencer::sequencer *seq;
bool broken_mode = false;	
bool buffered = false;
int rate=44100;
double delta = 1.0f/32;
int sample_byte_size;

#ifdef USE_PTHREAD
int sound_work_flag = 0;
unsigned int sound_playing_frame = 0;
unsigned int sound_buffered_frame = 0;
pthread_t sound_work_thread;
pthread_mutex_t sound_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

void sound_play(void *smp, int size)
{
#ifdef USE_BSD_AUDIO
	write(soundfd, smp, size);
#elif defined(USE_QNX_AUDIO)
	snd_pcm_plugin_write(ahandle, smp, size);
		
	bzero(&astatus, sizeof(astatus));
	astatus.channel = SND_PCM_CHANNEL_PLAYBACK;
	snd_pcm_plugin_status (ahandle, &astatus);
		
	if(astatus.status == SND_PCM_STATUS_UNDERRUN)
		snd_pcm_plugin_prepare (ahandle, SND_PCM_CHANNEL_PLAYBACK);
#elif defined(USE_LIBAO_AUDIO)
	ao_play(audevice, (char*)smp, size);
#endif
}

#ifdef USE_PTHREAD
static void *sound_work_thread_func(void *)
{
	for(int i = 0; i < 8; i++)
		sound_play(blankOut, sample_byte_size);

	while(sound_work_flag)
	{
		if(sound_buffered_frame > sound_playing_frame)
		{
			sound_play(sampOut + (sound_playing_frame % 8) * sample_byte_size,
				sample_byte_size);
			sound_playing_frame++;
		}
		else
		{
			usleep(1000);
		}
	}
	return nullptr;
}
#endif

bool sound_initialize(int playrate)
{
#ifdef USE_BSD_AUDIO
	soundfd = open(audevname, O_WRONLY);
	
	if(soundfd == -1)
	{
		fprintf(stderr, "ERROR: Could not open audio device %s\n", audevname);
		return false;
	}
	
	audio_info_t audio_settings;
	
	AUDIO_INITINFO(&audio_settings);
	audio_settings.mode = AUMODE_PLAY;
	audio_settings.play.sample_rate = playrate;
	audio_settings.play.channels = 2;
	audio_settings.play.precision = 16;
	audio_settings.play.encoding = AUDIO_ENCODING_SLINEAR;

	ioctl(soundfd, AUDIO_SETINFO, &audio_settings);
#elif defined(USE_QNX_AUDIO)
	
	int c;
	
	if(acard == -1 || adevice == -1)
		c = snd_pcm_open_preferred(&ahandle, &acard, &adevice,
			SND_PCM_OPEN_PLAYBACK);
	else
		c = snd_pcm_open(&ahandle, acard, adevice, 
			SND_PCM_OPEN_PLAYBACK);
	
	if(c < 0)
	{
		fprintf(stderr, "ERROR: Could not open audio\n");
		return false;
	}
	
	bzero(&apluginfo, sizeof(apluginfo));
	
	apluginfo.channel = SND_PCM_CHANNEL_PLAYBACK;
	
	if(snd_pcm_plugin_info(ahandle, &apluginfo) < 0)
	{
		fprintf(stderr, "ERROR: Could not open audio\n");
		return false;
	}
	
	bzero(&cparams, sizeof(snd_pcm_channel_params_t));
	
	cparams.channel = SND_PCM_CHANNEL_PLAYBACK;
	cparams.mode = SND_PCM_MODE_BLOCK;
	
	cparams.format.interleave = 1;
	cparams.format.format = snd_pcm_build_linear_format(16, 0, 0);
	cparams.format.rate = playrate;
	cparams.format.voices = 2;
	cparams.start_mode = SND_PCM_START_FULL;
	cparams.stop_mode = SND_PCM_STOP_STOP;
	
	cparams.buf.block.frag_size = apluginfo.max_fragment_size;
	cparams.buf.block.frags_max = 1;
	cparams.buf.block.frags_min = 1;
	
	if(snd_pcm_plugin_params(ahandle, &cparams) < 0)
	{
		fprintf(stderr, "ERROR: Could not open audio\n");
		return false;
	}
	
	if(snd_pcm_plugin_prepare(ahandle, SND_PCM_CHANNEL_PLAYBACK) < 0)
	{
		fprintf(stderr, "ERROR: Could not open audio\n");
		return false;
	}
#elif defined(USE_LIBAO_AUDIO)
	if(driver_id == -1)
	{
		fprintf(stderr, "ERROR: No available live output device available.");
		return false;
	}

	ao_sample_format smpformat;
	ao_info *drvinfo = ao_driver_info(driver_id);
	
	smpformat.bits = 16;
	smpformat.rate = playrate;
	smpformat.channels = 2;
	smpformat.byte_format = AO_FMT_NATIVE;
	smpformat.matrix = NULL;

	if(drvinfo->type == AO_TYPE_LIVE)
		audevice = ao_open_live(driver_id, &smpformat, NULL);
	else
	{
		if(outname[0] == 0)
			snprintf(outname, 4095, "fmmidi.%s", drvname);
		
		audevice = ao_open_file(driver_id, outname, 1, &smpformat, NULL);
	}
	
	if(audevice == NULL)
	{
		printf("ERROR: Could not open specified audio device.\n");
		return false;
	}
	
	if(drvinfo->type == AO_TYPE_FILE)
	{
		if(strcmp(outname, "-") == 0)
			fprintf(stderr, "Logging to standard output\n");
		else
			fprintf(stderr, "Logging to file %s\n", outname);
	}
#endif
	
	sample_byte_size = rate*delta*2*sizeof(short);
	
#ifdef USE_PTHREAD
	sound_playing_frame = 0;
	sound_buffered_frame = 0;
	sound_work_flag = 1;
	if(pthread_create(&sound_work_thread, nullptr, sound_work_thread_func,
		nullptr)!=0)
	{
		fprintf(stderr, "Failed to create sound thread: %s\n",
			strerror(errno));
		sound_work_flag = 0;
	}
#endif

	return true;
}

void sound_close(void)
{
#ifdef USE_PTHREAD
	if(sound_work_flag)
	{
		sound_work_flag = 0;
		pthread_join(sound_work_thread, NULL);
	}
#endif

#ifdef USE_BSD_AUDIO	
	close(soundfd);
#elif defined(USE_QNX_AUDIO)
	snd_pcm_close(ahandle);
#else
	ao_close(audevice);
#endif
}

static double musicTime;
static double totalTime;

static void print_info(void)
{
#ifdef FMMIDI_CURSES_H
	if(cursesok)
	{
		printw("Title: %s\n", seq->get_title().c_str());
		printw("Copyright: %s\n", seq->get_copyright().c_str());
		printw("Song: %s\n", seq->get_song().c_str());
		printw("Total time: %02d:%02d.%02d\n", (int)totalTime / 60,
			(int)totalTime % 60, (int)((totalTime - (int)totalTime) * 100));
	
		return;
	}
#endif	
	
	fprintf(stderr, "Title: %s\n", seq->get_title().c_str());
	fprintf(stderr, "Copyright: %s\n", seq->get_copyright().c_str());
	fprintf(stderr, "Song: %s\n", seq->get_song().c_str());
	fprintf(stderr, "Total time: %02d:%02d.%02d\n", (int)totalTime / 60,
			(int)totalTime % 60, (int)((totalTime - (int)totalTime) * 100));
}

#ifdef FMMIDI_CURSES_H
static void draw_curses_nsongs_loop(void)
{
	mvhline(6, 0, ' ', 10000);

	move(6, 0);
	printw("[ %d %s ]", (int)playlist.size(), (playlist.size() > 1) 
		? "Songs" : "Song");
	printw(" [ Loop: ");
	
	if(loopForever)
		printw("Forever");
	else if(loopTimes == 0)
		printw("No");
	else
		printw("%d %s", loopTimes, (loopTimes == 1) ? "time" : "times");
	
	printw(" ]");
}

static void draw_curses_titlebar(void)
{
	refresh();
	wclear(stdscr);
	attron(A_REVERSE);
	
	mvhline(0, 0, ' ', 10000);
	
	move(0, 0);
	printw("FMMidi Player "FMMIDI_VERSION);

	attroff(A_REVERSE);
}

static void draw_curses_ui(void)
{
	int x, y, z, n, s;
	int lowborder;
	char nstring[16];
	
	draw_curses_titlebar();

	lowborder = getmaxy(cwin) - 2;

	mvhline(lowborder, 0, ACS_HLINE, 10000);

	move(lowborder+1, 0);
	printw("Q to quit, N for next song, P for previous song");
	
	move(1, 0);
	
	print_info();
	
	draw_curses_nsongs_loop();
	
	if ( (7+playlistPos) >= lowborder)
		y = ((7+playlistPos) - lowborder) + 1;
	else
		y = 0;
		
	for(n = playlist.size(), s=0; n > 0; n/=10, s++);
	
	if(s < 4)
		s=4;
	
	for(x = 0; x < (int)playlist.size() && (7+x) < lowborder; x++)
	{
		move(7+x, 0);
		
		sprintf(nstring, "%d", x+y);


		n = strlen(nstring);
		
		if(n < s)
		{
			for(z = 0; z < s-n; z++)
				printw(" ");
		}

		if(playerr.find(x+y) != playerr.end())
			attron(A_REVERSE);

		printw("%s", nstring);
		
		if(playerr.find(x+y) != playerr.end())
			attroff(A_REVERSE);

		
		printw(" %c", (playlistPos == (x+y)) ? '>' : ' ');
		
		if(playlistPos == (x+y))
			attron(A_REVERSE);
		
		printw("%s", playlist.at(x+y));
		
		if(playlistPos == (x+y))
			attroff(A_REVERSE);
	}
}
#endif

int fmmidi_play_file(const char *path)
{
#ifdef FMMIDI_CURSES_H
	bool force_redraw_ui = false;
#endif

	FILE *midi = fopen(path, "rb");
	
	if(!midi)
	{
#ifdef FMMIDI_CURSES_H
		if(!cursesok)
#endif
		fprintf(stderr, "Error while opening %s: %s\n",
			path, strerror(errno));
		
		return 4;
	}
	
	seq->clear();
	
	if(!seq->load(midi))
	{
#ifdef FMMIDI_CURSES_H
		if(!cursesok)
#endif
		fprintf(stderr, "Error while opening %s: Invalid MIDI file\n",
			path);		
		
		fclose(midi);
		return 5;
	}
	
	fclose(midi);
	
	musicTime = 0;
	//delta = 1.0f/32;
	totalTime=seq->get_total_time();


#ifdef FMMIDI_CURSES_H
	if(cursesok)
		draw_curses_ui();	
	else
#endif		
		print_info();

	musicTime = 0;
	seq->rewind();

	while(musicTime < totalTime)
	{
#ifdef FMMIDI_SIGNAL_H
		if(fmmidi_sigint_fired)
		{
			fmmidi_sigint_fired = 0;
				
			if(fmmidi_sigint_time == time(NULL))
				return 1;
				
			fmmidi_sigint_time = time(NULL);
		
			return 2;
		}
		
		#ifdef FMMIDI_CURSES_H
		if(fmmidi_sigwinch_fired)
		{
			fmmidi_sigwinch_fired = 0;
			
			endwin();

			force_redraw_ui = true;
		}
		#endif
#endif
		
#ifdef FMMIDI_CURSES_H
		if(cursesok)
		{
			int c;

			switch((c = getch()))
			{
				case KEY_LEFT:
					musicTime -= 3;
					
					if(musicTime < 0)
						musicTime = 0;
						
					seq->set_time(musicTime, out);	
				break;
				
				case KEY_RIGHT:
					musicTime += 3;
					seq->set_time(musicTime, out);
				break;
				
				case 'q':
					return 1;
					
				case 'n':
					return 2;
					
				case 'p':
					return 3;
					
				case 'r':
					musicTime = 0;
					out->meta_event(META_EVENT_ALL_NOTE_OFF, NULL, 0);
					seq->rewind();
				break;
				
				case 'l':
					loopForever = !loopForever;
					loopTimes = 0;
					draw_curses_nsongs_loop();
				break;
				
				case '1':
					if(loopTimes > 0)
						loopTimes--;
						
					draw_curses_nsongs_loop();
				break;
				
				case '2':
					loopTimes++;
					
					draw_curses_nsongs_loop();
				break;
			}
		}
#endif
		
		seq->play(musicTime, out);
			
		musicTime+=delta;

		if(broken_mode && musicTime > 0.5 && musicTime < 0.55)
			out->set_mode(midisynth::system_mode_default);

#ifdef USE_PTHREAD
		out->synthesize(sampOut + (sound_buffered_frame % 8) *
			sample_byte_size, rate*delta, rate);
		sound_buffered_frame++;
#else
		out->synthesize(sampOut, rate*delta, rate);
#endif
		
#ifdef USE_PTHREAD
		while(sound_buffered_frame - sound_playing_frame >= 8)
		{
			usleep(1000);  
		}
#else
		if(!buffered)
		{
			for(int i = 0; i < 8; i++)
				sound_play(blankOut, sample_byte_size);
		}
		sound_play(sampOut, sample_byte_size);
		buffered = true;
#endif

		
#ifdef FMMIDI_CURSES_H
		if(cursesok)
		{
			if(force_redraw_ui)
			{
				draw_curses_ui();
				force_redraw_ui = false;
			}
		
			move(5, 0);
			printw("%02d:%02d.%02d / %02d:%02d.%02d",
			(int)musicTime / 60, (int)musicTime% 60,
			(int)((musicTime - (int)musicTime) * 100),
			(int)totalTime / 60, (int)totalTime % 60,
			(int)((totalTime - (int)totalTime) * 100));
			
			continue;
		}
#endif
		
		fprintf(stderr, "					 \r");
		fprintf(stderr, "%02d:%02d.%02d / %02d:%02d.%02d\r",
			(int)musicTime / 60, (int)musicTime% 60,
			(int)((musicTime - (int)musicTime) * 100),
			(int)totalTime / 60, (int)totalTime % 60,
			(int)((totalTime - (int)totalTime) * 100));
		
		fflush(stderr);
				
	}

	return 0;
}			

int main(int argc, char *argv[])
{
	int c;
	int playrate=-1;

	
	if(argc < 2)
	{
		display_help();
		return EXIT_FAILURE;
	}
	
	#ifdef USE_LIBAO_AUDIO
		
	ao_initialize();
	driver_id = ao_default_driver_id();
	
	atexit(ao_shutdown);
	
	drvname[0] = 0;
	outname[0] = 0;
	#elif defined(USE_BSD_AUDIO)
	strcpy(audevname, "/dev/audio");
	#endif
	
	out = new fmOut();

	while ( (c = getopt(argc, argv, 
	
#ifdef FMMIDI_CURSES_H
#define BASE_OPTIONS		"s:S:Bl:LVht"
#else
#define BASE_OPTIONS		"s:S:Bl:LVh"
#endif
	
#ifdef USE_LIBAO_AUDIO
	BASE_OPTIONS"D:o:" // libao
#elif defined(USE_BSD_AUDIO)	
	BASE_OPTIONS"a:" // BSD audio
#elif defined(USE_QNX_AUDIO)
	BASE_OPTIONS"a:" // QNX audio
#endif
	
	)) != -1)
	{
		switch(c)
		{
			case '?':
				display_help();
				return EXIT_FAILURE;
			case 'h':
				display_help();
				return EXIT_SUCCESS;
			case 's':
				rate = atoi(optarg);
			break;
			case 'S':
				playrate = atoi(optarg);
			break;
			case 'B':
				broken_mode = true;
			break;
			case 'l':
				loopTimes = atoi(optarg);
			break;
			case 'L':
				loopForever = true;
			break;
			
			#ifdef USE_LIBAO_AUDIO
			
			case 'D':
				if(strcmp(optarg, "list") == 0)
				{					
					int drvcount;
					ao_info **drvinfo = ao_driver_info_list(&drvcount);
			
					fprintf(stderr, "Number of libao drivers: %d\n", drvcount);
			
					for(c = 0; c < drvcount; c++)
						fprintf(stderr, "Driver: %s (%s), type: %s\n",
							drvinfo[c]->short_name, drvinfo[c]->name,
								(drvinfo[c]->type==AO_TYPE_LIVE) 
									? "live":"file");
					return EXIT_SUCCESS;
				}				
				else 
				{
					if ( (driver_id=ao_driver_id(optarg)) == -1)
					{
						fprintf(stderr, "ERROR: No driver named %s exists.\n", optarg);
						return EXIT_FAILURE;
					}
									
					strncpy(drvname, optarg, 15);
					drvname[15] = 0;
				}
			break;
			
			case 'o':
				if(drvname[0] == 0)
				{
					if(strcmp(optarg, "-") == 0)
						strcpy(drvname, "raw");
					else
						strcpy(drvname, "wav");
					
					if ( (driver_id=ao_driver_id(drvname)) == -1)
					{
						fprintf(stderr, "ERROR: No WAV output plugin available, try other file output drivers.\n");
						return EXIT_FAILURE;
					}
				}
			
				strncpy(outname, optarg, 4095);
				outname[4095] = 0;
			break;
			
			#elif defined(USE_BSD_AUDIO)
			
			case 'a':
				strncpy(audevname, optarg, 4095);
				audevname[4095] = 0;
			break;
			
			#elif defined(USE_QNX_AUDIO)
				
			case 'a':
				sscanf(optarg, "%d:%d",	&acard, &adevice);
			break;
				
			#endif
				
			#ifdef FMMIDI_CURSES_H
			
			case 't':
				cursesok = false;
			break;
			
			#endif
				
			case 'V':
				fprintf(stderr, FMMIDI_VERSION"\n");
				return EXIT_SUCCESS;
		}
	}

	argc -= optind;
	argv += optind;
	
	if(argc <= 0)
	{
		fprintf(stderr, "ERROR: No files specified.\n");
		return EXIT_FAILURE;
	}
	
	if(playrate == -1)
		playrate = rate;
	
	if(rate <= 0)
	{
		fprintf(stderr, "ERROR: Rate cannot be zero or negative.\n");
		return EXIT_FAILURE;
	}
	
	if(playrate <= 0)
	{
		fprintf(stderr, "ERROR: Play rate cannot be zero or negative.\n");
		return EXIT_FAILURE;
	}
		
	if(!sound_initialize(playrate))
		return EXIT_FAILURE;

	sampOut = (short*)malloc(rate * 24 * sizeof(short));
	blankOut = (short*)malloc(rate * 2 * sizeof(short));
	
	for(int i = 0; i < (rate * 2); i++)
	{
		sampOut[i] = 0;
		blankOut[i] = 0;
	}
	
	seq = new midisequencer::sequencer();
	
	#ifdef FMMIDI_CURSES_H
	if(cursesok)
		cwin = initscr();
	
	if(!cwin)
		cursesok = false;
	
	if(cursesok)
	{
		raw();
		keypad(cwin, true);
		noecho();
		nodelay(cwin, true);
		curs_set(0);
	}

	#endif
	
	#ifdef FMMIDI_SIGNAL_H
		signal(SIGINT, fmmidi_sigint_handler);
		
	#ifdef FMMIDI_CURSES_H
	if(cursesok)
		signal(SIGWINCH, fmmidi_sigwinch_handler);
	#endif
		
	#endif
	
	int r = 0;
	int oldR = 0;
	
	for(r = 0; r < argc; r++)
		playlist.push_back(argv[r]);
	
	while(loopTimes >= 0 || loopForever)
	{
		for(playlistPos = 0; playlistPos < (int)playlist.size(); playlistPos++)
		{
			if(playerr.find(playlistPos) != playerr.end())
			{
			#ifdef FMMIDI_CURSES_H
				if(!cursesok)
			#endif
				fprintf(stderr, "\nSkipping %s\n", playlist.at(playlistPos));
				
				r = 6;
			}
			else
			{
			#ifdef FMMIDI_CURSES_H
				if(!cursesok)
			#endif
					printf("\n");
			
				r = fmmidi_play_file(playlist.at(playlistPos));
				
			#ifdef FMMIDI_CURSES_H
				if(!cursesok)
			#endif
					if(r<4)printf("\n");
			}
			
			if(r == 1)
				break;
			else if(r == 3)
			{
make_it_go_to_previous_song:				
				playlistPos-=2;
				
				if(playlistPos < 0)
					playlistPos = -1;
				
				r=3;
			}
			else if(r == 4 || r == 5 || r == 6) // Load error
			{
				if(r != 6)
				{
					std::string str(playlist.at(playlistPos));
				
					playlist.erase(playlist.begin()+playlistPos);
				
					str += " (";
					
					if(r == 4)
						str += strerror(errno);
					else
						str += "Invalid MIDI file";
				
					str += ")";
				
					playlist.insert(playlist.begin()+
					playlistPos, (const char*)strdup(str.c_str()));
				
					playerr.insert(std::pair<int, bool>(playlistPos,true));
					
					if(playlist.size() == playerr.size())
						break;
				}
					
				if(oldR == 3)
					goto make_it_go_to_previous_song;
			}
			
			oldR = r;
		}

		if(r == 1)
			break;
		
		if(!loopForever)
			loopTimes--;
	}
	
	sound_play(sampOut, sample_byte_size);

	if(!r)
		sleep(1);
	
	sound_close();
	
	#ifdef FMMIDI_CURSES_H
	if(cursesok)
	{
		wclear(stdscr);
		refresh();
		endwin();
	}
	#endif
	
	if(playlist.size() == playerr.size())
		fprintf(stderr, "No songs could be played!\n");
	
	return EXIT_SUCCESS;
}
