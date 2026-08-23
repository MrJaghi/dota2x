#pragma once
#include <cstdint>
#include <cmath>
#include <math.h>

struct Vector3 {
	float x = 0, y = 0, z = 0;
};

struct Vector2 {
	float x = 0, y = 0;
};

struct ViewMatrix {
	float m[4][4];
};

inline bool WorldToScreen(const Vector3& worldPos, Vector2& screenPos, const ViewMatrix& matrix, int screenWidth, int screenHeight) {
	float clipX = matrix.m[0][0] * worldPos.x + matrix.m[0][1] * worldPos.y + matrix.m[0][2] * worldPos.z + matrix.m[0][3];
	float clipY = matrix.m[1][0] * worldPos.x + matrix.m[1][1] * worldPos.y + matrix.m[1][2] * worldPos.z + matrix.m[1][3];
	float clipW = matrix.m[3][0] * worldPos.x + matrix.m[3][1] * worldPos.y + matrix.m[3][2] * worldPos.z + matrix.m[3][3];

	if (clipW < 0.001f)
		return false;

	float invW = 1.0f / clipW;
	float ndcX = clipX * invW;
	float ndcY = clipY * invW;

	screenPos.x = (screenWidth / 2.0f) * (1.0f + ndcX);
	screenPos.y = (screenHeight / 2.0f) * (1.0f - ndcY);

	return true;
}
