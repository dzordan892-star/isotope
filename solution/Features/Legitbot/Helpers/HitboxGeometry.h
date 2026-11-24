#pragma once

#include "../../../Utils/Math/Math.h"
#include "../../../SDK/Valve/Datatypes/Hitbox.h"
#include "../../../SDK/Valve/Datatypes/Taia.h"
#include "SpreadCache.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <span>
#include <vector>

struct HitboxGeometry_t {
	static float Dot(const Vector& first, const Vector& second) { return first.x * second.x + first.y * second.y + first.z * second.z; }

	struct RayData_t {
		Vector axis;
		Vector relative_center;
		Vector box_origin;
		bool box_prepared = false;
		double axis_length_squared;
		double inverse_axis_length_squared;
		double cylinder_constant;
		double center_distance_squared;
		double bound_radius = 0.;
		double axis_origin;
		double origin_length_squared;
		double radius_squared;
		double offsets[2][3];
		double cap_constants[2];

		void Prepare(const Vector& origin, const Vector& first, const Vector& last, float radius) {
			axis = last - first;
			relative_center = (first + last) * 0.5f - origin;
			center_distance_squared = static_cast<double>(relative_center.x) * relative_center.x + static_cast<double>(relative_center.y) * relative_center.y + static_cast<double>(relative_center.z) * relative_center.z;
			box_prepared = false;
			axis_length_squared = static_cast<double>(axis.x) * axis.x + static_cast<double>(axis.y) * axis.y + static_cast<double>(axis.z) * axis.z;
			inverse_axis_length_squared = axis_length_squared > 1e-8 ? 1. / axis_length_squared : 0.;
			radius_squared = static_cast<double>(radius) * radius;
			for (int end = 0; end < 2; ++end) {
				const Vector& cap = end ? last : first;
				for (int index = 0; index < 3; ++index)
					offsets[end][index] = static_cast<double>(origin[index]) - cap[index];

				cap_constants[end] = offsets[end][0] * offsets[end][0] + offsets[end][1] * offsets[end][1] + offsets[end][2] * offsets[end][2] - radius_squared;
			}

			axis_origin = axis.x * offsets[0][0] + axis.y * offsets[0][1] + axis.z * offsets[0][2];
			origin_length_squared = offsets[0][0] * offsets[0][0] + offsets[0][1] * offsets[0][1] + offsets[0][2] * offsets[0][2];
			cylinder_constant = origin_length_squared - axis_origin * axis_origin * inverse_axis_length_squared - radius_squared;
		}
	};

	struct Ray_t {
		double precise_length_squared;
		explicit Ray_t(const Vector& delta) : precise_length_squared(static_cast<double>(delta.x) * delta.x + static_cast<double>(delta.y) * delta.y + static_cast<double>(delta.z) * delta.z) {}
	};

	static bool IntersectCapsule(const Vector& ray_origin, const Vector& ray_direction, const Vector& capsule_a, const Vector& capsule_b, float radius, float& fraction, const RayData_t* prepared = nullptr, const Ray_t* ray = nullptr) {
		RayData_t local;
		if (!prepared) {
			local.Prepare(ray_origin, capsule_a, capsule_b, radius);
			prepared = &local;
		}

		const Vector& axis = prepared->axis;
		const double axis_length_squared = prepared->axis_length_squared;
		const double direction_length_squared = ray ? ray->precise_length_squared : static_cast<double>(ray_direction.x) * ray_direction.x + static_cast<double>(ray_direction.y) * ray_direction.y + static_cast<double>(ray_direction.z) * ray_direction.z;
		if (direction_length_squared < 1e-8)
			return false;

		const double axis_direction = static_cast<double>(axis.x) * ray_direction.x + static_cast<double>(axis.y) * ray_direction.y + static_cast<double>(axis.z) * ray_direction.z;
		const double axis_origin = prepared->axis_origin;
		const double origin_direction = prepared->offsets[0][0] * ray_direction.x + prepared->offsets[0][1] * ray_direction.y + prepared->offsets[0][2] * ray_direction.z;
		double best_fraction = 1.;
		bool hit = false;

		if (axis_length_squared > 1e-8) {
			const double quadratic = direction_length_squared - axis_direction * axis_direction * prepared->inverse_axis_length_squared;
			const double half_linear = origin_direction - axis_direction * axis_origin * prepared->inverse_axis_length_squared;
			const double constant = prepared->cylinder_constant;
			const double discriminant = half_linear * half_linear - quadratic * constant;
			if (quadratic > 1e-8 && discriminant >= 0.) {
				const double root = std::sqrt(discriminant);
				for (double sign : {-1., 1.}) {
					const double current = (-half_linear + sign * root) / quadratic;
					const double position = axis_origin + axis_direction * current;
					if (current >= 0. && current < best_fraction && position >= 0. && position <= axis_length_squared) {
						if (sign < 0.) {
							fraction = static_cast<float>(current);
							return true;
						}
						best_fraction = current;
						hit = true;
						break;
					}
				}
			}
		}

		for (int end = 0; end < (axis_length_squared == 0. ? 1 : 2); ++end) {
			const double cap_x = prepared->offsets[end][0];
			const double cap_y = prepared->offsets[end][1];
			const double cap_z = prepared->offsets[end][2];
			const double half_linear = cap_x * ray_direction.x + cap_y * ray_direction.y + cap_z * ray_direction.z;
			const double constant = prepared->cap_constants[end];
			const double discriminant = half_linear * half_linear - direction_length_squared * constant;
			if (discriminant < 0.)
				continue;

			const double root = std::sqrt(discriminant);
			for (double sign : {-1., 1.}) {
				const double current = (-half_linear + sign * root) / direction_length_squared;
				if (current < 0. || current >= best_fraction)
					continue;

				const double position = axis_origin + axis_direction * current;
				if (axis_length_squared > 1e-8 && (end ? position < axis_length_squared : position > 0.))
					continue;

				best_fraction = current;
				hit = true;
				break;
			}
		}

		if (hit)
			fraction = static_cast<float>(best_fraction);

		return hit;
	}

	static bool IntersectBox(const Vector& origin, const Vector& delta, const Vector& mins, const Vector& maxs, float& fraction) {
		float entry = 0.f;
		float exit = 1.f;

		for (int axis = 0; axis < 3; ++axis) {
			if (std::fabs(delta[axis]) < 1e-8f) {
				if (origin[axis] < mins[axis] || origin[axis] > maxs[axis])
					return false;

				continue;
			}

			float entry_fraction = (mins[axis] - origin[axis]) / delta[axis];
			float exit_fraction = (maxs[axis] - origin[axis]) / delta[axis];

			if (entry_fraction > exit_fraction)
				std::swap(entry_fraction, exit_fraction);

			entry = std::max(entry, entry_fraction);
			exit = std::min(exit, exit_fraction);

			if (entry > exit)
				return false;
		}

		fraction = entry;
		return true;
	}

	Vector first, last, center, mins, maxs, position;
	Matrix3x4_t inverse;
	float radius = 0.f;
	float bound_radius_squared = 0.f;
	float scale_squared = 1.f;
	int group = -1;
	bool valid = false;

	void Prepare(const Vector& lower, const Vector& upper, float shape_radius, const Matrix3x4_t& matrix, float scale, int hitgroup, bool geometry_valid) {
		valid = geometry_valid && scale >= 1e-6f;
		if (!valid)
			return;

		mins = lower;
		maxs = upper;
		first = Math::TransformPoint(mins, matrix);
		last = Math::TransformPoint(maxs, matrix);
		center = (first + last) * 0.5f;
		radius = shape_radius > 0.001f ? shape_radius * scale : 0.f;
		group = hitgroup;
		position = {matrix[0][3], matrix[1][3], matrix[2][3]};
		scale_squared = scale * scale;
		float inverse_scale = 1.f / scale_squared;
		for (int row = 0; row < 3; ++row)
			for (int column = 0; column < 3; ++column)
				inverse[row][column] = matrix[column][row] * inverse_scale;

		float bound = (last - first).Length() * 0.5f + radius;
		bound += 0.01f + 32.f * FLT_EPSILON * (std::fabs(center.x) + std::fabs(center.y) + std::fabs(center.z) + bound + 1.f);
		bound_radius_squared = bound * bound;
	}

	void PrepareRay(const Vector& origin, RayData_t& ray, double maximum_spread = 0.) const {
		ray.Prepare(origin, first, last, radius);
		ray.bound_radius = std::sqrt(static_cast<double>(bound_radius_squared)) + 64. * FLT_EPSILON * (std::sqrt(ray.center_distance_squared) + 8192. * (1. + maximum_spread) + 1.);
		if (radius <= 0.f) {
			ray.box_origin = Math::TransformPoint(origin - position, inverse);
			ray.box_prepared = true;
		}
	}

	bool Intersect(const Vector& origin, const Vector& delta, float& fraction, const RayData_t* prepared = nullptr, const Ray_t* ray = nullptr) const {
		if (!valid)
			return false;

		if (prepared && ray) {
			const auto& relative = prepared->relative_center;
			double length_squared = ray->precise_length_squared;
			if (length_squared < 1e-8)
				return false;
			double projection = static_cast<double>(relative.x) * delta.x + static_cast<double>(relative.y) * delta.y + static_cast<double>(relative.z) * delta.z;
			double distance_squared = prepared->center_distance_squared;
			if (projection <= 0.) {
				if (distance_squared > bound_radius_squared)
					return false;
			} else if (projection >= length_squared) {
				if (distance_squared - 2. * projection + length_squared > bound_radius_squared)
					return false;
			} else if (distance_squared * length_squared - projection * projection > static_cast<double>(bound_radius_squared) * length_squared) {
				return false;
			}
		} else {
			Vector relative = center - origin;
			float length_squared = Dot(delta, delta);
			if (length_squared < 1e-8f)
				return false;
			float projection = std::clamp(Dot(relative, delta) / length_squared, 0.f, 1.f);
			if ((relative - delta * projection).LengthSqr() > bound_radius_squared)
				return false;
		}

		if (radius > 0.f)
			return IntersectCapsule(origin, delta, first, last, radius, fraction, prepared, ray);

		return IntersectBox(prepared && prepared->box_prepared ? prepared->box_origin : Math::TransformPoint(origin - position, inverse), Math::TransformPoint(delta, inverse), mins, maxs, fraction);
	}

	static bool CanReachHitchance(std::span<const HitboxGeometry_t> hitboxes, std::span<const RayData_t> rays,
		const std::array<double, CSpreadCache::sample_count>& sorted_spread, const Vector& forward, const Vector& right, const Vector& up, int desired, float required) {
		auto dot = [](const Vector& first, const Vector& second) {
			return static_cast<double>(first.x) * second.x + static_cast<double>(first.y) * second.y + static_cast<double>(first.z) * second.z;
		};
		double forward_length = std::sqrt(dot(forward, forward));
		double right_depth = dot(right, forward) / forward_length;
		double up_depth = dot(up, forward) / forward_length;
		double cross = std::fabs(dot(right, up) - right_depth * up_depth);
		double transverse = std::sqrt(std::max(0., std::min(dot(right, right) - right_depth * right_depth, dot(up, up) - up_depth * up_depth) - cross));
		double leakage = std::sqrt(right_depth * right_depth + up_depth * up_depth);
		double maximum_spread = std::sqrt(sorted_spread.back());
		if (!(forward_length >= 0.5 && transverse >= 0.5 && maximum_spread <= 1. && forward_length - leakage * maximum_spread >= 0.5))
			return true;
		double maximum_slope = 0.;
		bool found = false;
		for (size_t i = 0; i < hitboxes.size(); ++i) {
			if (!hitboxes[i].valid || hitboxes[i].group != desired)
				continue;
			const auto& ray = rays[i];
			double depth = dot(ray.relative_center, forward) / forward_length;
			double lateral = std::sqrt(std::max(0., ray.center_distance_squared - depth * depth));
			double radius = ray.bound_radius;
			if (!(depth > radius) || !std::isfinite(ray.center_distance_squared))
				return true;
			double slope = (lateral * depth + radius * std::sqrt(std::max(0., ray.center_distance_squared - radius * radius))) / ((depth - radius) * (depth + radius));
			maximum_slope = std::max(maximum_slope, slope);
			found = true;
		}
		if (!found)
			return false;
		double denominator = transverse - maximum_slope * leakage;
		if (!(denominator > 0.))
			return true;
		double radius = maximum_slope * forward_length / denominator;
		auto end = std::upper_bound(sorted_spread.begin(), sorted_spread.end(), radius * radius);
		int possible = static_cast<int>(end - sorted_spread.begin());
		float upper = static_cast<float>(possible) / sorted_spread.size();
		return !(upper < required);
	}

	Vector Contour(const Vector& eye, const Vector& view, const Vector& side, float scale = 1.f) const {
		Vector best = center;
		float best_slope = -FLT_MAX;
		float contour_radius = radius * scale;
		int count = 0;
		for (int index = 0; index < (radius > 0.f ? 2 : 8); ++index) {
			Vector point;
			if (radius > 0.f) {
				const Vector& cap = index ? last : first;
				Vector relative = cap - eye;
				float depth = Dot(relative, view);
				float lateral = Dot(relative, side);
				float distance_squared = depth * depth + lateral * lateral;
				point = cap + side * contour_radius;
				if (depth > contour_radius && distance_squared > contour_radius * contour_radius) {
					float tangent = std::sqrt(distance_squared - contour_radius * contour_radius);
					float side_weight = (depth * tangent - lateral * contour_radius) / distance_squared;
					float view_weight = -(lateral * tangent + depth * contour_radius) / distance_squared;
					point = cap + (side * side_weight + view * view_weight) * contour_radius;
				}
			} else {
				Vector local((index & 1) ? maxs.x : mins.x, (index & 2) ? maxs.y : mins.y, (index & 4) ? maxs.z : mins.z);
				point = position + Vector(local.x * inverse[0][0] + local.y * inverse[1][0] + local.z * inverse[2][0], local.x * inverse[0][1] + local.y * inverse[1][1] + local.z * inverse[2][1], local.x * inverse[0][2] + local.y * inverse[1][2] + local.z * inverse[2][2]) * scale_squared;
			}

			Vector relative = point - eye;
			float slope = Dot(relative, side) / std::max(Dot(relative, view), 0.0001f);
			if (slope > best_slope + 0.000001f) {
				best_slope = slope;
				best = point;
				count = 1;
			} else if (std::fabs(slope - best_slope) <= 0.000001f) {
				best += point;
				++count;
			}
		}
		return best / static_cast<float>(count);
	}
};

struct C_CSPlayerPawn;
struct CCSGOInput;
class CUserCommand;

enum AutoStopFlags {
	STOP_EARLY = 1 << 0,
	STOP_BETWEEN_SHOTS = 1 << 1,
	STOP_IN_AIR = 1 << 2,
	STOP_ON_LAND = 1 << 3
};
