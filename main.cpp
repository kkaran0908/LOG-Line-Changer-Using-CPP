//driver code to convert old logs to new logs
#include "utilityClass.h"
int main()
	{

int count = 1;

std::string path = "/Users/karankumar/Desktop/cppFiles/algo";

for (auto& dirEntry: std::filesystem::recursive_directory_iterator(path)) 
{
    if (!dirEntry.is_regular_file()) 
    {
            continue;
    }
        std::filesystem::path file = dirEntry.path();
       // std::cout << "Filename: " << file.filename() << " extension: " << file.extension()<<"File Path: "<< file << std::endl;

        //std::string filePath = dirEntry.path();
        std::string fileExtension = file.extension();
        std::string filename = file.filename();
    
        if (fileExtension!=".cpp")
        {
        	//cout<<"This is not the cpp extension"<<endl;
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
            if (filename=="algoinstmgr.cpp")
            {
                cout<<line<<endl;
                cout<<endl;
            }

            int spaceCount  = 0;

			string logType = uti.checkLog(line);  //check if some particular line is log line or not and find the type of log

            if (filename=="algoinstmgr.cpp")
            {
                cout<<"After Check Log"<<endl;
            }

			if (!logType.empty())   //if some particular line is log line and log is distributed in more than one line, combined it and stroed it in a variable called combinedLogLine
			{
                int spaceCount = uti.countSpaceBeforeLog(line);   //count the space before the log line so that after modification we can make consistency
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

            if (filename=="algoinstmgr.cpp")
            {
                cout<<"After Combining the Log"<<endl;
            }


			if (!logType.empty() && flag!=1) //convert the log to new form (ALGO_ILOG - TTLOG())
			{  
				int firstQuestionMarkPosition = uti.checkQuestionMarkInLog(line);
                
                
                bool isDobuleQuotes = uti.checkDoubleQuotesInLog(line); //handle the case when, no dobule quotes are there in the log
                 
                if (!isDobuleQuotes) //log is not having any double quote
                {

                    variableInLog = uti.findPrintableVariableinWithoutQuotesLog(line,logType);

                    variableInLog = uti.removeStdStringFromPrintableVariable(variableInLog);

                    variableInLog = uti.removeTo_StringFromPrintableVariable(variableInLog);

                    line = uti.convertOldLogToNewLogWithoutDoubleQuotes(line, variableInLog, spaceCount, logType);//  convert the old log line into the new log format

                }
                
                else
                {
                    if (filename=="algoinstmgr.cpp")
                    {
                        cout<<"Inside else "<<endl;
                    }
                        if (firstQuestionMarkPosition < 0)
                        {
                          line = uti.removeMultipleDoubleQuotesFromLogs(line);
                        }

                        if (firstQuestionMarkPosition > 0)
                        {
                          line = uti.removeMultipleDoubleQuotesFromLogsWithQuestionMark(line, firstQuestionMarkPosition);
                        }

                        specifier = uti.findFormatSpecifier(line);

                        if (filename=="algoinstmgr.cpp")
                    {
                        cout<<"After finding specifier "<<endl;
                    }

                    
                        variableInLog = uti.findPrintableVariable(line,logType);

                        variableInLog = uti.removeStdStringFromPrintableVariable(variableInLog);

                        variableInLog = uti.removeTo_StringFromPrintableVariable(variableInLog);

                        if (filename=="algoinstmgr.cpp")
                    {
                        cout<<"After finding variable in log "<<endl;

                        for(int itr=0; itr < variableInLog.size();itr++)
                        {
                            cout<<"Variable in log: "<<variableInLog[itr]<<endl;
                        }
                    }

                        line = uti.convertOldLogToNewLog(line, specifier ,variableInLog, spaceCount, logType);//  convert the old log line into the new log format

                        if (filename=="algoinstmgr.cpp")
                    {
                        cout<<"After converting to the new log "<<endl;
                    }
                }

        		line = uti.removeExtraSpaces(line,spaceCount); 
                cout.flush();
			}  	
			flag = 0;		
			MyFile<<line + "\n"; //write to the file

		}
		
		MyFile.close();

        //rename the processed file to its original name
		std::filesystem::rename(fileName, std::string(file));
		//std::filesystem::remove(std::string(fileName) );

		count+=1;

	}

		  return 0;
	}
