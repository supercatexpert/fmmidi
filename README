This is a conversion of the fmmidi.1 manual page to plain text.
For information about building fmmidi, read building.txt

*******

FMMIDI(1)                   General Commands Manual                  FMMIDI(1)

NAME
     fmmidi - Play Standard MIDI music file(s) by emulating a Yamaha YM2608 FM
     synthesizer

SYNOPSIS
     fmmidi [-BL] [-s rate] [-S rate] [-l times] [-D driver] [-o file]
            [-a device] [-V] [-h] [files ...]

DESCRIPTION
     fmmidi is a player for Standard MIDI music file(s). It emulates the
     Yamaha YM2608 FM synthesizer and it uses this emulation to play the
     musics in MIDI format.

     Many MIDI players since the late 1990s do not use (or emulate) FM
     synthesis and instead use prerecorded samples for instruments. These are
     called "wavetable" players.

     fmmidi on the other hand, emulating FM synthesis, generates sound waves
     at real time.  This makes music played using fmmidi sound more electronic
     compared to when it is played using wavetable players.

     fmmidi implements the MIDI specification up to the Yamaha XG commands.

OPTIONS
     -B      Set mode where the player does not accept Yamaha XG commands.
             This feature is "broken" but the sound generated when in this
             mode may be of interest.

     -L      Loop the music forever.

     -s rate
             rate at which the sound output is generated. (default: 44100)

     -S rate
             rate at which the sound output is played. When this option is not
             specified, this is equal to the rate at which the sound output is
             generated.

     -l times
             Loop the music times times.

     -D driver
             (libao version only) Specify the libao driver used to play the
             music.  To display a list of available libao drivers, specify
             list as driver.

     -o file
             (libao version only) Specify the file that will be used to store
             the sound data output. By default, the WAV format will be used,
             except when dumping to standard output ( `-' ) where sound data
             will be output raw.

     -a      (bsdaudio and qnx versions only) Specify audio device (bsdaudio
             default: /dev/audio, qnx default: 0:0)

     -V      Display program version

     -h      Display program help

HISTORY
     The original program was written in C++ for Windows by yuno in 2004.

     A version for the Mac OS X operating system, along with a version
     converted to the Java programming language, followed some time later. All
     these versions used a graphical user interface.


     The source code for the Mac OS X version was obtained by nextvolume in
     early 2015 and a simple command line interface was developed by him
     around the core parts that composed the original fmmidi program.

BUGS
     The drum emulation is currently somewhat broken.

AUTHORS
     nextvolume (Giuseppe Gatta) (tails92@gmail.com)

     yuno (Yoshio Uno) (yuno@users.sourceforge.jp)

WEBPAGE
     http://unhaut.x10host.com/fmmidi

                                 May 26, 2016                                 
