#pragma once
#include <string>

class logging
{
    private:
        //private constructor means no instances of this object can be created
        logging() {}
		static std::string format(std::string toLog, std::string level, std::string color, std::string source);
    public:
        static void info(std::string toLog, std::string source = "?");
		static void important(std::string toLog, std::string source = "?");
		static void warning(std::string toLog, std::string source = "?");
        static void error(std::string toLog, std::string source = "?");
		static void fatal(std::string toLog, std::string source = "?");
};