/*
	Original author of the starter code
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date: 2/8/20
	
	Please include your Name, UIN, and the date below
	Name: 
	UIN: 
	Date: 
*/
#include "common.h"
#include "FIFORequestChannel.h"
#include <sys/time.h>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;

int main(int argc, char *argv[])
{
    int pid = fork();

    if (pid == 0)
    {
        char* const sv_argv[] = { (char*)"server", nullptr };
        execvp("./server", sv_argv);
    }
    else
    {
        // this is the parent process
        FIFORequestChannel chan("control", FIFORequestChannel::CLIENT_SIDE); // create client

        // need to determine whether user wants to request a point or a data file
        // set default values

        int c = 0; // used to read flags from command line

        int patient = -1;
        double time = -1;
        int ecg = 0;
        bool newChannel = false;
        string fileName = "";

        int buffercapacity = MAX_MESSAGE; // change this in order to increase buffer capacity

        // depending on the flags entered in the command line, we determine whether client wants to request a file, data point, or new channel
        while ((c = getopt(argc, argv, "p:t:e:f:cm:")) != -1)
        { //gets arguments from command line and puts them into c
            switch (c)
            {
            case 'p': //if p flag is present
                if (optarg)
                {
                    patient = atoi(optarg);
                } //set patient to value of p flag
                break;
            case 't':
                if (optarg)
                { // if t flag is present
                    // we round the argument to nearest 0.004 seconds
                    time = atof(optarg); // set time to value of t flag
                }
                break;
            case 'e': // if e flag is present
                if (optarg)
                {
                    ecg = atoi(optarg);
                }
                break;
            case 'f': // in this case the user is wanting to request a whole file
                if (optarg)
                {
                    fileName = string(optarg);
                }
                break;
            case 'c':
                newChannel = true; //set new channel boolean to true
                break;
            case '?':
                EXITONERROR("Invalid Option");
                break;
            case 'm':
                cout << "Changing initiatl buffer capacity of: " << buffercapacity << "..." << endl;
                buffercapacity = atoi(optarg);
                cout << "DONE! New buffer capacity is: " << buffercapacity << endl;
                break;
            }
        }

        //once we have processed the command line message, we must make sure arguments are valid
        if ((time < 0 || time > 59.996) && time != -1)
        {
            EXITONERROR("Invalid time");
        }
        if (ecg < 0 || ecg > 2)
        {
            EXITONERROR("Invalid ecg value");
        }

        // we now determine whether the client wants to request a data point or a whole file
        if (time != -1 && ecg != 0)
        {
            if (patient < 1 || patient > 15)
            {
                EXITONERROR("Invalid patient");
            }
            // if the patient, ecg ,and time values are valid, we request the data point from server
            datamsg dataPoint(patient, time, ecg);
            chan.cwrite(&dataPoint, sizeof(datamsg));

            double result = 0.0;
            chan.cread(&result, sizeof(double));
            cout << "For person " << patient
                 << ", at time " << fixed << setprecision(3) << time
                 << ", the value of ecg " << ecg
                 << " is " << fixed << setprecision(2) << result << endl;
        }

        // if they do not want an individual data point, instead they want multiple points:
        else if (patient != -1)
        {
            if (patient < 1 || patient > 15)
            {
                EXITONERROR("Invalid patient");
            }

            struct timeval start; // used to calculate how long the process takes using this approach
            gettimeofday(&start, NULL);
            double totalTime = 0;
            // iterate through the data message and send to a new file
            ofstream myfile;
            mkdir("received", 0777);
            string fileName = "received/x" + to_string(patient) + ".csv";
            myfile.open(fileName);

            // iterate through first 1000 data points in file to match tests
            for (int i = 0; i < 1000; i++)
            {

                //first ecg column
                datamsg req1(patient, totalTime, 1);
                chan.cwrite(&req1, sizeof(datamsg));

                double received1 = 0.0;
                chan.cread(&received1, sizeof(double));
                myfile << totalTime << "," << received1 << ",";

                //second ecg column
                datamsg req2(patient, totalTime, 2);
                chan.cwrite(&req2, sizeof(datamsg));
                double received2 = 0.0;
                chan.cread(&received2, sizeof(double));
                myfile << received2 << endl;
                totalTime += 0.004;
            }

            struct timeval end;
            gettimeofday(&end, NULL);

            // Now we need to convert the time to milliseconds and find the difference:
            double totalStart = 0;
            double totalEnd = 0;
            totalStart = (double)start.tv_usec + (double)start.tv_sec * 1000000;
            totalEnd = (double)end.tv_usec + (double)end.tv_sec * 1000000;
            cout << "The data exchange performed took: " << totalEnd - totalStart << " microseconds." << endl;
        }

        // Next we handle the data transferring of full files
        else if (fileName != "")
        {
            // we first need to know the length of the file
            filemsg getLen(0, 0);
            std::vector<char> initial(sizeof(filemsg) + fileName.size() + 1);
            memcpy(initial.data(), &getLen, sizeof(filemsg));
            memcpy(initial.data() + sizeof(filemsg), fileName.c_str(), fileName.size() + 1);
            chan.cwrite(initial.data(), initial.size());
            __int64_t filelen;
            chan.cread(&filelen, sizeof(__int64_t));
            cout << "File lenght: " << filelen << endl;

            //define the output file
            ofstream myfile;
            string file_name = "received/" + fileName;
            myfile.open(file_name, ios::out | ios::binary); // make sure output file is binary
            struct timeval start;
            if (gettimeofday(&start, NULL) != 0) {
                perror("gettimeofday");
                exit(EXIT_FAILURE);
            } // starts the timer

            // now we send the message to get the entire file, divide in 256 byte-pieces
            __int64_t offset = 0;
            __int64_t length = filelen;
            std::vector<char> payload(buffercapacity);
            while (length > offset)
            {
                size_t chunk = static_cast<size_t>(min<__int64_t>(buffercapacity, length - offset));
                filemsg segment(offset, chunk);
                std::vector<char> header(sizeof(filemsg) + fileName.size() + 1);
                memcpy(header.data(), &segment, sizeof(filemsg));
                memcpy(header.data() + sizeof(filemsg), fileName.c_str(), fileName.size() + 1);
                chan.cwrite(header.data(), header.size());

                chan.cread(payload.data(), buffercapacity);
                myfile.write(payload.data(), chunk);
                offset += chunk;
            }
            // calculate how much time file exchange took
            struct timeval end;
            gettimeofday(&end, NULL);

            double totalStart = 0;
            double totalEnd = 0;
            totalStart = (double)start.tv_usec + (double)start.tv_sec * 1000000;
            totalEnd = (double)end.tv_usec + (double)end.tv_sec * 1000000;

            cout << "The data exchange performed took: " << totalEnd - totalStart << " microseconds" << endl;
        }
        else if (newChannel)
        {
            //cout << "New Channel" << endl;
            //if user requested a new channel
            MESSAGE_TYPE n = NEWCHANNEL_MSG;
            chan.cwrite(&n, sizeof(MESSAGE_TYPE));
            //cout << "Test1" << endl;

            std::vector<char> newChan(30);
            chan.cread(newChan.data(), buffercapacity);
            FIFORequestChannel newChannel(newChan.data(), FIFORequestChannel::CLIENT_SIDE);
            //cout << "Test2" << endl;

            // test to make sure that new channel can receive requests/send data
            datamsg testMessage(5, 0.32, 1);
            newChannel.cwrite(&testMessage, sizeof(datamsg));
            //cout << "Test3" << endl;

            double received = 0.0;
            newChannel.cread(&received, sizeof(double));
            cout << "The ecg 1 value for person 5 at time 0.32 was: " << received << endl;

            //close the channel
            MESSAGE_TYPE close = QUIT_MSG;
            newChannel.cwrite(&close, sizeof(MESSAGE_TYPE));

            //cout << "New Channel End" << endl;
        }
        // closing the channel
        MESSAGE_TYPE m = QUIT_MSG;
        chan.cwrite(&m, sizeof(MESSAGE_TYPE));
        // wait on child process
        // wait(NULL);
        usleep(1000000);
    }
}