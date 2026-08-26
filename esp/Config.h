#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <functional>
#include "VectorMath.h"

// ---------------------------------------------------------------------------
// Fixed-capacity string to avoid per-frame heap churn (FPS optimization).
// ---------------------------------------------------------------------------
template <int N>
struct FixedStr {
	char buf[N] = {};
	void set(const char* s) {
		if (!s) { buf[0] = 0; return; }
		int i = 0;
		while (s[i] && i < N - 1) { buf[i] = s[i]; i++; }
		buf[i] = 0;
	}
	void setTrunc(const char* s, int maxChars) {
		set(s);
		if (maxChars > 0 && maxChars < N - 1) buf[maxChars] = 0;
	}
	const char* c_str() const { return buf; }
};

struct EspSettings {
	// --- Visuals ---
	bool enabled = true;
	bool showEnemies = true;
	bool showAllies = false;        // per request: allies hidden by default
	bool showHealthBar = true;
	bool showManaBar = true;
	bool showIllusions = true;
	bool showNames = true;
	bool showDistance = true;
	bool cornerStyle = true;
	bool showGlow = true;
	bool showOffscreen = true;      // fallback arrows for heroes out of view
	float boxThickness = 2.0f;
	float smoothing = 0.35f;
	float maxDrawDistance = 6000.0f; // world units

	// --- Players (Heroes Information, read-only) ---
	bool showHeroLevel = true;
	bool showPlayerNames = true;
	bool showItems = true;          // read-only inventory snapshot
	bool showInvisTag = true;       // "INVISIBLE" tag (m_flInvisibilityLevel)
	bool showAttackRange = false;   // local hero attack-range circle

	// --- World (map-hack style, 100% read-only) ---
	bool showWards = true;          // enemy observer/sentry wards
	bool showRoshan = true;         // Roshan HP bar
	bool showRunes = true;          // rune pickups
	bool showGhosts = true;         // last-known-position fallback

	// --- Trackers (enemy activity, read-only) ---
	bool showCastTracker = true;    // cast / ult / channel location pings
	bool showJungleTracker = true;  // enemy jungling indicator
	bool showRoshanLog = true;      // "X is attacking Roshan" feed + console
	float castMarkerTime = 6.0f;    // seconds a cast ping stays visible

	// --- Last Hit helper ---
	bool showLastHitHelper = true;  // colored markers on killable creeps
	bool showLastHitAnnounce = false; // "LAST HIT"/"DENY" text -- OFF per request
	bool autoLastHit = false;       // hold trigger key: auto last-hit + deny
	int  triggerKey = 'X';          // key that activates auto last-hit/deny
	int  attackKey = 'A';           // the Dota 2 "Attack" bind we tap
	bool lastHitPriority = true;    // true: last-hit beats deny when both up
	float denyPct = 50.0f;          // ally creep HP% below which deny is possible
	float clickCooldown = 0.30f;    // seconds between auto attack actions

	// --- Misc ---
	bool showFps = true;
	bool showWatermark = true;
	float ghostFadeTime = 15.0f;    // seconds to keep last-known markers (hard cap)
	float scanRate = 30.0f;         // memory scans per second (perf)
	float itemRefreshInterval = 1.0f; // seconds between inventory re-reads

	// --- Theme accent (Settings tab presets) ---
	float accent[3] = { 0.05f, 0.85f, 0.75f };
};

struct EspTarget {
	uintptr_t id = 0;
	float x = 0, y = 0, w = 0, h = 0;
	int health = 0, maxHealth = 0;
	int mana = 0, maxMana = 0;
	int level = 0;                  // 0 = unknown -> level badge hidden
	float distance = 0;
	bool enemy = false;
	bool isIllusion = false;
	bool invis = false;
	bool offscreen = false;         // projects outside the viewport
	Vector2 screenCenter = {};      // for edge arrows
	FixedStr<28> name;
	FixedStr<24> playerName;
	FixedStr<24> items[6];          // read-only inventory snapshot
	int itemCount = 0;
};

struct CreepTarget {
	uintptr_t id = 0;
	float x = 0, y = 0, w = 0, h = 0;
	int health = 0, maxHealth = 0;
	float distance = 0;
	bool isKillableNow = false;
	bool isKillableSoon = false;
	bool isDenyable = false;
	bool isAlly = false;
	FixedStr<40> name;
};

enum MarkerKind : int {
	Marker_WardObserver = 0,
	Marker_WardSentry = 1,
	Marker_Roshan = 2,
	Marker_Rune = 3,
};

struct MarkerTarget {
	int kind = 0;
	Vector3 origin = {};
	Vector2 screen = {};
	bool onScreen = false;
	int health = 0, maxHealth = 0;
	float distance = 0;
	FixedStr<28> name;
};

// Hero that could not be read/projected this frame -> drawn from its
// last-known position (the fallback the user asked for).
struct GhostTarget {
	uintptr_t id = 0;
	Vector3 origin = {};
	Vector2 screen = {};
	bool onScreen = false;
	bool enemy = true;
	bool invis = false;
	float age = 0.0f;               // seconds since last seen
	float distance = 0;
	FixedStr<28> name;
};
