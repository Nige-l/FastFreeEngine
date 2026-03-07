#pragma once

#include "core/ecs.h"

#include <string>

namespace ffe::editor {

enum class PlayState { EDITING, PLAYING, PAUSED };

// PlayMode manages the play-in-editor lifecycle: snapshot the ECS World
// before entering play mode, and restore it when the user hits "Stop".
// This allows testing gameplay without permanently modifying the scene.
//
// The snapshot is an in-memory JSON string produced by the scene serialiser.
// No file I/O occurs.
class PlayMode {
public:
    PlayState state() const { return m_state; }

    // Enter play mode: snapshot current scene state, set state to PLAYING.
    // No-op if already PLAYING or PAUSED.
    void play(ffe::World& world);

    // Pause: stop ticking but keep the current (modified) state.
    // Only valid when PLAYING; no-op otherwise.
    void pause();

    // Resume from pause back to PLAYING.
    // Only valid when PAUSED; no-op otherwise.
    void resume();

    // Stop: restore the snapshot taken at play(), return to EDITING.
    // Clears all entities then deserialises the snapshot.
    // Only valid when PLAYING or PAUSED; no-op otherwise.
    void stop(ffe::World& world);

private:
    PlayState m_state = PlayState::EDITING;
    std::string m_snapshot; // JSON string captured at play()
};

} // namespace ffe::editor
