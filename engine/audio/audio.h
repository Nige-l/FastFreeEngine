#pragma once

// audio.h — public API for the FFE audio subsystem.
//
// Tiers: LEGACY (primary), STANDARD, MODERN. RETRO is NOT supported.
//
// This is a global module singleton, matching the pattern of ffe::rhi.
// There is one audio device, one mixer, one output stream.
//
// Thread safety: all public functions must be called from the main thread,
// with the exception of playSound() which may be called from any thread
// that is NOT the audio callback thread.
//
// Performance: loadSound() / unloadSound() are load-time-only operations.
// playSound() and setMasterVolume() are safe to call on game-event boundaries
// (not per-frame). The audio callback runs on its own thread managed by
// miniaudio. No per-frame allocations.
//
// NOT a per-frame API. loadSound() performs file I/O, stat(), realpath(),
// and full PCM decode. Never call loadSound() per-frame.

#include "core/types.h"

namespace ffe::audio {

// ---------------------------------------------------------------------------
// SoundHandle — opaque handle to a loaded sound buffer.
// A handle with id == 0 is invalid (not loaded or load failed).
// Mirrors the rhi::TextureHandle pattern.
// ---------------------------------------------------------------------------
struct SoundHandle {
    u32 id = 0;
};

// Validity helper — mirrors the rhi::isValid() pattern.
inline bool isValid(const SoundHandle h) { return h.id != 0; }

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Maximum simultaneous sound effect voices. LEGACY-tier budget.
static constexpr u32 MAX_AUDIO_VOICES = 32u;

// Maximum path length for audio asset paths (matches texture_loader).
static constexpr u32 MAX_AUDIO_PATH = 4096u;

// Maximum decoded audio data per sound (bytes).
// 10 MB covers ~30 seconds of 44.1 kHz stereo float PCM.
// Sounds larger than this are rejected at load time.
static constexpr u64 AUDIO_MAX_DECODED_BYTES = 10ull * 1024ull * 1024ull;

// Maximum raw audio file size accepted before decoding begins.
// 32 MB allows for large uncompressed WAV sound effects.
static constexpr u64 AUDIO_MAX_FILE_BYTES = 32ull * 1024ull * 1024ull;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Initialise the audio subsystem. Call once from the main thread before any
// other audio function.
//
// headless: if true, skip device creation entirely (for CI/headless tests).
//   In headless mode, loadSound() and unloadSound() still work (for path-
//   validation testing). playSound() becomes a no-op. isAudioAvailable()
//   returns false.
//
// Returns false if the audio device could not be opened. The engine continues
// without audio; all playback functions become no-ops.
//
// Calling init() a second time logs a warning and returns false (no-op).
bool init(bool headless = false);

// Shut down the audio subsystem. Stops the audio thread, frees all loaded
// sounds, closes the device. Call once at shutdown, after all other audio
// calls. Safe to call if init() was never called.
//
// Thread safety: call from main thread only, with no concurrent audio calls.
void shutdown();

// ---------------------------------------------------------------------------
// Loading and unloading
// ---------------------------------------------------------------------------

// Load a WAV or OGG audio file and return a handle to the decoded PCM buffer.
//
// path must be a relative path with no traversal sequences:
//   - Must not start with '/' or '\'
//   - Must not contain "../", "..\", "/..", "\.."
//   - Must not be absolute (no drive letters, no UNC)
//   - Length (via strnlen) must be < MAX_AUDIO_PATH
//
// assetRoot must be an absolute path to an existing directory.
//
// Format is detected by magic bytes (not file extension).
//   Supported: WAV (RIFF magic), OGG (OggS magic).
//
// Security: path traversal check runs FIRST, before any syscall.
//   realpath() is used to canonicalise and verify the resolved path remains
//   within assetRoot. File size is checked via stat() before decoding.
//
// Returns SoundHandle{0} on failure. Logs the error via FFE_LOG_ERROR.
//
// Performance: performs stat(), realpath(), fopen(), and full PCM decode.
//   NOT a per-frame function. Call at scene load time.
SoundHandle loadSound(const char* path, const char* assetRoot);

// Load a music file for streaming playback via playMusic().
//
// Unlike loadSound(), this does NOT decode the entire file into memory.
// It validates the path, checks magic bytes (OGG/WAV), and stores the
// canonical path for streaming. Memory usage: ~4 KB per slot (path only).
//
// Use this for music tracks that are too large for full PCM decode
// (the 10 MB AUDIO_MAX_DECODED_BYTES limit does not apply here).
// The AUDIO_MAX_FILE_BYTES limit still applies to the raw file size.
//
// The returned handle can be passed to playMusic() but NOT to playSound()
// (no decoded PCM data is available — playSound() will find an empty buffer
// and silently skip it).
//
// Same path validation and security checks as loadSound().
SoundHandle loadMusic(const char* path, const char* assetRoot);

// Unload a sound and free its PCM buffer. No-op for SoundHandle{0}.
// If the sound is currently playing, the active voice is stopped first.
// After this call, the handle must not be used.
//
// Thread safety: call from main thread only.
void unloadSound(SoundHandle handle);

// ---------------------------------------------------------------------------
// Playback — sound effects
// ---------------------------------------------------------------------------

// Play a one-shot sound effect. Fire-and-forget — no way to stop it
// individually once started.
//
// No-op if:
//   - handle is invalid (SoundHandle{0})
//   - audio system is not initialised or is in headless mode
//   - all MAX_AUDIO_VOICES slots are occupied (logs a warning)
//
// volume: per-instance volume multiplier [0.0, 1.0]. Clamped. NaN/Inf -> 0.0.
//
// Thread safety: may be called from the main thread. Do NOT call from within
// the audio callback.
void playSound(SoundHandle handle, float volume = 1.0f);

// ---------------------------------------------------------------------------
// Playback — music
// ---------------------------------------------------------------------------
//
// One music track at a time. Music runs as a streaming decode directly in the
// audio callback thread (stb_vorbis per-callback decode). Low memory overhead:
// ~64 KB working memory per open track.
//
// Music volume is independent of the master (sound effects) volume. Both are
// applied multiplicatively in the mixer.
//
// All music functions are no-ops if audio is unavailable or in headless mode.

// Play a music track loaded by loadSound().
//
// If a track is already playing it is stopped first, then the new track starts.
// loop: if true, the track loops indefinitely; if false, it plays once.
//
// No-op if: handle is invalid, audio is unavailable, or in headless mode.
void playMusic(SoundHandle handle, bool loop = true);

// Stop the currently playing music track immediately.
// No-op if no music is playing.
void stopMusic();

// Set/get music volume, independent of the sound-effects master volume.
// Clamped to [0.0, 1.0]. NaN/Inf -> 0.0.
void setMusicVolume(float volume);
float getMusicVolume();

// Returns true if a music track is currently playing or looping.
// Reads an atomic — safe to call per-frame.
bool isMusicPlaying();

// ---------------------------------------------------------------------------
// Volume control
// ---------------------------------------------------------------------------

// Set the master volume applied to all audio output.
// Clamped to [0.0, 1.0]. Values outside this range are clamped.
// NaN and Inf are treated as 0.0.
// No-op if audio system is not initialised.
void setMasterVolume(float volume);

// Query the current master volume [0.0, 1.0].
float getMasterVolume();

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

// Returns true if the audio device was successfully opened.
// Returns false if init() failed, was called with headless=true,
// or was never called.
bool isAudioAvailable();

// Returns the number of sound voices currently active [0, MAX_AUDIO_VOICES].
// Reads an atomic counter — no mutex, safe to call per-frame for diagnostics.
u32 getActiveVoiceCount();

} // namespace ffe::audio
