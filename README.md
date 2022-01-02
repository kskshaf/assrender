# AssRender-Vapoursynth

AssRender-Vapoursynth is a Vapoursynth api3 plugin that renders ASS / SSA and SRT (without the HTML-like markup) subtitles, based on the source of pinterf’s fork. It uses libass to render the subtitles, which makes it the fastest, lower memory usage and most correct ASS renderer for Vapoursynth.

This also means that it is much more picky about script syntax than VSFilter and friends, so keep that in mind before blaming the filter. Yes, people have reported a lot of errors that were actually the script author’s fault.

## Usage

Supported video formats:
  - 8-, 10-, 12-, 14- and 16-bit integer
  - YUV, RGB, GRAY

### TextSub

`assrender.TextSub(clip clip, string file, [string vfr, int hinting=0, float scale=1.0, float line_spacing=1.0, float dar, float sar, int top=0, int bottom=0, int left=0, int right=0, string charset, int debuglevel, string fontdir="", string srt_font="sans-serif", string colorspace])`

Like `sub.TextFile`, `xyvsf.TextSub`

- `clip`: Input video clip.

- `file`: Your subtitle file. May be ASS, SSA or SRT (without the HTML-like markup).
	
- `vfr`: Specify timecodes v1 or v2 file when working with VFRaC.
	
- `hinting`: Font hinting mode. Choose between none (0, default), light (1), normal (2) and Freetype native (3) autohinting.
	
- `scale`: Font scale. Defaults to 1.0.
	
- `line_spacing`: Line spacing in pixels. Defaults to 1.0 and won’t be scaled with frame size.
	
- `dar`, `sar`: Aspect ratio. Of course you need to set both parameters.
	
- `top`, `bottom`, `left`, `right`: Margins. They will be added to the frame size and may be negative.
	
- `charset`: Character set to use, in GNU iconv or enca format. Defaults to detect the BOM and fallback to UTF-8 if BOM not found. Example enca format: `enca:pl:cp1250` (guess the encoding for Polish, fall back on `cp1250`)
		
- `debuglevel`: How much crap assrender is supposed to spam to stderr.
	
- `fontdir`: Additional font directory. Useful if you are lazy but want to keep your system fonts clean. Default value: `""`

- `srt_font`: Font to use for SRT subtitles. Defaults to whatever Fontconfig chooses for “sans-serif”.
	
- `colorspace`: The color space of your (YUV) video. Possible values:
  - Rec2020, BT.2020
  - Rec709, BT.709
  - Rec601, BT.601
  - PC.709
  - PC.601
  - TV.fcc, PC.fcc
  - TV.240m, PC.240m
  - none, guess
 
  Default is to use the ASS script’s `YCbCr Matrix` or `Video Colorspace` property.

  Recognized .ASS properties: `TV.601`, `TV.709`, `PC.601`, `PC.709`, `TV.240m`, `PC.240m` `TV.fcc`, `PC.fcc` and `none`. 

  `none` and `guess` decides upon on video resolution: width > 1280 or height > 576 → `BT.709`, else → `BT.601`.
  When no hint found in ASS script and `colorspace` parameter is empty then the default is `BT.601`.

### Subtitle

`assrender.Subtitle(clip clip, string[] text, [string style="sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1", int[] start, int[] end, string vfr, int hinting=0, float scale=1.0, float line_spacing=1.0, float dar, float sar, int top=0, int bottom=0, int left=0, int right=0, string charset, int debuglevel, string fontdir="", string srt_font="sans-serif", string colorspace])`

Like `sub.Subtitle`, it can render single line or multiline subtile string instead of a subtitle file.

- `clip`: Input video clip.

- `text`: String to be rendered, it will rendering multiline string if input a list. These can include ASS Tags.

- `style`: `text` used ASS style

- `start`, `end`: Subtitle display time, start frame number and end frame’s next frame number, it will trim like `[start:end]`, default is all frames of clip.
  - If you input list, each pair of `start` and `end` matches a `text`, and the missing `start` and `end` will be filled with the last one.
  - You must specify both the value of `start` and `end` if use these, otherwise it will be discarded.
  - If you dont’t render multiline subtitles, `std.Loop` maybe faster.

Other parameters are same as `assrender.TextSub`, but not necessarily useful, such as `srt_font`.

## Build instructions

### Windows Visual Studio 2019

* Prequisite: vsnasm integration
  - get VSNASM from https://github.com/ShiftMediaProject/VSNASM
  - run install_script.bat

* Clone repo

  Clone https://github.com/AmusementClub/assrender from VS IDE or 

      git clone https://github.com/AmusementClub/assrender
      git submodule update --init --recursive --remote

* Build:
  Open solution file from IDE

### Windows GCC (mingw installed by msys2)

* Clone repo

      git clone https://github.com/AmusementClub/assrender
        
  This environment is not using the git submodules, we need libass as a package.
  There is no need for submodule update.

* Prequisite: libass package

  - Get package

    Example for x64 version:
  
        $ pacman -S mingw64/mingw-w64-x86_64-libass

    Output:

          resolving dependencies...
          looking for conflicting packages...
          warning: dependency cycle detected:
          warning: mingw-w64-x86_64-harfbuzz will be installed before its mingw-w64-x86_64-freetype dependency

          Packages (10) mingw-w64-x86_64-fontconfig-2.13.93-1
                      mingw-w64-x86_64-freetype-2.10.4-1
                      mingw-w64-x86_64-fribidi-1.0.10-2
                      mingw-w64-x86_64-glib2-2.66.4-1
                      mingw-w64-x86_64-graphite2-1.3.14-2
                      mingw-w64-x86_64-harfbuzz-2.7.4-1
                      mingw-w64-x86_64-libpng-1.6.37-3  mingw-w64-x86_64-pcre-8.44-2
                      mingw-w64-x86_64-wineditline-2.205-3
                      mingw-w64-x86_64-libass-0.15.0-1

          Total Download Size:    6.92 MiB
          Total Installed Size:  42.31 MiB

          :: Proceed with installation? [Y/n]

     Choose Y and wait

* Build
  from the 'build' folder under project root:

      del ..\CMakeCache.txt
      cmake .. -G "MinGW Makefiles"
      cmake --build . --config Release --clean-first 

### Linux
* Clone repo

      git clone https://github.com/AmusementClub/assrender
      cd assrender
      cmake -B build -S .
      cmake --build build --clean-first
  
  Remark: submodules are not needed, libass is used as a package.

* Find binaries at
    
      build/assrender/libassrender.so

* Install binaries

      cd build
      sudo make install

## Licenses
  For all modules: see msvc/licenses

## Links
* Doom9 forum: https://forum.doom9.org/showthread.php?t=148926
* Avisynth wiki: http://avisynth.nl/index.php/AssRender
* libass original: https://github.com/libass/libass
* libass submodule used for msvc https://github.com/ShiftMediaProject/libass
* Aegisub: https://github.com/wangqr/Aegisub

## Change log
See https://github.com/AmusementClub/assrender/releases
