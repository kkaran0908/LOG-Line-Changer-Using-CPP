#include <fstream>
#include<string>
#include <vector>
#include<cstring>
#include <algorithm>
#include <fstream>
#include<string>
#include<iostream>
#include<sstream>
#include <regex>

using namespace std;
class utilityClass
{

public:
	int space_count;
	utilityClass(int space);
	std::string checkLog(std::string line);
	//std::string findPrintableVariable( std::string,std::string );
	int countSpaceBeforeLog(const std::string &line);
	std::string fetchTheEntireLogLine(std::string line);
	std::string convertOldLogToNewLog(std::string, std::vector<string>, std::vector<string>, int , std::string);
	std::vector<std::string> findFormatSpecifier(std::string);
	std::vector<std::string> findPrintableVariable(std::string, std::string);
	std::string remove_extra_whitespaces(const string &input, string &output);
	std::string removeStringBetweenDoubleQuotes(std::string);
	std::string removeSpaces(std::string);
	std::vector<string> sliceStringAroundComma(std::string);
};