#include <float.h>
#include "global.h"
#include "parser.h"

///trace
#include <iostream>
#include <fstream>
///std::ofstream outfile("fileserver_2000_RWandLBA.tmp");
///trace


/// ziqi convert to seconds
#define WINDOWS_TICK 10000000
#define SEC_TO_UNIX_EPOCH 11644473600LL

double  WindowsTickToUnixMilliSeconds(long long windowsTicks)
{
    return (double)(((double)windowsTicks / (double)WINDOWS_TICK - (double)SEC_TO_UNIX_EPOCH) * (double)1000);
}

bool  getAndParseTrace(std::ifstream &inputTrace, char *traceName, reqAtom *newn)
{
    int bcount_temp = 0;
    long long int time = 0;
    double doubleTypeTime = 0;
    int fetched = 0;
    unsigned long long int byteoff = 0;
    char *tempchar;
    char *r_w;
    char line[201];
    static uint32_t lineno = 0;

    ///ziqi: don't forget to use static. If not, baseTime would be reset to 0 in the while loop at the start of a new loop
    static double baseTime = 0;

    ///ziqi: FIXME bug here
    //static double old_time = 0;

    assert(inputTrace.good());

    while(!fetched) {

        std::string lineString;
        std::getline(inputTrace,  lineString);

        if(inputTrace.eof()) {
            //end of file
            _gConfiguration.maxLineNo = lineno; // record last line number
            return false;
        }

        strcpy(line, lineString.c_str());
        ++lineno;

        tempchar = strtok(line, " ,\t");

        //std::size_t foundRead = lineString.find("Read");
        //std::size_t foundWrite = lineString.find("Write");
        //std::size_t foundComma = lineString.find(",");

        // MSR trace parse, must end in .csv
        // Sample MSR trace lien:
        // 	Timestamp        ,Hostname,DiskNumber,Type  ,Offset     ,Size,ResponseTime
        // 128166554283938750,wdev    ,3         ,Write ,3154152960,4096 ,   2170
        //if ( (foundRead!=std::string::npos) || (foundWrite!=std::string::npos) ) {
        if ( NULL != strstr(traceName, "csv") ) {

            ///ziqi: if it is the first line, denote its time stamp as base time. The following entry's time stamp need to be substracted by base time.
            if(lineno == 1) {
                time = strtoll(tempchar, NULL, 10);
                if(WindowsTickToUnixMilliSeconds(time) <= DBL_MAX) {
                    baseTime = WindowsTickToUnixMilliSeconds(time);
                }
            }
            else {
                time = strtoll(tempchar, NULL, 10);
            }

            if(time) {
                if(WindowsTickToUnixMilliSeconds(time) <= DBL_MAX) {
                    newn->issueTime = WindowsTickToUnixMilliSeconds(time) - baseTime;
                }
                else {
                    fprintf(stderr, "ARH: request time reach to the double boundry\n");
                    fprintf(stderr, "line: %s", line);
                    ExitNow(1);
                }
                ///ziqi: FIXME I don't know what does the old_time doing here
                // 			if( old_time > newn->time){
                // 			  fprintf(stderr, "ARH: new time is small equal than old time\n");
                // 				fprintf(stderr, "line: %s", line);
                // 				ddbg_assert(0);
                // 			}

                //            if(old_time >= newn->issueTime) {
                //                newn->issueTime = old_time + 1;
                /*				fprintf(stderr, "ARH: new time is small equal than old time\n");
                			  fprintf(stderr, "line: %s", line);
                			  ddbg_assert(0);*/
                //          }

                //            if(old_time > newn->issueTime) {
                //                fprintf(stderr, "ARH: new time is small equal than old time\n");
                //                fprintf(stderr, "line: %s", line);
                //                ExitNow(1);
                //            }

                //            old_time = newn->issueTime;
                strtok(NULL, ","); //step over host name
                strtok(NULL, ","); //step over devno
                //ARH: msr traces only have one dev
                r_w = strtok(NULL, ","); //step over type

                if(strcmp(r_w, "Write") == 0) {
                    newn->flags = WRITE;
                    /*
                    ///trace
                    outfile<<"W";
                    ///trace
                    */
                }
                else if(strcmp(r_w, "Read") == 0) {
                    newn->flags = READ;
                    /*
                    ///trace
                    outfile<<"R";
                    ///trace
                    */
                }
                else
                    continue;

                byteoff = strtoull((strtok(NULL, " ,")) , NULL , 10) ;   //read byteoffset (byte)
                //ARH: comment this line to accept blkno 0
                // 			if(!byteoff) {
                // 				continue;
                // 			}
                // 			if(!byteoff % 512) {
                // 				PRINT(fprintf(stderr, "ARH: request byte offset is not aligned to sector size\n"););
                // 				PRINT(fprintf(stderr, "line: %s", line););
                // 			} else {
                newn->fsblkno = (byteoff / _gConfiguration.fsblkSize) ; //convert byte2sector and align to page size
                //TODO: fix this line

                /*
                ///trace
                outfile<<newn->fsblkno<<std::endl;
                ///trace
                	*/

                newn->ssdblkno = newn->fsblkno / _gConfiguration.ssd2fsblkRatio[0];
                // 			}
                bcount_temp = atoi((strtok(NULL, " ,")));   // read size

                if(!bcount_temp) {
                    continue;
                }

                if(!bcount_temp % 512) {
                    PRINT(fprintf(stderr, "ARH: request byte count is not aligned to sector size\n"););
                    PRINT(fprintf(stderr, "line: %s", line););
                }
                else {
                    ///ziqi: if the remaining request size is less than one block size, still count it as one.
                    ///ziqi: The following "if" statement works as getting the ceiling of bcount_temp / _gConfiguration.fsblkSize
                    if((bcount_temp % _gConfiguration.fsblkSize) == 0)
                        newn->reqSize = bcount_temp / _gConfiguration.fsblkSize;
                    else
                        newn->reqSize = bcount_temp / _gConfiguration.fsblkSize + 1;

                    //TODO: fix this line
                    if(newn->fsblkno % _gConfiguration.ssd2fsblkRatio[0] + newn->reqSize >= _gConfiguration.ssd2fsblkRatio[0]) {   // req size is big, going to share multiple block
                        newn->reqSize = _gConfiguration.ssd2fsblkRatio[0] - newn->fsblkno % _gConfiguration.ssd2fsblkRatio[0];
                    }
                }

                fetched = 1;
                newn->lineNo = lineno;
            } //end if(time)
        }

        // Trace parse ended in .spc
        // Sample WebSearch trace lien:
        // ASU, LBA,    Size, Opcode, Timestamp
        // 0,   657728, 8192, R,      0.011413
        //else if(foundComma!=std::string::npos) {
        else if( NULL != strstr(traceName, "spc") ) {
            newn->lineNo = lineno;

            fetched = 1;

            byteoff = strtoull((strtok(NULL, " ,")) , NULL , 10) ;   // LBA

            newn->fsblkno = byteoff ; //convert byte2sector and align to page size

            ///trace
            ///outfile<<newn->fsblkno;
            ///trace

            //TODO: fix this line
            newn->ssdblkno = newn->fsblkno / _gConfiguration.ssd2fsblkRatio[0];

            bcount_temp = atoi((strtok(NULL, " ,")));   // read size

            if(!bcount_temp) {
                continue;
            }

            if(!bcount_temp % 512) {
                PRINT(fprintf(stderr, "ARH: request byte count is not aligned to sector size\n"););
                PRINT(fprintf(stderr, "line: %s", line););
            }
            else {
                ///ziqi: if the remaining request size is less than one block size, still count it as one.
                ///ziqi: The following "if" statement works as getting the ceiling of bcount_temp / _gConfiguration.fsblkSize
                if((bcount_temp % _gConfiguration.fsblkSize) == 0)
                    newn->reqSize = bcount_temp / _gConfiguration.fsblkSize;
                else
                    newn->reqSize = bcount_temp / _gConfiguration.fsblkSize + 1;

                //TODO: fix this line
                if(newn->fsblkno % _gConfiguration.ssd2fsblkRatio[0] + newn->reqSize >= _gConfiguration.ssd2fsblkRatio[0]) {   // req size is big, going to share multiple block
                    newn->reqSize = _gConfiguration.ssd2fsblkRatio[0] - newn->fsblkno % _gConfiguration.ssd2fsblkRatio[0];
                }
            }

            r_w = strtok(NULL, ","); //step over type
            if(strcmp(r_w, "W") == 0 || strcmp(r_w, "w") == 0) {
                newn->flags = WRITE;

                ///trace
                ///outfile<<"W"<<std::endl;
                ///trace

            }
            else if(strcmp(r_w, "R") == 0 || strcmp(r_w, "r") == 0) {
                newn->flags = READ;

                ///trace
                ///outfile<<"R"<<std::endl;
                ///trace

            }
            //else
            //continue;

            tempchar = strtok(NULL, ",");
            ///ziqi: if it is the first line, denote its time stamp as base time. The following entry's time stamp need to be substracted by base time.
            if(lineno == 1) {
                doubleTypeTime = atof(tempchar);
                //if(WindowsTickToUnixMilliSeconds(time) <= DBL_MAX) {
                ///ziqi: in milliseconds
                baseTime = doubleTypeTime*1000;
                //}
            }
            else {
                doubleTypeTime = atof(tempchar);
            }

            //if(WindowsTickToUnixMilliSeconds(time) <= DBL_MAX) {
            ///ziqi: in milliseconds
            newn->issueTime = doubleTypeTime*1000 - baseTime;
            //}
            //else {
            //  fprintf(stderr, "ARH: request time reach to the double boundry\n");
            //  fprintf(stderr, "line: %s", line);
            // ExitNow(1);
            //}
        }
        // Trace end in .lis (dupont and citibank)
        // No read or wrtie in these traces, so assuming all writes for now
        // Four parameters:	blkno	size	day	request_no
        //                      303567 	7 	0 	0
        // Use lineno to fillup timestamp and 1 to fillup reqSize
        else if ( NULL != strstr(traceName, "lis") ) {
            newn->lineNo = lineno;

            fetched = 1;

            newn->fsblkno = strtoull(tempchar, NULL, 10) ;

            newn->reqSize = strtoull((strtok(NULL, " ,\t")) , NULL , 10) ;   // LBA
            newn->ssdblkno = newn->fsblkno / _gConfiguration.ssd2fsblkRatio[0];

            newn->flags = WRITE;

            ///ziqi: use line number as timestamp
            newn->issueTime = lineno;
        }
        // Trace ended in .LOG (DiskmonSSD.LOG and DiskmonSSD1.LOG)
        // Trace parameters: seqNum	timestamp	executionTime	diskNum	opcode	blkno	size
        //                   8		0.406375	0.00009537	1	Read	3889199	8
        // Use lineno to fillup timestamp and 1 to fillup reqSize
        else if ( NULL != strstr(traceName, "LOG") ) {
            newn->lineNo = lineno;

            fetched = 1;

            strtok(NULL, " ,\t"); // step over timestamp
            strtok(NULL, " ,\t"); // step over executionTime
            strtok(NULL, " ,\t"); // step over diskNum

            r_w = strtok(NULL, " ,\t"); // Read or Write?
            assert(r_w != NULL);
            if(strcmp(r_w, "Write") == 0) {
                newn->flags = WRITE;
            }
            else if(strcmp(r_w, "Read") == 0) {
                newn->flags = READ;
            }

            newn->fsblkno = strtoull((strtok(NULL, " ,\t")) , NULL , 10);
            newn->ssdblkno = newn->fsblkno / _gConfiguration.ssd2fsblkRatio[0];

            newn->reqSize = strtoull((strtok(NULL, " ,\t")) , NULL , 10);

            // use line number as timestamp
            newn->issueTime = lineno;
        }
        // Trace distilled.txt
        // Trace parameters: AMPM	accessTime	opcode	blkno		size
        //                   �W�� 	11:13:02 	W 	19535095 	8
        // Use lineno to fillup timestamp and 1 to fillup reqSize
        else if ( NULL != strstr(traceName, "distilled") ) {
            newn->lineNo = lineno;

            fetched = 1;

            strtok(NULL, " ,"); // step over accessTime

            r_w = strtok(NULL, " ,"); // Read or Write?
            assert(r_w != NULL);
            if(strcmp(r_w, "W") == 0) {
                newn->flags = WRITE;
            }
            else if(strcmp(r_w, "R") == 0) {
                newn->flags = READ;
            }

            newn->fsblkno = strtoull((strtok(NULL, " ,")) , NULL , 10);
            newn->ssdblkno = newn->fsblkno / _gConfiguration.ssd2fsblkRatio[0];

            newn->reqSize = strtoull((strtok(NULL, " ,")) , NULL , 10);

            // use line number as timestamp
            newn->issueTime = lineno;
        }
        // SpatialClock traces, use space as seperator instead of comma
        // Only have two parameters: Read or Write LAB
        //                          R/W 2786
        // Use lineno to fillup timestamp and 1 to fillup reqSize
        else {
            newn->lineNo = lineno;

            fetched = 1;

            r_w = tempchar; //step over type
            assert(r_w != NULL);
            if(strcmp(r_w, "W") == 0 || strcmp(r_w, "w") == 0) {
                newn->flags = WRITE;
            }
            else if(strcmp(r_w, "R") == 0 || strcmp(r_w, "r") == 0) {
                newn->flags = READ;
            }

            newn->fsblkno = strtoull((strtok(NULL, " ,")) , NULL , 10);
            newn->ssdblkno = newn->fsblkno / _gConfiguration.ssd2fsblkRatio[0];

            newn->reqSize = 1;

            // use line number as timestamp
            newn->issueTime = lineno;
        }
    }//end while fetched
    return true;
}