// mesh_loader.cpp — cgltf-based .glb mesh loading for FFE.
//
// cgltf v1.14 — https://github.com/jkuhlmann/cgltf
// cgltf is a single-header C library embedded in third_party/cgltf.h.
// CGLTF_IMPLEMENTATION is defined in exactly this file and no other.
//
// Security constraints implemented (from ADR-007-security-review.md):
//   SEC-M1: Path traversal prevention (identical to texture_loader pattern).
//   SEC-M2: stat() file size check before any cgltf call. Rejects > 64 MB.
//   SEC-M3: cgltf_validate() + buffer.data non-null check after cgltf_load_buffers (H-2).
//           Exclusive use of cgltf_accessor_read_float/index safe accessor API.
//   SEC-M4: Vertex and index count limits checked before any allocation.
//   SEC-M5: u64 arithmetic for all size calculations — no 32-bit overflow.
//   SEC-M6: cgltf_free() called on every exit path after cgltf_parse_file succeeds.
//   SEC-M7: No per-frame loading. Header comment states this constraint.
//   SEC-M8: .glb-only restriction. Extension check is the first path check.
//   M-2:    Heap fallback uses new(std::nothrow); null-checked before use.
//   M-3:    glGetError() checked after glBufferData — GPU OOM detection.

// Suppress warnings cgltf generates under -Wall -Wextra (it's a C library).
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wsign-conversion"
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
    #pragma clang diagnostic ignored "-Wcast-qual"
    #pragma clang diagnostic ignored "-Wimplicit-fallthrough"
    #pragma clang diagnostic ignored "-Wreserved-identifier"
    #pragma clang diagnostic ignored "-Wdouble-promotion"
    #pragma clang diagnostic ignored "-Wunused-function"
    #pragma clang diagnostic ignored "-Wcast-align"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
    #pragma clang diagnostic ignored "-Wpadded"
    #pragma clang diagnostic ignored "-Wshadow"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wdouble-promotion"
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #pragma GCC diagnostic ignored "-Wpadded"
    #pragma GCC diagnostic ignored "-Wshadow"
    #pragma GCC diagnostic ignored "-Wcast-align"
#endif

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#ifdef __clang__
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include "renderer/mesh_loader.h"
#include "renderer/skeleton.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/opengl/rhi_opengl.h"
#include "renderer/texture_loader.h"
#include "core/logging.h"

#include <glad/glad.h>

// GL 3.0 core function missing from our GLAD generation.
// glVertexAttribIPointer is required for integer vertex attributes (bone indices).
// We load it via glfwGetProcAddress once, at first use.
#include <climits>    // PATH_MAX
#include <cstring>    // strnlen, strstr, memcpy, strcasecmp equivalent
#include <cctype>     // tolower
#include <sys/stat.h> // stat()
#include <stdlib.h>   // size_t
#include "core/platform.h"
#include <cinttypes>  // PRIu64
#include <memory>     // std::unique_ptr, std::make_unique
#include <new>        // std::nothrow
#include <cmath>      // sqrtf (flat normal computation)
#include <glm/gtc/type_ptr.hpp>  // glm::make_mat4

namespace ffe::renderer {

// MeshGpuRecord is defined in mesh_loader.h so that mesh_renderer.cpp can
// dereference pointers returned by getMeshGpuRecord() without an incomplete-type
// error. The GLuint fields are compatible with the unsigned int members declared
// in the header because GLuint is typedef'd to unsigned int on all supported targets.

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

static MeshGpuRecord  s_meshPool[MAX_MESH_ASSETS + 1]; // slot 0 reserved (null handle)
static SkeletonData   s_skeletonPool[MAX_MESH_ASSETS + 1];   // parallel array, indexed by MeshHandle::id
static std::unique_ptr<MeshAnimations> s_animationPool[MAX_MESH_ASSETS + 1];  // parallel array, indexed by MeshHandle::id
static u32            s_nextMeshSlot = 1;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// isPathSafe — SEC-M1: path traversal prevention.
// MUST be the first operation called on any caller-supplied path.
// Returns true only if the path is safe to concatenate and pass to realpath().
static bool isPathSafe(const char* const path) {
    if (!path) {
        return false;
    }
    const size_t pathLen = strnlen(path, PATH_MAX);
    if (pathLen >= PATH_MAX) {
        return false;
    }
    if (path[0] == '\0') {
        return false;
    }
    // Reject absolute Unix paths
    if (path[0] == '/') {
        return false;
    }
    // Reject absolute Windows paths (backslash root)
    if (path[0] == '\\') {
        return false;
    }
    // Reject drive letters (e.g., "C:")
    if (pathLen >= 2 && path[1] == ':') {
        return false;
    }
    // Reject UNC paths ("\\server\share")
    if (path[0] == '\\' && pathLen >= 2 && path[1] == '\\') {
        return false;
    }
    // Reject traversal sequences
    if (strstr(path, "../") != nullptr) { return false; }
    if (strstr(path, "..\\") != nullptr) { return false; }
    if (strstr(path, "/..") != nullptr) { return false; }
    if (strstr(path, "\\..") != nullptr) { return false; }
    // Reject Windows Alternate Data Streams (e.g. "models/mesh.glb:stream").
    // Legitimate relative asset paths never contain ':'.
    // Drive-letter absolute paths ("C:\...") are already rejected above by the
    // path[1]==':' check, so any remaining ':' is always an ADS or device path.
    if (strchr(path, ':') != nullptr) { return false; }
    return true;
}

// hasGlbExtension — SEC-M8: reject any path that doesn't end with ".glb"
// (case-insensitive). Performed after isPathSafe but before any file I/O.
static bool hasGlbExtension(const char* const path) {
    const size_t len = strnlen(path, PATH_MAX);
    if (len < 4) {
        return false; // shorter than ".glb\0" — can't possibly end with .glb
    }
    // Check last 4 characters case-insensitively
    const char* ext = path + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'g' || ext[1] == 'G') &&
            (ext[2] == 'l' || ext[2] == 'L') &&
            (ext[3] == 'b' || ext[3] == 'B'));
}

// findFreeSlot — linear scan for a free MeshGpuRecord slot.
// Only called at load time (cold path).
static u32 findFreeMeshSlot() {
    // Try the fast path: next sequential slot
    if (s_nextMeshSlot <= MAX_MESH_ASSETS && !s_meshPool[s_nextMeshSlot].alive) {
        return s_nextMeshSlot++;
    }
    // Linear scan for a free slot
    for (u32 i = 1; i <= MAX_MESH_ASSETS; ++i) {
        if (!s_meshPool[i].alive) {
            if (i >= s_nextMeshSlot) s_nextMeshSlot = i + 1;
            return i;
        }
    }
    return 0; // No free slots
}

// countAliveSlots — for pool utilisation warning.
static u32 countAliveSlots() {
    u32 count = 0;
    for (u32 i = 1; i <= MAX_MESH_ASSETS; ++i) {
        if (s_meshPool[i].alive) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// loadMesh implementation
// ---------------------------------------------------------------------------

MeshHandle loadMesh(const char* const path) {

    // --- SEC-M1: Path safety (traversal prevention) — FIRST check ---
    if (!isPathSafe(path)) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: unsafe path rejected: \"%s\"",
                      path ? path : "(null)");
        return MeshHandle{0};
    }

    // --- SEC-M8: .glb-only restriction ---
    if (!hasGlbExtension(path)) {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: only .glb files are accepted (path: \"%s\")", path);
        return MeshHandle{0};
    }

    // --- Asset root check ---
    const char* const assetRoot = getAssetRoot();
    if (assetRoot == nullptr || assetRoot[0] == '\0') {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: asset root not set — call setAssetRoot() first");
        return MeshHandle{0};
    }

    // --- Pool cap check (before file I/O — SEC-M7 defence) ---
    const u32 aliveCount = countAliveSlots();
    if (aliveCount >= MAX_MESH_ASSETS) {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: mesh pool full (%u/%u) — unload meshes before loading new ones",
                      aliveCount, MAX_MESH_ASSETS);
        return MeshHandle{0};
    }

    // --- Pool utilisation warning at 75% ---
    if (aliveCount >= (MAX_MESH_ASSETS * 3 / 4)) {
        FFE_LOG_WARN("MeshLoader",
                     "loadMesh: mesh pool at %u/%u slots (>75%% full)",
                     aliveCount, MAX_MESH_ASSETS);
    }

    // --- Build full path: assetRoot + "/" + path ---
    const size_t rootLen = strnlen(assetRoot, PATH_MAX);
    const size_t pathLen = strnlen(path, PATH_MAX);
    // HIGH-2 equivalent: check combined path length before concatenation
    if (rootLen + 1u + pathLen + 1u > static_cast<size_t>(PATH_MAX) + 1u) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: concatenated path too long");
        return MeshHandle{0};
    }

    char fullPath[PATH_MAX + 1];
    memcpy(fullPath, assetRoot, rootLen);
    fullPath[rootLen] = '/';
    memcpy(fullPath + rootLen + 1u, path, pathLen);
    fullPath[rootLen + 1u + pathLen] = '\0';

    // --- SEC-M1 (continued): canonicalize path to resolve symlinks and encoded traversal ---
    char canonPath[PATH_MAX + 1];
    if (!ffe::canonicalizePath(fullPath, canonPath, sizeof(canonPath))) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: canonicalizePath() failed for \"%s\"", fullPath);
        return MeshHandle{0};
    }

    // Verify the canonical path begins with the asset root prefix
    if (strncmp(canonPath, assetRoot, rootLen) != 0) {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: canonical path \"%s\" escapes asset root \"%s\"",
                      canonPath, assetRoot);
        return MeshHandle{0};
    }
    // Confirm the separator immediately follows the root prefix
    if (canonPath[rootLen] != '/' && canonPath[rootLen] != '\0') {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: canonical path \"%s\" escapes asset root \"%s\"",
                      canonPath, assetRoot);
        return MeshHandle{0};
    }

    // --- SEC-M2: File size check before any cgltf call ---
    {
        struct stat fileStat{};
        if (::stat(canonPath, &fileStat) != 0) {
            FFE_LOG_ERROR("MeshLoader", "loadMesh: file not found or stat() failed: \"%s\"",
                          canonPath);
            return MeshHandle{0};
        }
        const u64 fileSize = static_cast<u64>(fileStat.st_size);
        if (fileSize > MESH_FILE_SIZE_LIMIT) {
            FFE_LOG_ERROR("MeshLoader",
                          "loadMesh: file too large (%" PRIu64 " bytes, max %" PRIu64 "): \"%s\"",
                          fileSize, MESH_FILE_SIZE_LIMIT, canonPath);
            return MeshHandle{0};
        }
    }

    // --- Parse the .glb file using cgltf ---
    cgltf_options options{};  // Zero-initialised: use cgltf defaults (system malloc/free)
    cgltf_data* data = nullptr;

    const cgltf_result parseResult = cgltf_parse_file(&options, canonPath, &data);
    if (parseResult != cgltf_result_success) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: cgltf_parse_file failed (result=%d) for \"%s\"",
                      static_cast<int>(parseResult), canonPath);
        // data may be non-null on partial parse — free it
        if (data != nullptr) {
            cgltf_free(data);
        }
        return MeshHandle{0};
    }

    // --- Load the embedded BIN chunk from the .glb ---
    const cgltf_result loadResult = cgltf_load_buffers(&options, data, canonPath);
    if (loadResult != cgltf_result_success) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: cgltf_load_buffers failed (result=%d) for \"%s\"",
                      static_cast<int>(loadResult), canonPath);
        cgltf_free(data); // SEC-M6
        return MeshHandle{0};
    }

    // --- SEC-M3 (H-2): Verify buffer data_size >= buffer.size for every buffer ---
    for (cgltf_size bi = 0; bi < data->buffers_count; ++bi) {
        const cgltf_buffer& buf = data->buffers[bi];
        if (buf.data == nullptr) {
            FFE_LOG_ERROR("MeshLoader",
                          "loadMesh: buffer[%zu].data is null after cgltf_load_buffers for \"%s\"",
                          bi, canonPath);
            cgltf_free(data); // SEC-M6
            return MeshHandle{0};
        }
        // cgltf_buffer has no data_size field; a non-null data pointer after
        // cgltf_load_buffers succeeds is the correct guarantee that buf.size
        // bytes have been loaded (cgltf loads the full declared size or fails).
    }

    // --- SEC-M3: cgltf_validate() ---
    const cgltf_result validateResult = cgltf_validate(data);
    if (validateResult != cgltf_result_success) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: cgltf_validate failed (result=%d) for \"%s\"",
                      static_cast<int>(validateResult), canonPath);
        cgltf_free(data); // SEC-M6
        return MeshHandle{0};
    }

    // --- SEC-M3: Verify at least one mesh and one primitive exist ---
    if (data->meshes_count == 0) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: no meshes in \"%s\"", canonPath);
        cgltf_free(data);
        return MeshHandle{0};
    }

    const cgltf_mesh& mesh = data->meshes[0];
    if (mesh.primitives_count == 0) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: mesh[0] has no primitives in \"%s\"", canonPath);
        cgltf_free(data);
        return MeshHandle{0};
    }

    const cgltf_primitive& prim = mesh.primitives[0];

    // --- SEC-M3: Verify primitive type is TRIANGLES ---
    if (prim.type != cgltf_primitive_type_triangles) {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: primitive type is not TRIANGLES (type=%d) in \"%s\"",
                      static_cast<int>(prim.type), canonPath);
        cgltf_free(data);
        return MeshHandle{0};
    }

    // --- Unindexed mesh support ---
    // glTF primitives may omit the indices accessor. In that case we generate
    // a trivial index buffer (0, 1, 2, ..., vertexCount-1) so the rest of the
    // renderer can use the indexed draw path uniformly.
    const bool isUnindexed = (prim.indices == nullptr);
    const cgltf_accessor* idxAccessor = prim.indices; // nullptr when unindexed

    // --- SEC-M3: Validate index accessor component type (only when indexed) ---
    if (!isUnindexed) {
        if (idxAccessor->component_type != cgltf_component_type_r_16u &&
            idxAccessor->component_type != cgltf_component_type_r_32u &&
            idxAccessor->component_type != cgltf_component_type_r_8u) {
            FFE_LOG_ERROR("MeshLoader",
                          "loadMesh: unsupported index component type (%d) in \"%s\"",
                          static_cast<int>(idxAccessor->component_type), canonPath);
            cgltf_free(data);
            return MeshHandle{0};
        }
    }

    // --- Locate POSITION, NORMAL, TEXCOORD_0, JOINTS_0, WEIGHTS_0 accessors ---
    const cgltf_accessor* posAccessor      = nullptr;
    const cgltf_accessor* normalAccessor   = nullptr;
    const cgltf_accessor* texcoordAccessor = nullptr;
    const cgltf_accessor* jointsAccessor   = nullptr;
    const cgltf_accessor* weightsAccessor  = nullptr;

    for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
        const cgltf_attribute& attr = prim.attributes[ai];
        if (attr.type == cgltf_attribute_type_position) {
            posAccessor = attr.data;
        } else if (attr.type == cgltf_attribute_type_normal) {
            normalAccessor = attr.data;
        } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
            texcoordAccessor = attr.data;
        } else if (attr.type == cgltf_attribute_type_joints && attr.index == 0) {
            jointsAccessor = attr.data;
        } else if (attr.type == cgltf_attribute_type_weights && attr.index == 0) {
            weightsAccessor = attr.data;
        }
    }

    // --- SEC-M3: POSITION is required ---
    if (posAccessor == nullptr) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: no POSITION attribute in \"%s\"", canonPath);
        cgltf_free(data);
        return MeshHandle{0};
    }

    // --- SEC-M3: Validate POSITION accessor type/component ---
    if (posAccessor->component_type != cgltf_component_type_r_32f ||
        posAccessor->type != cgltf_type_vec3) {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: POSITION accessor must be VEC3/F32 in \"%s\"", canonPath);
        cgltf_free(data);
        return MeshHandle{0};
    }

    // --- SEC-M3: Validate NORMAL accessor if present ---
    if (normalAccessor != nullptr &&
        (normalAccessor->component_type != cgltf_component_type_r_32f ||
         normalAccessor->type != cgltf_type_vec3)) {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: NORMAL accessor must be VEC3/F32 in \"%s\"", canonPath);
        cgltf_free(data);
        return MeshHandle{0};
    }

    // --- SEC-M4: Vertex and index count limits ---
    const u32 vertexCount = static_cast<u32>(posAccessor->count);
    // For unindexed meshes, index count equals vertex count (trivial 0..N-1).
    const u32 indexCount  = isUnindexed ? vertexCount
                                        : static_cast<u32>(idxAccessor->count);

    if (vertexCount > MAX_MESH_VERTICES) {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: vertex count %u exceeds MAX_MESH_VERTICES=%u in \"%s\"",
                      vertexCount, MAX_MESH_VERTICES, canonPath);
        cgltf_free(data);
        return MeshHandle{0};
    }

    if (indexCount > MAX_MESH_INDICES) {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: index count %u exceeds MAX_MESH_INDICES=%u in \"%s\"",
                      indexCount, MAX_MESH_INDICES, canonPath);
        cgltf_free(data);
        return MeshHandle{0};
    }

    if (indexCount == 0) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: zero indices in \"%s\"", canonPath);
        cgltf_free(data);
        return MeshHandle{0};
    }

    // --- Detect skinned mesh (has both JOINTS_0 and WEIGHTS_0 + a skin) ---
    const bool isSkinned = (jointsAccessor != nullptr && weightsAccessor != nullptr &&
                            data->skins_count > 0);

    // --- SEC-M5: u64 arithmetic for size calculations ---
    const u64 vertexStride = isSkinned ? sizeof(SkinnedMeshVertex) : sizeof(rhi::MeshVertex);
    const u64 vboBytes = static_cast<u64>(vertexCount) * vertexStride;
    const u64 iboBytes = static_cast<u64>(indexCount)  * sizeof(u32);

    // --- Parse skeleton and animation data BEFORE freeing cgltf (cold path) ---
    SkeletonData  parsedSkeleton;
    auto parsedAnimations = std::make_unique<MeshAnimations>();

    if (isSkinned) {
        const cgltf_skin& skin = data->skins[0];

        // Validate bone count
        if (skin.joints_count > MAX_BONES) {
            FFE_LOG_ERROR("MeshLoader",
                          "loadMesh: bone count %zu exceeds MAX_BONES=%u in \"%s\"",
                          skin.joints_count, MAX_BONES, canonPath);
            cgltf_free(data);
            return MeshHandle{0};
        }

        parsedSkeleton.boneCount = static_cast<u32>(skin.joints_count);

        // Read inverse bind matrices
        if (skin.inverse_bind_matrices != nullptr) {
            for (u32 bi = 0; bi < parsedSkeleton.boneCount; ++bi) {
                float mat[16];
                cgltf_accessor_read_float(skin.inverse_bind_matrices,
                                          static_cast<cgltf_size>(bi), mat, 16);
                // cgltf stores matrices in column-major order, same as glm
                parsedSkeleton.bones[bi].inverseBindMatrix = glm::make_mat4(mat);
            }
        }

        // Build parent indices from the node tree
        // First, create a mapping from cgltf_node* to bone index
        for (u32 bi = 0; bi < parsedSkeleton.boneCount; ++bi) {
            parsedSkeleton.bones[bi].parentIndex = -1; // default: root

            const cgltf_node* boneNode = skin.joints[bi];
            // Search for this bone's parent among the other joints
            if (boneNode != nullptr) {
                for (u32 pi = 0; pi < parsedSkeleton.boneCount; ++pi) {
                    if (pi == bi) continue;
                    const cgltf_node* parentCandidate = skin.joints[pi];
                    if (parentCandidate == nullptr) continue;
                    // Check if parentCandidate is the parent of boneNode
                    for (cgltf_size ci = 0; ci < parentCandidate->children_count; ++ci) {
                        if (parentCandidate->children[ci] == boneNode) {
                            parsedSkeleton.bones[bi].parentIndex = static_cast<i32>(pi);
                            break;
                        }
                    }
                    if (parsedSkeleton.bones[bi].parentIndex >= 0) break;
                }
            }
        }

        // --- Parse animations ---
        const u32 animCount = static_cast<u32>(
            data->animations_count < MAX_ANIMATIONS_PER_MESH
                ? data->animations_count : MAX_ANIMATIONS_PER_MESH);
        parsedAnimations->clipCount = animCount;

        for (u32 ai = 0; ai < animCount; ++ai) {
            const cgltf_animation& anim = data->animations[ai];
            AnimationClipData& clip = parsedAnimations->clips[ai];
            clip.duration = 0.0f;
            clip.channelCount = 0;

            for (cgltf_size ci = 0; ci < anim.channels_count; ++ci) {
                const cgltf_animation_channel& channel = anim.channels[ci];
                if (channel.target_node == nullptr || channel.sampler == nullptr) continue;

                // Map target node to bone index
                i32 boneIdx = -1;
                for (u32 bi = 0; bi < parsedSkeleton.boneCount; ++bi) {
                    if (skin.joints[bi] == channel.target_node) {
                        boneIdx = static_cast<i32>(bi);
                        break;
                    }
                }
                if (boneIdx < 0) continue; // target is not a bone in this skin

                AnimationChannel& outCh = clip.channels[boneIdx];
                outCh.boneIndex = static_cast<u32>(boneIdx);

                const cgltf_animation_sampler* sampler = channel.sampler;
                if (sampler->input == nullptr || sampler->output == nullptr) continue;

                // Parse interpolation mode from glTF sampler
                switch (sampler->interpolation) {
                    case cgltf_interpolation_type_step:
                        outCh.mode = InterpolationMode::STEP;
                        break;
                    case cgltf_interpolation_type_cubic_spline:
                        outCh.mode = InterpolationMode::CUBIC_SPLINE;
                        FFE_LOG_WARN("MeshLoader",
                                     "loadMesh: CUBIC_SPLINE interpolation not fully supported, "
                                     "falling back to LINEAR for bone %u in \"%s\"",
                                     static_cast<u32>(boneIdx), canonPath);
                        break;
                    default: // cgltf_interpolation_type_linear
                        outCh.mode = InterpolationMode::LINEAR;
                        break;
                }

                const u32 keyframeCount = static_cast<u32>(
                    sampler->input->count < MAX_KEYFRAMES_PER_CHANNEL
                        ? sampler->input->count : MAX_KEYFRAMES_PER_CHANNEL);

                if (keyframeCount > sampler->input->count) continue;

                switch (channel.target_path) {
                    case cgltf_animation_path_type_translation: {
                        outCh.translationCount = keyframeCount;
                        for (u32 ki = 0; ki < keyframeCount; ++ki) {
                            float t = 0.0f;
                            cgltf_accessor_read_float(sampler->input, static_cast<cgltf_size>(ki), &t, 1);
                            outCh.translationTimes[ki] = t;
                            if (t > clip.duration) clip.duration = t;

                            float val[3] = {0.0f, 0.0f, 0.0f};
                            cgltf_accessor_read_float(sampler->output, static_cast<cgltf_size>(ki), val, 3);
                            outCh.translationValues[ki] = {val[0], val[1], val[2]};
                        }
                        break;
                    }
                    case cgltf_animation_path_type_rotation: {
                        outCh.rotationCount = keyframeCount;
                        for (u32 ki = 0; ki < keyframeCount; ++ki) {
                            float t = 0.0f;
                            cgltf_accessor_read_float(sampler->input, static_cast<cgltf_size>(ki), &t, 1);
                            outCh.rotationTimes[ki] = t;
                            if (t > clip.duration) clip.duration = t;

                            float val[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                            cgltf_accessor_read_float(sampler->output, static_cast<cgltf_size>(ki), val, 4);
                            // glTF quaternion order: (x, y, z, w); glm::quat constructor: (w, x, y, z)
                            outCh.rotationValues[ki] = glm::quat(val[3], val[0], val[1], val[2]);
                        }
                        break;
                    }
                    case cgltf_animation_path_type_scale: {
                        outCh.scaleCount = keyframeCount;
                        for (u32 ki = 0; ki < keyframeCount; ++ki) {
                            float t = 0.0f;
                            cgltf_accessor_read_float(sampler->input, static_cast<cgltf_size>(ki), &t, 1);
                            outCh.scaleTimes[ki] = t;
                            if (t > clip.duration) clip.duration = t;

                            float val[3] = {1.0f, 1.0f, 1.0f};
                            cgltf_accessor_read_float(sampler->output, static_cast<cgltf_size>(ki), val, 3);
                            outCh.scaleValues[ki] = {val[0], val[1], val[2]};
                        }
                        break;
                    }
                    default:
                        break; // weights / other — skip
                }

                ++clip.channelCount;
            }
        }
    }

    // --- Allocate CPU-side staging buffers ---
    // Use heap allocation (new std::nothrow) for staging — this is a cold path.
    // M-2: use new(std::nothrow), check for null before proceeding.

    // For skinned meshes, allocate SkinnedMeshVertex; for static, MeshVertex.
    void* vertexData = nullptr;
    if (isSkinned) {
        auto* skinnedVerts = new(std::nothrow) SkinnedMeshVertex[vertexCount];
        if (skinnedVerts == nullptr) {
            FFE_LOG_ERROR("MeshLoader",
                          "loadMesh: failed to allocate skinned vertex staging buffer (%u vertices)", vertexCount);
            cgltf_free(data);
            return MeshHandle{0};
        }
        vertexData = skinnedVerts;
    } else {
        auto* staticVerts = new(std::nothrow) rhi::MeshVertex[vertexCount];
        if (staticVerts == nullptr) {
            FFE_LOG_ERROR("MeshLoader",
                          "loadMesh: failed to allocate vertex staging buffer (%u vertices)", vertexCount);
            cgltf_free(data);
            return MeshHandle{0};
        }
        vertexData = staticVerts;
    }

    u32* const indices = new(std::nothrow) u32[indexCount];
    if (indices == nullptr) {
        FFE_LOG_ERROR("MeshLoader",
                      "loadMesh: failed to allocate index staging buffer (%u indices)", indexCount);
        if (isSkinned) {
            delete[] static_cast<SkinnedMeshVertex*>(vertexData);
        } else {
            delete[] static_cast<rhi::MeshVertex*>(vertexData);
        }
        cgltf_free(data); // SEC-M6
        return MeshHandle{0};
    }

    // --- SEC-M3 (H-2): Extract vertex data using ONLY the safe accessor API ---
    // cgltf_accessor_read_float / cgltf_accessor_read_index perform bounds checking.
    // Direct pointer arithmetic into buffer_view->data is prohibited.

    if (isSkinned) {
        auto* skinnedVerts = static_cast<SkinnedMeshVertex*>(vertexData);
        for (u32 vi = 0; vi < vertexCount; ++vi) {
            SkinnedMeshVertex& v = skinnedVerts[vi];

            // Position (required)
            float pos[3] = {0.0f, 0.0f, 0.0f};
            cgltf_accessor_read_float(posAccessor, static_cast<cgltf_size>(vi), pos, 3);
            v.px = pos[0]; v.py = pos[1]; v.pz = pos[2];

            // Normal
            float norm[3] = {0.0f, 1.0f, 0.0f};
            if (normalAccessor != nullptr) {
                cgltf_accessor_read_float(normalAccessor, static_cast<cgltf_size>(vi), norm, 3);
            }
            v.nx = norm[0]; v.ny = norm[1]; v.nz = norm[2];

            // Texcoord
            float uv[2] = {0.0f, 0.0f};
            if (texcoordAccessor != nullptr) {
                cgltf_accessor_read_float(texcoordAccessor, static_cast<cgltf_size>(vi), uv, 2);
            }
            v.u = uv[0]; v.v = uv[1];

            // Joint indices — read as uint via cgltf_accessor_read_uint
            cgltf_uint joints[4] = {0, 0, 0, 0};
            cgltf_accessor_read_uint(jointsAccessor, static_cast<cgltf_size>(vi), joints, 4);

            // Validate joint indices: must be < boneCount
            for (int j = 0; j < 4; ++j) {
                if (joints[j] >= parsedSkeleton.boneCount) {
                    FFE_LOG_ERROR("MeshLoader",
                                  "loadMesh: vertex %u joint[%d]=%u >= boneCount=%u in \"%s\"",
                                  vi, j, joints[j], parsedSkeleton.boneCount, canonPath);
                    delete[] skinnedVerts;
                    delete[] indices;
                    cgltf_free(data);
                    return MeshHandle{0};
                }
            }
            v.jx = static_cast<u16>(joints[0]);
            v.jy = static_cast<u16>(joints[1]);
            v.jz = static_cast<u16>(joints[2]);
            v.jw = static_cast<u16>(joints[3]);

            // Weights
            float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            cgltf_accessor_read_float(weightsAccessor, static_cast<cgltf_size>(vi), weights, 4);

            // Renormalize weights if they don't sum to 1.0
            const float weightSum = weights[0] + weights[1] + weights[2] + weights[3];
            if (weightSum > 0.0001f && (weightSum < 0.999f || weightSum > 1.001f)) {
                const float invSum = 1.0f / weightSum;
                weights[0] *= invSum;
                weights[1] *= invSum;
                weights[2] *= invSum;
                weights[3] *= invSum;
            } else if (weightSum <= 0.0001f) {
                // Zero weights — assign fully to first joint
                weights[0] = 1.0f;
                weights[1] = 0.0f;
                weights[2] = 0.0f;
                weights[3] = 0.0f;
            }
            v.wx = weights[0]; v.wy = weights[1]; v.wz = weights[2]; v.ww = weights[3];
        }

        // --- Flat normal generation for unindexed skinned meshes with no NORMAL accessor ---
        // glTF unindexed triangle lists store vertices in sequential triplets: (v0,v1,v2), (v3,v4,v5)...
        // When no normals are supplied, compute face normals from cross(edge1, edge2) and assign
        // the same flat normal to all three vertices of each triangle.
        // OpenGL default winding: counter-clockwise = front face.
        if (isUnindexed && normalAccessor == nullptr) {
            const u32 triCount = vertexCount / 3u;
            for (u32 ti = 0; ti < triCount; ++ti) {
                SkinnedMeshVertex& v0 = skinnedVerts[ti * 3u + 0u];
                SkinnedMeshVertex& v1 = skinnedVerts[ti * 3u + 1u];
                SkinnedMeshVertex& v2 = skinnedVerts[ti * 3u + 2u];

                // Edge vectors
                const float e1x = v1.px - v0.px;
                const float e1y = v1.py - v0.py;
                const float e1z = v1.pz - v0.pz;
                const float e2x = v2.px - v0.px;
                const float e2y = v2.py - v0.py;
                const float e2z = v2.pz - v0.pz;

                // Cross product: e1 x e2 (CCW winding gives outward-facing normal)
                float nx = e1y * e2z - e1z * e2y;
                float ny = e1z * e2x - e1x * e2z;
                float nz = e1x * e2y - e1y * e2x;

                // Normalise
                const float len = sqrtf(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) {
                    const float invLen = 1.0f / len;
                    nx *= invLen;
                    ny *= invLen;
                    nz *= invLen;
                } else {
                    // Degenerate triangle — fall back to up vector
                    nx = 0.0f; ny = 1.0f; nz = 0.0f;
                }

                v0.nx = nx; v0.ny = ny; v0.nz = nz;
                v1.nx = nx; v1.ny = ny; v1.nz = nz;
                v2.nx = nx; v2.ny = ny; v2.nz = nz;
            }
            FFE_LOG_INFO("MeshLoader",
                         "loadMesh: computed flat normals for %u triangles (unindexed skinned mesh, no NORMAL in \"%s\")",
                         triCount, canonPath);
        }
    } else {
        auto* staticVerts = static_cast<rhi::MeshVertex*>(vertexData);
        for (u32 vi = 0; vi < vertexCount; ++vi) {
            rhi::MeshVertex& v = staticVerts[vi];

            // Position (required)
            float pos[3] = {0.0f, 0.0f, 0.0f};
            cgltf_accessor_read_float(posAccessor, static_cast<cgltf_size>(vi), pos, 3);
            v.px = pos[0]; v.py = pos[1]; v.pz = pos[2];

            // Normal (optional — default to {0,1,0} if absent)
            float norm[3] = {0.0f, 1.0f, 0.0f};
            if (normalAccessor != nullptr) {
                cgltf_accessor_read_float(normalAccessor, static_cast<cgltf_size>(vi), norm, 3);
            }
            v.nx = norm[0]; v.ny = norm[1]; v.nz = norm[2];

            // Texcoord (optional — default to {0,0} if absent)
            float uv[2] = {0.0f, 0.0f};
            if (texcoordAccessor != nullptr) {
                cgltf_accessor_read_float(texcoordAccessor, static_cast<cgltf_size>(vi), uv, 2);
            }
            v.u = uv[0]; v.v = uv[1];
        }

        // --- Flat normal generation for unindexed static meshes with no NORMAL accessor ---
        // Same logic as the skinned path above.
        if (isUnindexed && normalAccessor == nullptr) {
            const u32 triCount = vertexCount / 3u;
            for (u32 ti = 0; ti < triCount; ++ti) {
                rhi::MeshVertex& v0 = staticVerts[ti * 3u + 0u];
                rhi::MeshVertex& v1 = staticVerts[ti * 3u + 1u];
                rhi::MeshVertex& v2 = staticVerts[ti * 3u + 2u];

                const float e1x = v1.px - v0.px;
                const float e1y = v1.py - v0.py;
                const float e1z = v1.pz - v0.pz;
                const float e2x = v2.px - v0.px;
                const float e2y = v2.py - v0.py;
                const float e2z = v2.pz - v0.pz;

                float nx = e1y * e2z - e1z * e2y;
                float ny = e1z * e2x - e1x * e2z;
                float nz = e1x * e2y - e1y * e2x;

                const float len = sqrtf(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) {
                    const float invLen = 1.0f / len;
                    nx *= invLen;
                    ny *= invLen;
                    nz *= invLen;
                } else {
                    nx = 0.0f; ny = 1.0f; nz = 0.0f;
                }

                v0.nx = nx; v0.ny = ny; v0.nz = nz;
                v1.nx = nx; v1.ny = ny; v1.nz = nz;
                v2.nx = nx; v2.ny = ny; v2.nz = nz;
            }
            FFE_LOG_INFO("MeshLoader",
                         "loadMesh: computed flat normals for %u triangles (unindexed static mesh, no NORMAL in \"%s\")",
                         triCount, canonPath);
        }
    }

    // --- Extract index data ---
    if (isUnindexed) {
        // Generate trivial index buffer: 0, 1, 2, ..., vertexCount-1
        // This lets the rest of the renderer use the indexed draw path uniformly.
        for (u32 ii = 0; ii < indexCount; ++ii) {
            indices[ii] = ii;
        }
        FFE_LOG_INFO("MeshLoader",
                     "loadMesh: generated trivial index buffer (%u indices) for unindexed mesh in \"%s\"",
                     indexCount, canonPath);
    } else {
        // All indices widened to u32 (ADR-007 Section 13.7).
        for (u32 ii = 0; ii < indexCount; ++ii) {
            indices[ii] = static_cast<u32>(cgltf_accessor_read_index(idxAccessor,
                                                                      static_cast<cgltf_size>(ii)));
        }
    }

    // --- SEC-M6: cgltf data no longer needed — free before GPU upload ---
    cgltf_free(data);
    data = nullptr;

    // --- Helper lambda to free vertex data ---
    auto freeVertexData = [&]() {
        if (isSkinned) {
            delete[] static_cast<SkinnedMeshVertex*>(vertexData);
        } else {
            delete[] static_cast<rhi::MeshVertex*>(vertexData);
        }
    };

    // --- Allocate mesh slot ---
    const u32 slot = findFreeMeshSlot();
    if (slot == 0) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: no free mesh slots");
        freeVertexData();
        delete[] indices;
        return MeshHandle{0};
    }

    MeshGpuRecord& rec = s_meshPool[slot];

    // --- GPU upload ---
    // Step 9a: Create VBO via RHI (registers with RHI for VRAM tracking)
    rhi::BufferDesc vboDesc;
    vboDesc.type      = rhi::BufferType::VERTEX;
    vboDesc.usage     = rhi::BufferUsage::STATIC;
    vboDesc.data      = vertexData;
    vboDesc.sizeBytes = static_cast<u32>(vboBytes);  // safe — vboBytes <= 56 MB < u32 max

    rec.vboHandle = rhi::createBuffer(vboDesc);
    if (!rhi::isValid(rec.vboHandle)) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: failed to create VBO for \"%s\"", canonPath);
        freeVertexData();
        delete[] indices;
        return MeshHandle{0};
    }

    // Get the raw GL ID from the RHI handle for VAO configuration
    rec.vboId = rhi::getGlBufferId(rec.vboHandle);

    // M-3: Check for GL_OUT_OF_MEMORY after VBO upload
    // Note: rhi::createBuffer calls glBufferData internally; we check error state here.
    {
        const GLenum glErr = glGetError();
        if (glErr == GL_OUT_OF_MEMORY) {
            FFE_LOG_ERROR("MeshLoader", "loadMesh: GPU OOM after VBO upload for \"%s\"", canonPath);
            rhi::destroyBuffer(rec.vboHandle);
            rec.vboHandle = {};
            rec.vboId = 0;
            freeVertexData();
            delete[] indices;
            return MeshHandle{0};
        }
    }

    // Step 9b: Create IBO via RHI
    rhi::BufferDesc iboDesc;
    iboDesc.type      = rhi::BufferType::INDEX;
    iboDesc.usage     = rhi::BufferUsage::STATIC;
    iboDesc.data      = indices;
    iboDesc.sizeBytes = static_cast<u32>(iboBytes);  // safe — iboBytes <= 12 MB < u32 max

    rec.iboHandle = rhi::createBuffer(iboDesc);
    if (!rhi::isValid(rec.iboHandle)) {
        FFE_LOG_ERROR("MeshLoader", "loadMesh: failed to create IBO for \"%s\"", canonPath);
        rhi::destroyBuffer(rec.vboHandle);
        rec.vboHandle = {};
        rec.vboId = 0;
        freeVertexData();
        delete[] indices;
        return MeshHandle{0};
    }

    rec.iboId = rhi::getGlBufferId(rec.iboHandle);

    // M-3: Check for GL_OUT_OF_MEMORY after IBO upload
    {
        const GLenum glErr = glGetError();
        if (glErr == GL_OUT_OF_MEMORY) {
            FFE_LOG_ERROR("MeshLoader", "loadMesh: GPU OOM after IBO upload for \"%s\"", canonPath);
            rhi::destroyBuffer(rec.iboHandle);
            rhi::destroyBuffer(rec.vboHandle);
            rec.iboHandle = {};
            rec.vboHandle = {};
            rec.iboId = 0;
            rec.vboId = 0;
            freeVertexData();
            delete[] indices;
            return MeshHandle{0};
        }
    }

    // CPU staging data is no longer needed after GPU upload
    freeVertexData();
    delete[] indices;

    // Step 9c-h: Create and configure the VAO for this mesh (GL 3.3 — no DSA)
    glGenVertexArrays(1, &rec.vaoId);
    glBindVertexArray(rec.vaoId);

    // Bind VBO inside VAO state capture
    glBindBuffer(GL_ARRAY_BUFFER, rec.vboId);

    if (isSkinned) {
        // Skinned vertex layout: SkinnedMeshVertex (56 bytes)
        //   location 0: position — 3 floats, offset 0
        //   location 1: normal   — 3 floats, offset 12
        //   location 2: texcoord — 2 floats, offset 24
        //   location 4: joints   — 4 u16,    offset 32 (ivec4 via glVertexAttribIPointer)
        //   location 5: weights  — 4 floats, offset 40
        // Note: location 3 (tangent) is skipped — skinned meshes currently
        // don't have tangent data. The shader handles tangent length = 0.

        const GLsizei stride = static_cast<GLsizei>(sizeof(SkinnedMeshVertex));

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(0));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(12));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(24));

        // location 4 — joints (integer attribute, must use glVertexAttribIPointer)
        glEnableVertexAttribArray(4);
        glVertexAttribIPointer(4, 4, GL_UNSIGNED_SHORT, stride,
                               reinterpret_cast<const void*>(32));

        // location 5 — weights
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(40));
    } else {
        // Static vertex layout: rhi::MeshVertex (32 bytes)
        //   location 0: position — 3 floats, offset 0,  stride 32
        //   location 1: normal   — 3 floats, offset 12, stride 32
        //   location 2: texcoord — 2 floats, offset 24, stride 32

        // location 0 — position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(rhi::MeshVertex)),
                              reinterpret_cast<const void*>(0));

        // location 1 — normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(rhi::MeshVertex)),
                              reinterpret_cast<const void*>(12));

        // location 2 — texcoord
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(rhi::MeshVertex)),
                              reinterpret_cast<const void*>(24));
    }

    // Bind IBO inside VAO state capture (recorded in VAO state)
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rec.iboId);

    // Unbind VAO first, then unbind buffers (order matters: VAO must be unbound before
    // unbinding IBO, otherwise the IBO unbind is recorded in the VAO state)
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // Note: do NOT unbind GL_ELEMENT_ARRAY_BUFFER after unbinding VAO — it's not stored outside VAO

    // --- Finalise the record ---
    rec.indexCount   = indexCount;
    rec.vertexCount  = vertexCount;
    rec.alive        = true;
    rec.hasSkeleton  = isSkinned;

    // Store skeleton and animation data in parallel arrays (cold path copy)
    if (isSkinned) {
        s_skeletonPool[slot]  = parsedSkeleton;
        s_animationPool[slot] = std::move(parsedAnimations);

        FFE_LOG_INFO("MeshLoader",
                     "loadMesh: loaded \"%s\" — %u vertices, %u indices, %u bones, %u animations (slot %u)",
                     path, vertexCount, indexCount,
                     parsedSkeleton.boneCount, s_animationPool[slot]->clipCount, slot);
    } else {
        s_skeletonPool[slot]  = SkeletonData();
        s_animationPool[slot].reset();

        FFE_LOG_INFO("MeshLoader",
                     "loadMesh: loaded \"%s\" — %u vertices, %u indices (slot %u)",
                     path, vertexCount, indexCount, slot);
    }

    return MeshHandle{slot};
}

// ---------------------------------------------------------------------------
// unloadMesh
// ---------------------------------------------------------------------------

void unloadMesh(const MeshHandle handle) {
    if (!isValid(handle)) {
        return;
    }
    if (handle.id > MAX_MESH_ASSETS) {
        return;
    }

    MeshGpuRecord& rec = s_meshPool[handle.id];
    if (!rec.alive) {
        return;
    }

    // Destroy VAO directly (not tracked by RHI — GL only)
    if (rec.vaoId != 0) {
        glDeleteVertexArrays(1, &rec.vaoId);
        rec.vaoId = 0;
    }

    // Destroy VBO and IBO through RHI (handles VRAM tracking)
    if (rhi::isValid(rec.iboHandle)) {
        rhi::destroyBuffer(rec.iboHandle);
        rec.iboHandle = {};
        rec.iboId = 0;
    }
    if (rhi::isValid(rec.vboHandle)) {
        rhi::destroyBuffer(rec.vboHandle);
        rec.vboHandle = {};
        rec.vboId = 0;
    }

    rec.indexCount   = 0;
    rec.vertexCount  = 0;
    rec.alive        = false;
    rec.hasSkeleton  = false;

    // Clear skeleton and animation data for this slot
    s_skeletonPool[handle.id]  = SkeletonData();
    s_animationPool[handle.id].reset();
}

// ---------------------------------------------------------------------------
// unloadAllMeshes
// ---------------------------------------------------------------------------

void unloadAllMeshes() {
    for (u32 i = 1; i <= MAX_MESH_ASSETS; ++i) {
        if (s_meshPool[i].alive) {
            unloadMesh(MeshHandle{i});
        }
    }
    s_nextMeshSlot = 1;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

u32 getMeshVertexCount(const MeshHandle handle) {
    if (!isValid(handle) || handle.id > MAX_MESH_ASSETS) {
        return 0;
    }
    const MeshGpuRecord& rec = s_meshPool[handle.id];
    return rec.alive ? rec.vertexCount : 0u;
}

u32 getMeshIndexCount(const MeshHandle handle) {
    if (!isValid(handle) || handle.id > MAX_MESH_ASSETS) {
        return 0;
    }
    const MeshGpuRecord& rec = s_meshPool[handle.id];
    return rec.alive ? rec.indexCount : 0u;
}

const MeshGpuRecord* getMeshGpuRecord(const MeshHandle handle) {
    if (!isValid(handle) || handle.id > MAX_MESH_ASSETS) {
        return nullptr;
    }
    const MeshGpuRecord& rec = s_meshPool[handle.id];
    return rec.alive ? &rec : nullptr;
}

const SkeletonData* getMeshSkeletonData(const MeshHandle handle) {
    if (!isValid(handle) || handle.id > MAX_MESH_ASSETS) {
        return nullptr;
    }
    const MeshGpuRecord& rec = s_meshPool[handle.id];
    if (!rec.alive || !rec.hasSkeleton) {
        return nullptr;
    }
    return &s_skeletonPool[handle.id];
}

const MeshAnimations* getMeshAnimations(const MeshHandle handle) {
    if (!isValid(handle) || handle.id > MAX_MESH_ASSETS) {
        return nullptr;
    }
    const MeshGpuRecord& rec = s_meshPool[handle.id];
    if (!rec.alive || !rec.hasSkeleton) {
        return nullptr;
    }
    const auto& animPtr = s_animationPool[handle.id];
    if (!animPtr) return nullptr;
    return animPtr.get();
}

} // namespace ffe::renderer
