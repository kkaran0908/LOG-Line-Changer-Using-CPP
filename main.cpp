//driver code to convert old logs to new logs
#include "utilityClass.h"
int main()
    {
    int count = 1;
    ofstream fileToStoreStreamBasedLogs("fileWithStreamBasedLogs.cpp"); //file to store stream based logs accross all the files
    ofstream fileToStoreOldLogs("fileWithOlgLogs.cpp"); //file to store old format logs accross all the files

    std::string path = "/Users/karankumar/Desktop/algo"; //path from where we recursively read the files for processing

    for (auto& dirEntry: std::filesystem::recursive_directory_iterator(path)) 
        {
            if (!dirEntry.is_regular_file()) 
            {
                continue;
            }

            std::filesystem::path file = dirEntry.path();
            std::string fileExtension = file.extension();
            std::string filename = file.filename();
    
            if (fileExtension!=".cpp")
            {
        	continue;
		    }
		  
            utilityClass uti = utilityClass(0);

            std::ifstream data(file); //file that you want to modify with new log design

		    std::string line;
		    std::vector<std::string> specifier;
		    std::vector<std::string> variableInLog;
		    int flag = 0;

		    const char *plast;
		    plast = strrchr(std::string(file).c_str(), '/');

		    int endIndex = plast - std::string(file).c_str();

		    std::string subPath = std::string(file).substr(0,endIndex+1);

		    std:string fileName = subPath + "temperoryFile.cpp" ;//temperorily save the content of the file

		    ofstream MyFile(fileName); //file to save the modified content

		    cout<<"File Number:" << count << "Processed File Path: "<<file<<endl;

		    while(getline(data,line))    //read the file line by line
		    {

                int spaceCount  = 0;

			    string logType = uti.checkLog(line);  //check if some particular line is log line or not and find the type of log

                //cout<<"Log Type: "<<logType<<endl;
                //continue;

    			if (!logType.empty())   //if some particular line is log line and log is distributed in more than one line, combined it and stroed it in a variable called combinedLogLine
	       		{
                
			     	std::string combinedLogLine= "";

            	    while(line.back()!=';')
            	   {
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
                    spaceCount = uti.countSpaceBeforeLog(line);   //count the space before the log line so that after modification we can make consistency

                    std::string temperoryLine = uti.removeExtraSpaces(line, spaceCount); //remove space to store the log line in universal file i.e. fileWithOlgLogs.cpp

                    fileToStoreOldLogs<<std::string(filename)+temperoryLine<<"\n";       
			    }

                if(logType=="m_execIf->LogToAlgoJob") //convert such logs to normal log format
                {
                    line = uti.processTheLogLineWithLog_m_execIfLogToAlgoJob(line);
                    logType = uti.checkLog(line);  //logType contains what kind of log line it is i.e. warning, error etc.
                }

            

			   if (!logType.empty() && flag!=1) //convert the log to new form (ALGO_ILOG -> TTLOG())
			   {  
				    int firstQuestionMarkPosition = uti.checkQuestionMarkInLog(line);  //check if ? is there in the log to handle the situation where mutiple double quotes are there because of ?
                
                    bool isDobuleQuotes = uti.checkDoubleQuotesInLog(line); //handle the case where, no dobule quotes are there in the log
                 
                    if (!isDobuleQuotes) //log is not having any double quote
                    {

                        variableInLog = uti.findPrintableVariableinWithoutQuotesLog(line,logType); //find all the variables from the logs which are not having any double quotes

                        variableInLog = uti.removeStdStringFromPrintableVariable(variableInLog);  //remove std::string from std::string(variable);

                        variableInLog = uti.removeTo_StringFromPrintableVariable(variableInLog); //remove .to_string from variable.to_string();

                        line = uti.convertOldLogToNewLogWithoutDoubleQuotes(line, variableInLog, spaceCount, logType);//  convert the old log line into the new log format

                    }
                    else //handle the case where double quotes are there in the logs 
                    {
                        if (firstQuestionMarkPosition < 0) //if '?' position is negative, it means double quotes due to '?' are not there in the log
                        {
                            line = uti.removeMultipleDoubleQuotesFromLogs(line);
                        }

                        if (firstQuestionMarkPosition > 0) //if '?' position is greater then one, it means double quotes due to '?' are there and we can not remove them as they are part of variable section
                        {
                            line = uti.removeMultipleDoubleQuotesFromLogsWithQuestionMark(line, firstQuestionMarkPosition);
                        }

                        specifier = uti.findFormatSpecifier(line);  //find the format specifiers in the log
                    
                        variableInLog = uti.findPrintableVariable(line,logType); // find all the variables from the log

                        

                        variableInLog = uti.removeStdStringFromPrintableVariable(variableInLog);  //remove std::string from std::string(variable);

                        variableInLog = uti.removeTo_StringFromPrintableVariable(variableInLog);  //remove .to_string from variable.to_string();


                        line = uti.convertOldLogToNewLog(line, specifier ,variableInLog, spaceCount, logType);//  convert the old log line into the new log format

                    }

        		line = uti.removeExtraSpaces(line,spaceCount); //remove extra spaces from the modified log

                cout.flush();

                fileToStoreStreamBasedLogs<< std::string(filename) + line + "\n"; //save the modified logs to the universal file that will contain stream based logs of all the files
			}  	
			flag = 0;

            line = uti.distributeLongerLogToMultipleLines(line,spaceCount);		
			MyFile<<line + "\n"; //write to the log line to the temperory file
        }
		
		MyFile.close();

        //rename the temperory file to its original name
		std::filesystem::rename(fileName, std::string(file));
		count+=1;
	}
    //close the universal file 
    fileToStoreOldLogs.close();
    fileToStoreStreamBasedLogs.close();
	
    return 0;
}
