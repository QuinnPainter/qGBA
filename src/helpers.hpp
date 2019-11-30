#pragma once
#include <string>
#include <sstream>

class helpers
{
	private:
		helpers() {}
	public:
		template<typename T>
		static std::string intToHex(T i)
		{
			std::ostringstream stream;
			stream.width(sizeof(T) * 2);
			stream.fill('0');
			stream << std::uppercase << std::hex << (int)i;
			return stream.str();
		}
};