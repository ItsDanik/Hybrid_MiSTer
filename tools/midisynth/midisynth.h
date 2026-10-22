// C model of the core's MIDI voice engine (docs/midi_synth.md, "Design"):
// frame by frame what the FPGA does with the commands of the MIDI ring and
// the samples of the sample memory, in integers. The RTL matches it sample
// for sample; fakecore runs it in place of the FPGA.
#ifndef MIDISYNTH_H
#define MIDISYNTH_H

#include <stdint.h>

typedef struct midisynth midisynth;

// samples: the sample memory (nsamples 16-bit samples), read, never written
midisynth* ms_new(const int16_t* samples, uint32_t nsamples);
void ms_free(midisynth* ms);
// Every voice off, the commands not made yet dropped (MIDI disabled).
void ms_reset(midisynth* ms);
// A command from the ring, in the order written. 0 when the model's queue
// is full (it holds a ring's worth).
int ms_push(midisynth* ms, const uint64_t* cmd);
int ms_queued(const midisynth* ms);
// Audio frame `frame`: the commands for it and before, the control updates,
// every voice. sums: left, right, reverb, chorus; Q.14 of a 16-bit sample,
// 40 bits.
void ms_frame(midisynth* ms, uint32_t frame, int64_t* sums);
// The reverb and the chorus of the frame's sums (from ms_frame()): their
// sound added to the left and the right, held at 40 bits again.
void ms_effects(midisynth* ms, int64_t* sums);
// Left and right for the core's output at `volume` (32 as it comes), held
// at 16 bits.
void ms_output(const int64_t* sums, int volume, int16_t* out);
// For comparing with FluidSynth only: every voice's control update at
// frames that are multiples of 64, as FluidSynth's blocks, and no limit to
// the commands a frame.
void ms_aligned(midisynth* ms, int aligned);
// The voices playing: bit v of bits[v / 32]. Returns how many.
int ms_playing(const midisynth* ms, uint32_t* bits);

#endif
