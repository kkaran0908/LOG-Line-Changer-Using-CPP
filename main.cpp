//driver code to convert old logs to new logs

#include "utilityClass.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include<typeinfo>
using namespace std;
int main()
	{
		utilityClass uti = utilityClass(0);
		std::ifstream data("original_file.cpp");
		std::string line;
		std::vector<std::string> specifier;
		std::vector<std::string> variable;
		ofstream MyFile("temp.cpp");

		while(getline(data,line))
		{

			string logType = uti.checkLog(line);
			int spaceCount = uti.countSpaceBeforeLog(line);
			if (!logType.empty())
			{
				std::string combinedLogLine= "";
            	while(line.back()!=';')
            	{
                	combinedLogLine = combinedLogLine + line;
                	getline(data,line);
                	
            	}
            	combinedLogLine = combinedLogLine + line;
            	line = combinedLogLine;
			}


			if (!logType.empty())
			{  
				specifier = uti.findFormatSpecifier(line); 
			
        		variable = uti.findPrintableVariable(line,logType);
        		line = uti.convertOldLogToNewLog(line, specifier ,variable, spaceCount, logType);//  convert the old log line into the new log format

        		//line = uti.removeSpaces(line); 

        		//cout<<"Here"<<endl;
        		cout.flush();    
			}  

			
			MyFile<<line + "\n";


		}
		
		MyFile.close();

		  return 0;
	}