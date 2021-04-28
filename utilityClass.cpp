#include "utilityClass.h"
using namespace std;

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
    		count++;
    		continue;
    	}
    	else if (count==0 && logLine[itr]==' ')
    	{
    		continue;
    	}
    	else if(count ==0 || count==2)
    	{
    		stringWithoutQuotes = stringWithoutQuotes + logLine[itr];
    	}

    }
    return stringWithoutQuotes;
}

std::string utilityClass::removeSpaces(string logLine) 
{
    logLine.erase(remove(logLine.begin(), logLine.end(), ' '), logLine.end());
    return logLine;
}

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

std::vector<std::string> utilityClass::findPrintableVariable(std::string log_line, std::string logType)
{

	std::string temp = log_line; //store the log line in temperary variable
	std::string output = "";

	temp = removeSpaces(temp);
	temp = removeStringBetweenDoubleQuotes(temp);


	if (logType.substr(0,6)== "SERVER")
	{
		temp = temp.substr(logType.length() + 2, temp.length() - logType.length()-4);
	}
	else if (logType.substr(0,4) == "ALGO")
	{
		temp = temp.substr(logType.length() + 2, temp.length() - logType.length()-4);
	}

	vector<string> printableVariable = sliceStringAroundComma(temp); //find out all the values that has been used in the log

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
		}
	}
	return specifiers;

}

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

	//log_line = removeSpaces(log_line);

	for (int i =0;i<space_count;i++)
	{
		newLog = newLog + " ";
	}

	

	//cout<<log_line<<endl;

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
	
	int position = 0;
	int count = 0;
	bool flag = 0;

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
			flag = 1; 
			newLog = newLog + "\"<<" + variable[position] +"<<\"";
			position++;
			i = i+1;
		}
		else
		{
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

	newLog = newLog.substr(0,newLog.length()-3) + ";";
	return newLog;

}
	
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