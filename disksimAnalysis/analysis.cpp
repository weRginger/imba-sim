/*
 * Ziqi Fan: UMN CS PHD
 * This app designed for analysis large amount of result files from sim-ideal
 * The inputFileList contains the results wanted to be analyzed 
 * The parameterWant2Output is the parameter wanted to extract from those files
 * e.g. ./analysis fileList.txt "Total cache hit ratio,"
 * */

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>    // std::sort
#include <vector>       // std::vector

using namespace std;

int main(int argc, char *argv[]) {
    if(argc != 3)
	{
		cout<<"USAGE: ./analysis <inputFileList> <parameterWant2Output>"<<endl;
		return 0;
	}
    ifstream myfile(argv[1]);

    string targetParameter = argv[2];

	string lineInFile;
	string lineInSubfile;
	size_t found;
	
    if(myfile.is_open())
	{
		while(myfile.good())
		{
			getline (myfile,lineInFile);
			ifstream subfile(lineInFile.c_str());
			if(subfile.is_open())
			{
				while(subfile.good())
				{				
					getline (subfile,lineInSubfile);
					found = lineInSubfile.find(targetParameter);
					if(found != string::npos)
					{
						cout<<lineInSubfile.substr(targetParameter.length(),20)<<" ";
					}
				}
			}
		}
	}
	cout<<endl;

	return 0;
}
