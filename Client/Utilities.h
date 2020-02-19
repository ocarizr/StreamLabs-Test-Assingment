#pragma once
#include <iostream>
#include <functional>

class Utilities
{
public:
	[[nodiscard]] static bool DoTryCatch(std::function<void(bool&)> func)
	{
		bool result = false;

		try
		{
			func(result);
		}
		catch (std::exception & e)
		{
			result = false;
		}
		catch (...)
		{
			result = false;
		}

		return result;
	}
};