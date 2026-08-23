#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

struct Vector3 {
	float x = 0, y = 0, z = 0;
};

struct Vector2 {
	float x = 0, y = 0;
};

struct ViewMatrix {
	float m[4][4];
};

struct EspSettings {
	bool enabled = true;
	bool showEnemies = true;
	bool showAllies = true;
	bool showHealthBar = true;
	bool showNames = true;
	bool showDistance = true;
	bool cornerStyle = true;
	bool showLastHitHelper = true;
	float boxThickness = 2.2f;
	float smoothing = 0.35f;
};

struct EspTarget {
	uintptr_t id = 0;
	float x = 0, y = 0, w = 0, h = 0;
	int health = 0, maxHealth = 0;
	float distance = 0;
	std::string name;
	bool enemy = false;
};

struct CreepTarget {
	uintptr_t id = 0;
	float x = 0, y = 0, w = 0, h = 0;
	int health = 0, maxHealth = 0;
	float distance = 0;
	bool isKillableNow = false;
	bool isKillableSoon = false;
	float timeToKillable = 0.0f;
	std::string name;
};

struct Module {
	std::string name;
	bool* enabled;
	std::function<void()> drawSettings;
};

struct Category {
	std::string name;
	std::vector<Module> modules;
};
