#include<string>
#include<vector>
#include<cstring>
#include<algorithm>
#include<fstream>
#include<string>
#include<iostream>
#include<sstream>
#include <regex>
#include<typeinfo>
using namespace std;

class utilityClass
{
public:
    int space_count;
    utilityClass(int space);
    std::string checkLog(std::string line); //check if a line is log line or not and return the log type
    int countSpaceBeforeLog(std::string);
    std::string fetchTheEntireLogLine(std::string line);
    std::string convertOldLogToNewLog(std::string, std::vector<string>, std::vector<string>, int , std::string);
    std::vector<std::string> findFormatSpecifier(std::string);
    std::vector<std::string> findPrintableVariable(std::string, std::string);
    std::string remove_extra_whitespaces(const string &input, string &output);
    std::string removeStringBetweenDoubleQuotes(std::string);
    std::string removeSpaces(std::string);
    std::string removeExtraSpaces(std::string,int);
    std::vector<string> sliceStringAroundComma(std::string);
    std::vector<string> removeStdStringFromPrintableVariable(std::vector<string>);
    std::vector<string> removeTo_StringFromPrintableVariable(std::vector<string>);
    std::string removeMultipleDoubleQuotesFromLogs(std::string);
    int checkQuestionMarkInLog(std::string);
    std::string removeMultipleDoubleQuotesFromLogsWithQuestionMark(std::string, int);
    bool checkDoubleQuotesInLog(std::string);
    std::vector<string> findPrintableVariableinWithoutQuotesLog(std::string,std::string);
    std::string convertOldLogToNewLogWithoutDoubleQuotes(std::string, std::vector<std::string>, int, std::string);
    std::string processTheLogLineWithLog_m_execIfLogToAlgoJob(std::string);
    std::string distributeLongerLogToMultipleLines(std::string,int);
};
