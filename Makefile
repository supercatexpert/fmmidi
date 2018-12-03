# there are two sound output methods:
# libao (uses the libao library, that runs on Windows, Linux, OS X, etc.)
# bsd (uses the native BSD audio interface of NetBSD and OpenBSD)
# qnx (uses the native QNX audio interface)

SNDOUT = libao

# if this is set to yes, enable signal handling, otherwise disable it
# Set this to no on Windows

HAS_SIGNAL_H = yes
HAS_PTHREAD_H = yes

# if this is set to yes, use Curses interface
HAS_CURSES_H = yes
# library flags for Curses library
CURSES_LDFLAGS = -lcurses

CXXFLAGS = -I/usr/pkg/include -g -Wall
LDFLAGS = 

# prefix to use when installing
PREFIX = /usr/local

ifeq ($(HAS_SIGNAL_H),yes)
	CXXFLAGS += -DFMMIDI_SIGNAL_H
endif

ifeq ($(HAS_PTHREAD_H),yes)
	CXXFLAGS += -DUSE_PTHREAD
	LDFLAGS += -lpthread
endif

ifeq ($(HAS_CURSES_H),yes)
	CXXFLAGS += -DFMMIDI_CURSES_H
	LDFLAGS += $(CURSES_LDFLAGS)
endif

EXTRACOMMAND = true

ifeq ($(SNDOUT),libao) 
	CXXFLAGS +=
	LDFLAGS += -lao
else
	ifeq ($(SNDOUT),bsd)
		CXXFLAGS += -DUSE_BSD_AUDIO
		LDFLAGS += 
	else
		ifeq ($(SNDOUT),qnx)
			CXXFLAGS += -DUSE_QNX_AUDIO
			LDFLAGS += -lasound
			EXTRACOMMAND = echo '%1> %C -h' | usemsg ./fmmidi -
		else
			CXX = $(error Invalid sound ouput method $(SNDOUT))
		endif
	endif
endif

fmmidi: midisynth.o sequencer.o filter.o fmmidi.o Makefile
	$(CXX) -o fmmidi midisynth.o sequencer.o filter.o fmmidi.o $(CXXFLAGS) \
		$(LDFLAGS)
	sh -c "$(EXTRACOMMAND)"

midisynth.o: midisynth.cpp Makefile
sequencer.o: sequencer.cpp Makefile
filter.o: filter.cpp Makefile
fmmidi.o: fmmidi.cpp Makefile

clean:
	rm -f fmmidi *.o

install: fmmidi
	cp fmmidi $(PREFIX)/bin
	cp fmmidi.1 $(PREFIX)/man/man1

