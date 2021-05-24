#include "utilityClass.h"

utilityClass::utilityClass(int space)
{ 
	space_count = space;
}

std::string utilityClass::remove_extra_whitespaces(const string &input, string &output)
{
	output.clear();  // unless you want to add at the end of existing sring...
	unique_copy (input.begin(), input.end(), back_insert_iterator<string>(output), [](char a,char b){ return isspace(a) && isspace(b);});  
	return output ;
}

std::string utilityClass::removeStringBetweenDoubleQuotes(const std::string logLine)
{
	int count = 0;
	std::string stringWithoutQuotes = "";


	for (int itr=0;itr<logLine.length();itr++)
    {
    	if (logLine[itr]=='\"')
    	{
    		count+=1;

    		if (count==2)
    			continue;

    	}
    	if (itr<logLine.length() && count >=2 )
    	{
    		stringWithoutQuotes = stringWithoutQuotes + logLine[itr];
    	}

    }
    	
    return stringWithoutQuotes;
}

std::string utilityClass::removeExtraSpaces(std::string logLine,int space_count) 
{
	std::string logAfterRemovingExtraSpace = "";

	char s1 = ' ';
	int itr = 0;
	
	for (itr = 0 ;itr<logLine.length()-1; itr++)
	{
		if(s1==logLine[itr] && s1==logLine[itr+1])
			continue;
		else
			logAfterRemovingExtraSpace = logAfterRemovingExtraSpace + logLine[itr];
	}
	logAfterRemovingExtraSpace = logAfterRemovingExtraSpace + logLine[itr];

	for(itr = 0; itr<space_count-1; itr++)
		logAfterRemovingExtraSpace = " "+ logAfterRemovingExtraSpace;

    
    return logAfterRemovingExtraSpace;
}

std::string utilityClass::removeSpaces(string logLine) 
{
    logLine.erase(remove(logLine.begin(), logLine.end(), ' '), logLine.end());
    return logLine;
}

// slice all the variables around comma, and store them in a vector to use them in the modified log line
std::vector<string> utilityClass::sliceStringAroundComma(std::string line)
{
	std::vector<string> resultVector;

    std::stringstream s_stream(line);

 
    while (s_stream.good()) {
        std::string substr;
        getline(s_stream, substr, ',');
        resultVector.push_back(substr);
    }
    s_stream.str("");
    return resultVector;
}

//extract all the variables from the log line
std::vector<std::string> utilityClass::findPrintableVariable(std::string log_line, std::string logType)
{

	std::string temp = log_line; //store the log line in temperary variable
	std::string output = "";

	temp = removeSpaces(temp);

	temp = removeStringBetweenDoubleQuotes(temp);

	temp = temp.substr(1, temp.length()-3);
	
	vector<string> printableVariable = sliceStringAroundComma(temp); //find out all the values that has been used in the log

	std::vector<string> variable  =  printableVariable;
	

	return printableVariable;
} 

//to find out the type of all the variable such as %s etc
std::vector<std::string> utilityClass::findFormatSpecifier(std::string log_line)
{
	std::vector<std::string> specifiers = {};
	int position = 0;
	for(int itr=0;itr<log_line.length()-1;itr++)
	{
		if(log_line.substr(itr,2)=="%s")
		{
			specifiers.push_back("%s");
			continue;
		}
		if(log_line.substr(itr,2)=="%d")
		{
			specifiers.push_back("%d");
			continue;
		}
		if (log_line.substr(itr,4)=="%016")
		{
			specifiers.push_back("hex");
			continue;
		}
		if (log_line.substr(itr,1)=="%")
		{
			specifiers.push_back("u");
			continue;
		}

	}
	return specifiers;

}

//count space before log   
int utilityClass::countSpaceBeforeLog(const std::string &line)
{
	space_count = 0;
	for(int i =0; i <line.length(); i++)
	{
		if (isspace(line[i]))
		{
			space_count++;
		}
		else
			break;
	}
   return space_count;
}


std::string utilityClass::convertOldLogToNewLog(std::string log_line, std::vector<string> specifier, std::vector<string> variable, int space_count, std::string logType)
{
	std::string newLog = ""; 

	for (int i =0;i<space_count;i++)
	{
		newLog = newLog + " ";
	}
 
    // take the decision based on log type
	if (logType=="ALGO_ELOG")
		newLog = newLog + "TTLOG(ERROR,13)<<\"";
	else if (logType=="ALGO_WLOG")
		newLog = newLog + "TTLOG(WARNING,13)<<\"";
	else if (logType=="ALGO_ILOG")
		newLog = newLog + "TTLOG(INFO,13)<<\"";
	else if (logType=="ALGO_DLOG")
		newLog = newLog + "TTLOG(DEBUG,13)<<\"";
	else if (logType=="SERVER_DLOG")
		newLog = newLog + "TTLOG(DEBUG,13)<<\"";
	else if (logType=="SERVER_ELOG")
		newLog = newLog + "TTLOG(ERROR,13)<<\"";
	else if (logType=="SERVER_ILOG")
		newLog = newLog + "TTLOG(INFO,13)<<\"";
	else if (logType=="SERVER_WLOG")
		newLog = newLog + "TTLOG(WARNING,13)<<\"";
	
	int position = 0; //position of varibale in vector (variables that will be printed in the log line)
	int count = 0;    
	bool flag = 0; 

	int breakpoint = 0; 


	for(int i = (logType.length()+space_count)+1; i < log_line.length(); i++)
	{
		if(log_line[i]=='"')
		{
			count++;
		}

		if (count==2)
		{
			break;
		}
		
		if (log_line[i]=='%')
		{
			if (specifier[position]=="hex") //to handle the hex code

			{
				//std::showbase << std::hex<<session_id

				newLog = newLog + "\"<<" + "std::showbase << std::hex<<" + variable[position] +"<<\"";
				//newLog = newLog + "\"<<" + variable[position] +"<<\"";

			}
			else
			{
				newLog = newLog + "\"<<" + variable[position] +"<<\"";
			}
			position++;
			i = i+1;
			breakpoint = 1;
		}

		else
		{
			breakpoint = 2;

			if(flag==1)
			{
				flag = 0;
				continue;
			}
			if(log_line[i]=='"')
			{
				continue;
			}
			newLog = newLog + log_line[i];
		}
	}

    if (breakpoint==2)
    {    
    	newLog = newLog + "\"<<\"";
    }
	newLog = newLog.substr(0,newLog.length()-3) + "<<endl;";
	
	return newLog;

}

int utilityClass::checkQuestionMarkInLog(std::string logLine)
{
	int found = logLine.find('?');
	return found;
}


std::string utilityClass::removeMultipleDoubleQuotesFromLogsWithQuestionMark(std::string line, int questionMarkPosition)
{
	//it will help us in handling the logs that are containing the multiple double quotes E.g.:
	/* 
	ALGO_ILOG("[algo:%s] Updated OMA Parent ID on request: "
                      "request=%s "
                      "new_oma_parent_id=%s",
                      inst_id().to_string(),
                      ALGOJOB_REQUEST_ID_STR[request->id],
                      std::string(oma_parent_uuid_string)
                     );*/
     
    std::string logWithoutMultipleDoubleQuotes = "";
	int startIndex = -1;
	int endIndex = -1;
	int positionOfLastQuotes = -1;
	int itr = 0;
	int flag = 0;
	while(itr<=questionMarkPosition)
	{
		if(line[itr]=='\"')
		{
			positionOfLastQuotes = itr;
		}
		itr++;
	}


	for (int itr = 0; itr < positionOfLastQuotes; itr++)
	{
		if (line[itr]=='\"' && flag ==0) //flag is to add the first quote
		{
			flag = 1;
			logWithoutMultipleDoubleQuotes = logWithoutMultipleDoubleQuotes + line[itr];
			continue;
		}
		else if (line[itr]=='\"')
			continue;
		logWithoutMultipleDoubleQuotes = logWithoutMultipleDoubleQuotes + line[itr];
	}

	//logWithoutMultipleDoubleQuotes = logWithoutMultipleDoubleQuotes + "\"";

	for (int itr = positionOfLastQuotes;itr<line.length();itr++)
	{
		logWithoutMultipleDoubleQuotes = logWithoutMultipleDoubleQuotes + line[itr];
	}
	return logWithoutMultipleDoubleQuotes;//logWithoutMultipleDoubleQuotes;
}


std::string utilityClass::removeMultipleDoubleQuotesFromLogs(std::string line)
{
	//it will help us in handling the logs that are containing the multiple double quotes E.g.:
	/* 
	ALGO_ILOG("[algo:%s] Updated OMA Parent ID on request: "
                      "request=%s "
                      "new_oma_parent_id=%s",
                      inst_id().to_string(),
                      ALGOJOB_REQUEST_ID_STR[request->id],
                      std::string(oma_parent_uuid_string)
                     );*/
     
    std::string logWithoutMultipleDoubleQuotes = "";
	int startIndex = -1;
	int endIndex = -1;


	const char *pfirst;
	pfirst = strchr(line.c_str(), '\"');
	startIndex = pfirst - line.c_str();

	const char *plast;
	plast = strrchr(line.c_str(), '\"');
	endIndex = plast - line.c_str();


	for (int itr = 0; itr < line.length(); itr++)
	{
		if ((line[itr]=='\"' && (itr==startIndex))|| (line[itr]=='\"' && (itr==endIndex)))
		{
			logWithoutMultipleDoubleQuotes = logWithoutMultipleDoubleQuotes + line[itr];
			continue;
		}
		else if (line[itr]=='\"')
			continue;
		logWithoutMultipleDoubleQuotes = logWithoutMultipleDoubleQuotes + line[itr];
	}

	return logWithoutMultipleDoubleQuotes;//logWithoutMultipleDoubleQuotes;
}


std::vector<string> utilityClass::removeTo_StringFromPrintableVariable(std::vector<string> variableInLog)
{

	std::vector<string> storeModifiedVariable;

    for (int i=0; i<variableInLog.size(); i++)
        {
      		if (variableInLog[i].find("to_string()") != std::string::npos)
      	        { 
      	    		std::string tmp1 = variableInLog[i].substr(0, variableInLog[i].size()-12);
      				storeModifiedVariable.push_back(tmp1);
      			}
      		else
      		{
      			storeModifiedVariable.push_back(variableInLog[i]);
      		}
        }
      return storeModifiedVariable;
}


std::vector<string> utilityClass::removeStdStringFromPrintableVariable(std::vector<string> variableInLog)
{
	  std::vector<string> storeModifiedVariable;

      for (int i=0; i<variableInLog.size(); i++)
      {
      	if (variableInLog[i].length()>=12 && variableInLog[i].substr(0,11)=="std::string")

      	{ 

      	    std::string tmp1 = variableInLog[i].substr(0, variableInLog[i].size()-1);
      	    std::string tmp2 = tmp1.substr(12, tmp1.size());
      		storeModifiedVariable.push_back(tmp2);
      	}
      	else
      	{
      		storeModifiedVariable.push_back(variableInLog[i]);
      	}
      }
      return storeModifiedVariable;
}

//check if the given line is log line or not and what is the type of the log	
std::string utilityClass::checkLog(std::string line)
{   
	std::string log_type;
	for(int i =0; i <line.length(); i++)
	{
		if (isspace(line[i]))
		{
			continue;
		}
		else if (line.substr(i,9)=="ALGO_ILOG")
		{
			log_type = "ALGO_ILOG";
			break;
		}
		else if (line.substr(i, 9)=="ALGO_ELOG")
		{
			log_type = "ALGO_ELOG";
			break;
		}
		else if (line.substr(i,9)=="ALGO_DLOG")
		{
			log_type = "ALGO_DLOG";
			break;
		}
		else if (line.substr(i,9)=="ALGO_WLOG")
		{
			log_type = "ALGO_WLOG";
			break;
		}
		else if (line.substr(i,11)=="SERVER_ILOG")
		{
			log_type = "SERVER_ILOG";
			break;
		}
		else if (line.substr(i,11)=="SERVER_WLOG")
		{
			log_type = "SERVER_WLOG";
			break;
		}
		else if (line.substr(i,11)=="SERVER_DLOG")
		{
			log_type = "SERVER_DLOG";
			break;
		}
		else if (line.substr(i,11)=="SERVER_ELOG")
		{
			log_type = "SERVER_ELOG";
			break;
		}
		else
		{
			break;
		}

	}
	return log_type;
}