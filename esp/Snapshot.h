#pragma once
#include <mutex>
#include <vector>
#include <atomic>
#include <cstdint>
#include "Config.h"
#include "VectorMath.h"
#include "EntityReader.h"

// ---------------------------------------------------------------------------
// Snapshot -- the thread-safe hand-off point between the three threads:
//
//   render thread (main) : ImGui menu + ESP drawing, ZERO memory reads
//   scan   thread        : full entity scans at settings.scanRate
//   last-hit thread      : high-rate creep HP tracking + prediction + input
//
// The render thread copies what it needs under short mutex locks and then
// draws from its own buffers, so heavy scans can never stall a frame and
// drawing can never stall the scan. This is what keeps the overlay at full
// frame rate with the menu open (the old build ran everything on one thread
// and dropped to ~30 FPS with a laggy cursor).
// ---------------------------------------------------------------------------

// High-frequency data the last-hit thread needs from the scan thread.
struct ScanMeta {
	std::vector<CreepCacheEntry> creeps;   // creeps near the local hero
	EntityReader::LocalState local{};      // cached local hero (pawn ptr etc.)
	bool localValid = false;
	ViewMatrix vm{};
	int screenW = 0, screenH = 0;
	std::vector<Vector2> rangeCircle;      // attack-range circle points
};

// High-frequency creep markers + live stats produced by the last-hit engine.
struct LhFrame {
	std::vector<CreepTarget> creepMarkers;
	LastHitStats stats{};
};

class Snapshot {
public:
	std::mutex mxScan;                 // guards scanOut + meta
	std::mutex mxLh;                   // guards lh
	EntityReader::ScanOutput scan;     // heroes, wards, roshan, runes, ghosts, pings, feed
	ScanMeta meta;
	LhFrame lh;

	std::atomic<int> clientX{ 0 };     // game client-area origin in SCREEN px
	std::atomic<int> clientY{ 0 };

	void PublishScan(EntityReader::ScanOutput&& out, ScanMeta&& m) {
		std::lock_guard<std::mutex> g(mxScan);
		scan = std::move(out);
		meta = std::move(m);
	}

	void PublishLh(std::vector<CreepTarget>&& markers, const LastHitStats& stats) {
		std::lock_guard<std::mutex> g(mxLh);
		lh.creepMarkers = std::move(markers);
		lh.stats = stats;
	}

	void ClearLh() {
		std::lock_guard<std::mutex> g(mxLh);
		lh.creepMarkers.clear();
		lh.stats = LastHitStats{};
	}
};
