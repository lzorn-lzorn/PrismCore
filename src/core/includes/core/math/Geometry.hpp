#pragma once

#include <cstdint>
#include "Vector.hpp"
namespace core
{

struct Rectangle
{
	int32_t X;
	int32_t Y;
	int32_t Width;
	int32_t Height;

	[[nodiscard]] bool isContains(const core::Vec2i& Point) const
	{
		return Point.x() >= X 
			&& Point.x() < X + Width 
			&& Point.y() >= Y 
			&& Point.y() < Y + Height;
	}

	[[nodiscard]] bool isContains(int32_t PointX, int32_t PointY) const
	{
		return PointX >= X 
			&& PointX < X + Width 
			&& PointY >= Y 
			&& PointY < Y + Height;
	}
};

}