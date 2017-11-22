# oscigen
Generates oscilloscope views for chiptunes

Requires [SFML](https://www.sfml-dev.org/) and [Tiny File Dialogs](https://sourceforge.net/projects/tinyfiledialogs/).

Command line arguments:

```
-i	Specifies input audio files (master, voice1, voice2 etc.)
-o	Specifies output file
-h	Headless mode (disables preview)
-?	Prints list of commands
```

Usage:

```
./oscigen -i master.wav 1.wav 2.wav 3.wav -o scopeview.mp4
```

If no arguments are passed, oscigen will open file dialogs to select files instead.
