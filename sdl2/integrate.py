#!/usr/bin/env python3
"""Add the MiSTer hybrid core drivers to an SDL2 source tree (2.32.x).

usage: integrate.py <SDL2 source directory>

Copies the drivers and the HPS library (hybrid/hps) into the tree and
registers them. Configure SDL with -DSDL_MISTER=ON afterwards; the "mister"
video and audio drivers are then tried first and only work with a hybrid core
loaded. mister_hybrid.h is installed next to the SDL headers.
"""
import shutil
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
sdl = Path(sys.argv[1])


def patch(path, anchor, replacement):
    text = (sdl / path).read_text()
    if replacement in text:
        return
    if text.count(anchor) != 1:
        sys.exit(f"integrate.py: {path}: anchor not found exactly once: {anchor!r}")
    (sdl / path).write_text(text.replace(anchor, replacement))


def copy(src, dst):
    (sdl / dst).parent.mkdir(parents=True, exist_ok=True)
    # unchanged files keep their time stamp, so SDL only rebuilds what changed
    if not (sdl / dst).exists() or (sdl / dst).read_bytes() != Path(src).read_bytes():
        shutil.copyfile(src, sdl / dst)


copy(here / "SDL_mistervideo.c", "src/video/mister/SDL_mistervideo.c")
copy(here / "SDL_misteraudio.c", "src/audio/mister/SDL_misteraudio.c")
copy(here.parent / "hps/mister_hybrid.c", "src/core/mister/mister_hybrid.c")
copy(here.parent / "hps/mister_joymap.c", "src/core/mister/mister_joymap.c")
copy(here.parent / "hps/mister_ui.c", "src/core/mister/mister_ui.c")
copy(here.parent / "hps/mister_games.c", "src/core/mister/mister_games.c")
copy(here.parent / "hps/mister_triggers.c", "src/core/mister/mister_triggers.c")
copy(here.parent / "hps/mister_midi.c", "src/core/mister/mister_midi.c")
copy(here.parent / "hps/mister_midi.h", "src/core/mister/mister_midi.h")
copy(here.parent / "hps/mister_font.h", "src/core/mister/mister_font.h")
copy(here.parent / "hps/mister_hybrid.h", "include/mister_hybrid.h")

patch("src/video/SDL_video.c",
      "static VideoBootStrap *bootstrap[] = {\n",
      "static VideoBootStrap *bootstrap[] = {\n#ifdef SDL_VIDEO_DRIVER_MISTER\n    &MISTER_bootstrap,\n#endif\n")
patch("src/video/SDL_sysvideo.h",
      "extern VideoBootStrap DUMMY_bootstrap;\n",
      "extern VideoBootStrap MISTER_bootstrap;\nextern VideoBootStrap DUMMY_bootstrap;\n")
patch("src/audio/SDL_audio.c",
      "static const AudioBootStrap *const bootstrap[] = {\n",
      "static const AudioBootStrap *const bootstrap[] = {\n#ifdef SDL_AUDIO_DRIVER_MISTER\n    &MISTERAUDIO_bootstrap,\n#endif\n")
patch("src/audio/SDL_sysaudio.h",
      "extern AudioBootStrap DUMMYAUDIO_bootstrap;\n",
      "extern AudioBootStrap MISTERAUDIO_bootstrap;\nextern AudioBootStrap DUMMYAUDIO_bootstrap;\n")

patch("CMakeLists.txt",
      "if(SDL_AUDIO)\n  # CheckDummyAudio/CheckDiskAudio - valid for all platforms\n",
      """set_option(SDL_MISTER "MiSTer hybrid core video, audio and input drivers" OFF)
if(SDL_MISTER)
  file(GLOB MISTER_SOURCES
    ${SDL2_SOURCE_DIR}/src/video/mister/*.c
    ${SDL2_SOURCE_DIR}/src/audio/mister/*.c
    ${SDL2_SOURCE_DIR}/src/core/mister/*.c)
  list(APPEND SOURCE_FILES ${MISTER_SOURCES})
  add_definitions(-DSDL_VIDEO_DRIVER_MISTER=1 -DSDL_AUDIO_DRIVER_MISTER=1)
endif()

if(SDL_AUDIO)
  # CheckDummyAudio/CheckDiskAudio - valid for all platforms
""")
# The core hands us the joysticks Main_MiSTer has already mapped: SDL must not
# also open the raw devices
patch("CMakeLists.txt",
      "if((LINUX OR FREEBSD) AND HAVE_LINUX_INPUT_H AND NOT ANDROID)\n      set(SDL_JOYSTICK_LINUX 1)",
      "if((LINUX OR FREEBSD) AND HAVE_LINUX_INPUT_H AND NOT ANDROID AND NOT SDL_MISTER)\n      set(SDL_JOYSTICK_LINUX 1)")
# The resampler (SDL_ConvertAudio(), audio streams) divided two 64-bit numbers
# for every frame: the ARM has no division instruction, and the C library's
# took most of the time (CrispyDoom's sounds, converted at its start: 5.7s).
# The source frame and its fraction are stepped instead, the same numbers.
patch("src/audio/SDL_audiocvt.c",
      "    for (i = 0; i < outframes; i++) {\n        const int srcindex = (int)((Sint64)i * inrate / outrate);\n",
      "    int step_index = 0, step_fraction = 0; /* MiSTer hybrid: i * inrate = step_index * outrate + step_fraction */\n"
      "    for (i = 0; i < outframes; i++) {\n        const int srcindex = step_index;\n")
patch("src/audio/SDL_audiocvt.c",
      "        const int srcfraction = ((Sint64)i) * inrate % outrate;\n",
      "        const int srcfraction = step_fraction;\n")
patch("src/audio/SDL_audiocvt.c",
      "            *(dst++) = outsample;\n        }\n    }\n\n    return outframes * chans * sizeof(float);\n",
      "            *(dst++) = outsample;\n        }\n"
      "        for (step_fraction += inrate; step_fraction >= outrate; step_fraction -= outrate) {\n"
      "            step_index++;\n        }\n    }\n\n    return outframes * chans * sizeof(float);\n")
# Most of the rest was working out the filter for every tap of every frame and
# channel, from the fraction of a source frame, which with a whole-number ratio
# has few values (11025 to 44100Hz: 4). SDL_ResampleAudio() works it out once
# for each and then adds up the same floats in the same order (the same sound:
# tested with every sound of DOOM2.WAD and PLUTONIA.WAD); a ratio with more
# fractions than it keeps goes the generic way.
patch("src/audio/SDL_audiocvt.c",
      "/* lpadding and rpadding are expected to be buffers of (ResamplePadding(inrate, outrate) * chans * sizeof(float)) bytes. */\n"
      "static int SDL_ResampleAudio(const int chans,",
      "/* lpadding and rpadding are expected to be buffers of (ResamplePadding(inrate, outrate) * chans * sizeof(float)) bytes. */\n"
      "static int SDL_ResampleAudio_Generic(const int chans,")
patch("src/audio/SDL_audiocvt.c",
      "}\n\nint SDL_ConvertAudio(SDL_AudioCVT *cvt)\n{",
      """}

/* MiSTer hybrid: the generic one with the filter of each fraction worked out
   once (see hybrid/sdl2/integrate.py) */
#define MISTER_RESAMPLE_PHASES 160 /* 44100 <-> 48000Hz: 147 */
#define MISTER_RESAMPLE_TAPS   ((RESAMPLER_FILTER_SIZE - 1) / RESAMPLER_SAMPLES_PER_ZERO_CROSSING + 1)
static int SDL_ResampleAudio(const int chans, const int inrate, const int outrate,
                             const float *lpadding, const float *rpadding,
                             const float *inbuf, const int inbuflen,
                             float *outbuf, const int outbuflen)
{
    const int paddinglen = ResamplerPadding(inrate, outrate);
    const int framelen = chans * (int)sizeof(float);
    const int inframes = inbuflen / framelen;
    const int wantedoutframes = (int)((Sint64)inframes * outrate / inrate);
    const int maxoutframes = outbuflen / framelen;
    const int outframes = SDL_min(wantedoutframes, maxoutframes);
    float coef1[MISTER_RESAMPLE_PHASES][MISTER_RESAMPLE_TAPS];
    float coef2[MISTER_RESAMPLE_PHASES][MISTER_RESAMPLE_TAPS];
    int taps1[MISTER_RESAMPLE_PHASES], taps2[MISTER_RESAMPLE_PHASES];
    float *dst = outbuf;
    int a = inrate, b = outrate, phases, phase, i, j, chan;
    int srcindex = 0, srcfraction = 0;

    /* frame i's fraction is i * inrate % outrate: outrate / gcd values */
    while (b != 0) {
        const int t = a % b;
        a = b;
        b = t;
    }
    phases = outrate / a;
    if (phases > MISTER_RESAMPLE_PHASES) {
        return SDL_ResampleAudio_Generic(chans, inrate, outrate, lpadding, rpadding, inbuf, inbuflen, outbuf, outbuflen);
    }

    /* the generic one's expressions */
    for (phase = 0; phase < phases; phase++) {
        const float interpolation1 = ((float)srcfraction) / ((float)outrate);
        const int filterindex1 = ((Sint32)srcfraction) * RESAMPLER_SAMPLES_PER_ZERO_CROSSING / outrate;
        const float interpolation2 = 1.0f - interpolation1;
        const int filterindex2 = ((Sint32)(outrate - srcfraction)) * RESAMPLER_SAMPLES_PER_ZERO_CROSSING / outrate;

        for (j = 0; (filterindex1 + (j * RESAMPLER_SAMPLES_PER_ZERO_CROSSING)) < RESAMPLER_FILTER_SIZE; j++) {
            const int filt_ind = filterindex1 + j * RESAMPLER_SAMPLES_PER_ZERO_CROSSING;
            coef1[phase][j] = ResamplerFilter[filt_ind] + (interpolation1 * ResamplerFilterDifference[filt_ind]);
        }
        taps1[phase] = j;
        for (j = 0; (filterindex2 + (j * RESAMPLER_SAMPLES_PER_ZERO_CROSSING)) < RESAMPLER_FILTER_SIZE; j++) {
            const int filt_ind = filterindex2 + j * RESAMPLER_SAMPLES_PER_ZERO_CROSSING;
            coef2[phase][j] = ResamplerFilter[filt_ind] + (interpolation2 * ResamplerFilterDifference[filt_ind]);
        }
        taps2[phase] = j;
        srcfraction = (int)(((Sint64)srcfraction + inrate) % outrate);
    }

    phase = 0;
    srcfraction = 0;
    for (i = 0; i < outframes; i++) {
        const float *c1 = coef1[phase];
        const float *c2 = coef2[phase];
        const int n1 = taps1[phase];
        const int n2 = taps2[phase];

        if (chans <= 2 && srcindex >= n1 - 1 && srcindex + n2 < inframes) {
            /* within the input, mono or stereo: each channel's sum in its
               own order, the two side by side */
            const float *in = inbuf + srcindex * chans;
            float out0 = 0.0f, out1 = 0.0f;

            if (chans == 2) {
                for (j = 0; j < n1; j++) {
                    out0 += (float) (in[-2 * j] * c1[j]);
                    out1 += (float) (in[-2 * j + 1] * c1[j]);
                }
                for (j = 0; j < n2; j++) {
                    out0 += (float) (in[2 * j + 2] * c2[j]);
                    out1 += (float) (in[2 * j + 3] * c2[j]);
                }
                dst[0] = out0;
                dst[1] = out1;
                dst += 2;
            } else {
                for (j = 0; j < n1; j++) {
                    out0 += (float) (in[-j] * c1[j]);
                }
                for (j = 0; j < n2; j++) {
                    out0 += (float) (in[j + 1] * c2[j]);
                }
                *(dst++) = out0;
            }
        } else for (chan = 0; chan < chans; chan++) {
            float outsample = 0.0f;

            for (j = 0; j < n1; j++) {
                const int srcframe = srcindex - j;
                const float insample = (srcframe < 0) ? lpadding[((paddinglen + srcframe) * chans) + chan] : inbuf[(srcframe * chans) + chan];
                outsample += (float) (insample * c1[j]);
            }
            for (j = 0; j < n2; j++) {
                const int srcframe = srcindex + 1 + j;
                const float insample = (srcframe >= inframes) ? rpadding[((srcframe - inframes) * chans) + chan] : inbuf[(srcframe * chans) + chan];
                outsample += (float) (insample * c2[j]);
            }
            *(dst++) = outsample;
        }
        if (++phase == phases) {
            phase = 0;
        }
        for (srcfraction += inrate; srcfraction >= outrate; srcfraction -= outrate) {
            srcindex++;
        }
    }

    return outframes * chans * sizeof(float);
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{""")
print(f"MiSTer hybrid drivers added to {sdl}")
