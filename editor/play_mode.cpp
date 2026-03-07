#include "play_mode.h"

#include "core/logging.h"
#include "scene/scene_serialiser.h"

namespace ffe::editor {

void PlayMode::play(ffe::World& world) {
    if (m_state != PlayState::EDITING) {
        return;
    }

    m_snapshot = ffe::scene::serialiseToJson(world);
    m_state = PlayState::PLAYING;
    FFE_LOG_INFO("Editor", "Play mode started — scene snapshot captured");
}

void PlayMode::pause() {
    if (m_state != PlayState::PLAYING) {
        return;
    }

    m_state = PlayState::PAUSED;
    FFE_LOG_INFO("Editor", "Play mode paused");
}

void PlayMode::resume() {
    if (m_state != PlayState::PAUSED) {
        return;
    }

    m_state = PlayState::PLAYING;
    FFE_LOG_INFO("Editor", "Play mode resumed");
}

void PlayMode::stop(ffe::World& world) {
    if (m_state == PlayState::EDITING) {
        return;
    }

    world.clearAllEntities();

    if (!m_snapshot.empty()) {
        const bool ok = ffe::scene::deserialiseFromJson(world, m_snapshot);
        if (!ok) {
            FFE_LOG_ERROR("Editor", "Failed to restore scene snapshot on stop");
        }
    }

    m_snapshot.clear();
    m_snapshot.shrink_to_fit(); // Release memory held by the snapshot
    m_state = PlayState::EDITING;
    FFE_LOG_INFO("Editor", "Play mode stopped — scene restored");
}

} // namespace ffe::editor
