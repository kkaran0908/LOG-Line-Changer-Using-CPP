//driver code to convert old logs to new logs
#include "utilityClass.h"
int main()
	{
		utilityClass uti = utilityClass(0);

		std::ifstream data("test_file.cpp"); //file that you want to modify with new log design

		std::string line;
		std::vector<std::string> specifier;
		std::vector<std::string> variableInLog;
		int flag = 0;
		ofstream MyFile("result_file.cpp"); //file to save the modified content

		while(getline(data,line))    //read the file line by line
		{

			string logType = uti.checkLog(line);  //check if some particular line is log line or not and find the type of log

			int spaceCount = uti.countSpaceBeforeLog(line);   //count the space before the log line so that after modification we can make consistency

			if (!logType.empty())   //if some particular line is log line and log is distributed in more than one line, combined it and stroed it in a variable called combinedLogLine
			{
				std::string combinedLogLine= "";

            	while(line.back()!=';')
            	{
            		//cout<<line<<endl;
                	combinedLogLine = combinedLogLine + line;
                	getline(data,line);

                	if (line.back()=='/')
                	{
                		flag = 1;
                		break;
                    }

            	}

            	combinedLogLine = combinedLogLine + line;
            	line = combinedLogLine;   //line contains the log distributed in multiple line
            	
			}


			if (!logType.empty() && flag!=1) //convert the log to new form (ALGO_ILOG - TTLOG())
			{  
				int firstQuestionMarkPosition = uti.checkQuestionMarkInLog(line);

				if (firstQuestionMarkPosition < 0)
				{
				  line = uti.removeMultipleDoubleQuotesFromLogs(line);
                }

                if (firstQuestionMarkPosition > 0)
				{
				  line = uti.removeMultipleDoubleQuotesFromLogsWithQuestionMark(line, firstQuestionMarkPosition);
                }

				specifier = uti.findFormatSpecifier(line);

				for (int i=0;i<specifier.size();i++)
				{
					cout<<specifier[i]<<endl;
				} 
			
        		variableInLog = uti.findPrintableVariable(line,logType);

        		variableInLog = uti.removeStdStringFromPrintableVariable(variableInLog);

        		variableInLog = uti.removeTo_StringFromPrintableVariable(variableInLog);

        		line = uti.convertOldLogToNewLog(line, specifier ,variableInLog, spaceCount, logType);//  convert the old log line into the new log format

        		line = uti.removeExtraSpaces(line,spaceCount); 

        		cout.flush();    
			}  	
			flag = 0;		
			MyFile<<line + "\n"; //write to the file

		}
		
		MyFile.close();

		  return 0;
	}