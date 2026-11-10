// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
namespace o1602::core::profile {
enum Metric { Events, Input, Simulation, Render, Sprites, Submit, Present, Wait, Audio, Decode,
              Video, FileIO, AtlasDecode, AtlasPack, Masks, Upload, CacheMiss, LogIO, AI, Commands, Mixer, Bind, LogBackground, AtlasCompress, SpriteInflate, MusicRead, SoundRead, NetworkHost, NetworkClient, NetworkHash, NetworkPoll, CursorTile, CursorUnits, HoverStatus, HudResources, CursorTileCompute, VideoDecode, VideoConvert, VideoUpload, SpritePrefetch, PresentInterval, InputPresent, SeaPath, AmbientTick, HudDraw, TextureCreate, SeaFill, SeaScan, SeaSearch, SpriteWait, SpriteReadMain, SpriteReadWorker, HudResourcesDraw, HudPanelDraw, HudMinimapDraw, HudStatusDraw, HudChatDraw, SaveSerialize, SaveWrite, EventDispatch, NetworkSelect, MenuOpen, SpriteBackingIO, SpriteFileLock, SpriteBlockWait, GoodsLayout, GoodsDraw, DiagnosticHash, Count };
bool enabled() noexcept;
void set_enabled(bool) noexcept;
void add(Metric metric, std::uint64_t ns) noexcept;
void frame(std::uint64_t ns) noexcept;
void begin() noexcept;
void mark(const char* event, std::uint64_t tick=0) noexcept;
enum Scene { Menu, World, VideoScene, Loading, BenchmarkIdle, BenchmarkCursor, SceneCount };
void scene(Scene) noexcept;
// Single producer (frame thread), single consumer (diagnostic writer). The
// producer never waits for serialization, sorting or disk I/O; busy snapshots skip.
bool capture() noexcept;
std::string captured_summary();
enum Gauge { HeapUsed, HeapArena, GpuCacheBytes, CacheMisses, CacheUploads, CacheEvictions,
             AudioQueuedFrames, AudioQueueErrors, AudioEmptyObservations, FreeUser, FreeCdram, MapCanvasBytes, MapCanvasAllocations, MusicStarves, MusicBufferBytes, SpriteRamBytes, SpriteDiskReads, EffectCacheBytes, EffectPending, CpuMHz, GpuMHz, BusMHz, XbarMHz, SpritePrefetchBytes, SpritePrefetchHits, SpritePrefetchJobs, UiPanelBytes, UiPanelHits, UiPanelBuilds, SeaPlans, SeaVisited, SpriteSyncReads, PrefetchReady, PrefetchJoined, PrefetchRejectedJobs, PrefetchRejectedBytes, SpriteReadCalls, SpriteReadBytes, SpriteBlockHits, SpriteBlockBytes, VideoCompressedBytes, WorldZoom, UiWindowBytes, UiWindowHits, UiWindowBuilds, GaugeCount };
void gauge(Gauge, std::uint64_t) noexcept;
void increment(Gauge, std::uint64_t amount=1) noexcept;
std::string summary();
class Scope {
public:
    explicit Scope(Metric metric) noexcept : m_metric(metric), m_enabled(enabled()), m_start(m_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
    ~Scope() { if(m_enabled)add(m_metric, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-m_start).count())); }
private:
    Metric m_metric;
    bool m_enabled;
    std::chrono::steady_clock::time_point m_start;
};
}
