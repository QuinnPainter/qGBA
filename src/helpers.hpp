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
		template<typename T>
		static void swap(T* var1, T* var2)
		{
			T temp = *var1;
			*var1 = *var2;
			*var2 = temp;
		}
		//Sign extends a number. X is the number and bits is the number of bits the number is now.
		//Example : Sign extend a 24 bit number to 32 bit: x is a uint32_t, and bits is 24
		//https://stackoverflow.com/questions/42534749/signed-extension-from-24-bit-to-32-bit-in-c
		template<class T>
		static T signExtend(T x, const int bits) {
			T m = 1 << (bits - 1);
			return (x ^ m) - m;
		}
};