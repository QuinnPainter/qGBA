#include "logging.hpp"
#include <sstream>
#include <iostream>

#define RESET   "\033[0m"
#define BLACK   "\033[30m"      /* Black */
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */
#define BLUE    "\033[34m"      /* Blue */
#define MAGENTA "\033[35m"      /* Magenta */
#define CYAN    "\033[36m"      /* Cyan */
#define WHITE   "\033[37m"      /* White */
#define BOLDBLACK   "\033[1m\033[30m"      /* Bold Black */
#define BOLDRED     "\033[1m\033[31m"      /* Bold Red */
#define BOLDGREEN   "\033[1m\033[32m"      /* Bold Green */
#define BOLDYELLOW  "\033[1m\033[33m"      /* Bold Yellow */
#define BOLDBLUE    "\033[1m\033[34m"      /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m"      /* Bold Magenta */
#define BOLDCYAN    "\033[1m\033[36m"      /* Bold Cyan */
#define BOLDWHITE   "\033[1m\033[37m"      /* Bold White */

//maybe this should be a macro?
std::string logging::format(std::string toLog, std::string level, std::string source)
{
	return "[" + source + "] " + "[" + level + "] " + toLog;
}

void logging::info(std::string toLog, std::string source)
{
	std::cout << WHITE << format(toLog, "info", source) << RESET << std::endl;
}

//same as info but with different colour text so it's easy to spot
void logging::important(std::string toLog, std::string source)
{
	std::cout << GREEN << format(toLog, "important", source) << RESET << std::endl;
}

void logging::warning(std::string toLog, std::string source)
{
	std::cout << YELLOW << format(toLog, "warning", source) << RESET << std::endl;
}

void logging::error(std::string toLog, std::string source)
{
	std::cerr << RED << format(toLog, "error", source) << RESET << std::endl;
}

//for errors so bad that we need to immediately exit
void logging::fatal(std::string toLog, std::string source)
{
	std::cerr << RED << format(toLog, "fatal", source) << RESET << std::endl;
	exit(1);
}