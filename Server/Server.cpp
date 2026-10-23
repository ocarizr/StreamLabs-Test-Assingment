#include "pch.h"
#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <Windows.h>
#include <cstdlib>
#include <strsafe.h>
#include <fstream>

#define BUFSIZE 4096

VOID vSyncOperations();																// If the Server Manager chooses to provide Sync Connections
VOID vAsyncOperations();															// If the Server Manager chooses to provide Async Connections

std::map<int, std::string> vReadSyncFile(HANDLE h);									// Read Process for Synchronous Connection
std::map<int, std::vector<std::string>> vReadSyncCustomClass(HANDLE h);				// Read Process to Acquire the Custom Class Data - Synchronous Connection
std::map<int, std::string> vReadAsyncFile(HANDLE h);								// Read Process for Asynchronous Connection
std::map<int, std::vector<std::string>> vReadAsyncCustomClass(HANDLE h);			// Read Process to Acquire the Custom Class Data - Asynchronous Connection

bool bWriteSyncFile(HANDLE h, std::map<int, std::string> m);						// Write Trivial Data Request to the Client - Synchronous Connection
bool bWriteSyncCustomClass(HANDLE h, std::map<int, std::vector<std::string>> m);	// Write Custom Class/Object to the Client - Synchronous Connection
bool bWriteAsyncFile(HANDLE h, std::map<int, std::string> m);						// Write Trivial Data Request to the Client - Asynchronous Connection
bool bWriteAsyncCustomClass(HANDLE h, std::map<int, std::vector<std::string>> m);	// Write Custom Class/Object to the Client - Asynchronous Connection

bool vDisconnectAndConnect(HANDLE h, bool bAsync);									// Function responsible to free the pipe and propose a new connection to a new Client
VOID vCloseApp(HANDLE h, bool b);													// Function to close the app

using namespace std;
int main()
{
	int iCommand;

	bool bConnChoosed = false;

	// I will develop a simple UI just for testing purposes
	cout << "\t\t....PIPEFORGE SERVER...." << endl;
	cout << endl;
	while (!bConnChoosed)
	{
		cout << "Which Connection Type the server will Allow to its users?" << endl
			<< "1. Synchronous" << endl
			<< "2. Asynchronous" << endl
			<< "COMMAND -> ";
		cin >> iCommand;
		cout << endl;

		// Verify if the iCommand is a valid one and execute the respective functions
		switch (iCommand)
		{
		case 1:
			vSyncOperations();
			bConnChoosed = true;
			break;
		case 2:
			vAsyncOperations();
			bConnChoosed = true;
			break;
		default:
			cout << "ERROR -> Invalid Command" << endl;
			break;
		}
		cout << endl;
	}

	system("PAUSE");
}

VOID vSyncOperations()
{
	HANDLE hSync;																// Variable responsible for handle the Sync Calls

	bool bConnStatus = NULL;													// Stores the Connection Status

	string sRequest;															// Store the request process ID

	std::map<int, std::string> mData;											// The Data variable to be written to the Client
	std::map<int, std::vector<std::string>> mFileData;							// The Custom Class data

	ofstream osFile;															// Store the Data received from the Client
	ifstream isFile;															// Vectorize the data to be sent to the client

	string sLine;

	hSync = CreateNamedPipeA(
		"\\\\.\\pipe\\pSyncPipe",
		PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		4,
		BUFSIZE,
		BUFSIZE,
		0,
		NULL);

	if (hSync == INVALID_HANDLE_VALUE)
		cout << "ERROR -> " << GetLastError() << endl;
	while (1) {
		bConnStatus = vDisconnectAndConnect(hSync, false);

		if (bConnStatus) {
			cout << "Sync Connection made with Success" << endl;
			while (1)
			{
				mData = vReadSyncFile(hSync);
				sRequest = mData[0];
				if (sRequest == "w") {
					mData[0] = "w";													// The Server need to send a Standard Welcome Message to the Client
					mData[1] = "Welcome Client, You are Connected to ME!";
					if (!bWriteSyncFile(hSync, mData))
						cout << "ERROR -> " << GetLastError() << endl;

					cout << "SUCCESS -> Welcome Message sent to the Client." << endl;
				}
				else if (sRequest == "as") {
					mData[0] = "as";												// The Server will send to the Client all the Texts Saved
					isFile.open("TEXT.txt");
					if (isFile.is_open()) {
						int i = 0;
						while (getline(isFile, mData[i + 2])) {
							i++;
						}
						mData[1] = i;
						if (!bWriteSyncFile(hSync, mData)) {
							cout << "ERROR -> " << GetLastError() << endl;
							break;
						}

						cout << "SUCCESS -> Welcome Message sended to the Client." << endl;
					}
					else {
						cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
					}
				}
				else if (sRequest == "af") {
					mData[0] = "af";												// The Server will send to the Client all the Numbers Saved
					isFile.open("NUMBER.txt");
					if (isFile.is_open()) {
						int i = 0;
						while (getline(isFile, mData[i + 2])) {
							i++;
						}
						mData[1] = i;
						if (!bWriteSyncFile(hSync, mData)) {
							cout << "ERROR -> " << GetLastError() << endl;
							break;
						}

						cout << "SUCCESS -> Welcome Message sended to the Client." << endl;
					}
					else {
						cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
					}
				}
				else if (sRequest == "s") {											// The String will be saved into a file 
					osFile.open("TEXT.txt");										// The File is TEXT.txt
					if (osFile.is_open()) {
						osFile << mData[1] << endl;									// Inputs the data into the next line
						cout << "SUCCESS -> Data added to the Storage." << endl;
					}
					else {
						cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
					}
					osFile.close();
				}
				else if (sRequest == "f") {											// The Number will be saved into a file
					osFile.open("NUMBER.txt");										// The File is NUMBER.txt
					if (osFile.is_open()) {
						osFile << mData[1] << endl;									// Inputs the data into the next line
						cout << "SUCCESS -> Data added to the Storage." << endl;
					}
					else {
						cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
					}
					osFile.close();
				}
				else if (sRequest == "ccr") {
					mFileData = vReadSyncCustomClass(hSync);
					// The Custom object storage only is acessible by the Custom Class Process
					if (mData.size() > 2) {											// Verify if it is a Custom object
						osFile.open(("/" + mData[1] + "/" + mData[2] + ".txt").c_str());
						if (osFile.is_open()) {
							for (int i = 0; i < mFileData[0].size(); i++)
							{
								osFile << mFileData[0][i] << endl;					// Save all the object data within a file
							}														// Each file store data of only one Object
							cout << "SUCCESS -> Custom Object Saved." << endl;
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
					}
					else {															// If it isn't an object it will start the Custom Class register process
						osFile.open(("/" + mData[1] + "/" + mData[1] + ".cpp").c_str());
						if (osFile.is_open()) {
							for (int i = 0; i < mFileData[0].size(); i++)
							{
								osFile << mFileData[0][i] << endl;					// The register process only save the class file into a folder
							}
							cout << "SUCCESS -> *.cpp File Registered." << endl;
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
						osFile.open(("/" + mData[1] + "/" + mData[1] + ".h").c_str());
						if (osFile.is_open()) {
							for (int i = 0; i < mFileData[0].size(); i++)
							{
								osFile << mFileData[1][i] << endl;					// It saves to the header file into the same folder
							}
							cout << "SUCCESS -> *.h File Registered." << endl;
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
					}
				}
				else if (sRequest == "ccc") {
					// All the Custom Object Iterations are only acessible by the Custom Class Operations
					if (mData.size() > 2) {											// Custom Object Call Procedure
						isFile.open(("/" + mData[1] + "/" + mData[2] + ".txt").c_str());
						if (isFile.is_open()) {
							while (getline(isFile, sLine))
							{
								mFileData[0].push_back(sLine);						// Input the data from the file into the vector
							}
							isFile.close();
							if (!bWriteSyncCustomClass(hSync, mFileData)) {
								cout << "ERROR -> " << GetLastError() << endl;
								break;
							}
							cout << "SUCCESS -> Custom Object Sended to Client." << endl;
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
					}
					else {
						isFile.open((mData[1] + ".cpp").c_str(), std::ifstream::in);
						if (isFile.is_open()) {
							while (getline(isFile, sLine))
							{
								mFileData[0].push_back(sLine);						// The data of the *.cpp file from the Custom Class
							}
							isFile.close();
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
						isFile.open((mData[1] + ".h").c_str(), std::ifstream::in);
						if (isFile.is_open()) {
							while (getline(isFile, sLine))
							{
								mFileData[1].push_back(sLine);						// The data of the *.h file from the Custom Class
							}
							isFile.close();
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
						if (!bWriteSyncCustomClass(hSync, mFileData)) {
							cout << "ERROR -> " << GetLastError() << endl;
							break;
						}
						cout << "SUCCESS -> Custom Object Sended to Client." << endl;
					}
				}
			}
		}
		else
			cout << "ERROR -> " << GetLastError() << endl;
	}

	vCloseApp(hSync, bConnStatus);
}

VOID vAsyncOperations()
{
	HANDLE hAsync;																// variable responsible for handle the Async Calls

	string sRequest;															// Store the request process ID

	bool bConnStatus = NULL;													// Stores the Connection Status

	std::map<int, std::string> mData;											// The Data variable to be written to the Client
	std::map<int, std::vector<std::string>> mFileData;							// The Custom Class data

	ofstream osFile;															// Store the Data received from the Client
	ifstream isFile;															// Vectorize the data to be sent to the client

	string sLine;

	hAsync = CreateNamedPipeA(
		"\\\\.\\pipe\\pAsyncPipe",
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		4,
		BUFSIZE,
		BUFSIZE,
		0,
		NULL);

	if (hAsync == INVALID_HANDLE_VALUE)
		cout << "ERROR -> " << GetLastError() << endl;

	while (1) {
		bConnStatus = vDisconnectAndConnect(hAsync, false);

		if (bConnStatus) {
			cout << "Sync Connection made with Success" << endl;
			while (1)
			{
				mData = vReadAsyncFile(hAsync);
				sRequest = mData[0];
				if (sRequest == "w") {
					mData[0] = "w";													// The Server need to send a Standard Welcome Message to the Client
					mData[1] = "Welcome Client, You are Connected to ME!";
					if (!bWriteAsyncFile(hAsync, mData))
						cout << "ERROR -> " << GetLastError() << endl;

					cout << "SUCCESS -> Welcome Message sended to the Client." << endl;
				}
				else if (sRequest == "as") {
					mData[0] = "as";												// The Server will send to the Client all the Texts Saved
					isFile.open("TEXT.txt");
					if (isFile.is_open()) {
						int i = 0;
						while (getline(isFile, mData[i + 2])) {
							i++;
						}
						mData[1] = i;
						if (!bWriteAsyncFile(hAsync, mData)) {
							cout << "ERROR -> " << GetLastError() << endl;
							break;
						}

						cout << "SUCCESS -> Welcome Message sended to the Client." << endl;
					}
					else {
						cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
					}
				}
				else if (sRequest == "af") {
					mData[0] = "af";												// The Server will send to the Client all the Numbers Saved
					isFile.open("NUMBER.txt");
					if (isFile.is_open()) {
						int i = 0;
						while (getline(isFile, mData[i + 2])) {
							i++;
						}
						mData[1] = i;
						if (!bWriteAsyncFile(hAsync, mData)) {
							cout << "ERROR -> " << GetLastError() << endl;
							break;
						}

						cout << "SUCCESS -> Welcome Message sended to the Client." << endl;
					}
					else {
						cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
					}
				}
				else if (sRequest == "s") {											// The String will be saved into a file 
					osFile.open("TEXT.txt");										// The File is TEXT.txt
					if (osFile.is_open()) {
						osFile << mData[1] << endl;									// Inputs the data into the next line
						cout << "SUCCESS -> Data added to the Storage." << endl;
					}
					else {
						cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
					}
					osFile.close();
				}
				else if (sRequest == "f") {											// The Number will be saved into a file
					osFile.open("NUMBER.txt");										// The File is NUMBER.txt
					if (osFile.is_open()) {
						osFile << mData[1] << endl;									// Inputs the data into the next line
						cout << "SUCCESS -> Data added to the Storage." << endl;
					}
					else {
						cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
					}
					osFile.close();
				}
				else if (sRequest == "ccr") {
					mFileData = vReadAsyncCustomClass(hAsync);
					// The Custom object storage only is acessible by the Custom Class Process
					if (mData.size() > 2) {											// Verify if it is a Custom object
						osFile.open(("/" + mData[1] + "/" + mData[2] + ".txt").c_str());
						if (osFile.is_open()) {
							for (int i = 0; i < mFileData[0].size(); i++)
							{
								osFile << mFileData[0][i] << endl;					// Save all the object data within a file
							}														// Each file store data of only one Object
							cout << "SUCCESS -> Custom Object Saved." << endl;
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
					}
					else {															// If it isn't an object it will start the Custom Class register process
						osFile.open(("/" + mData[1] + "/" + mData[1] + ".cpp").c_str());
						if (osFile.is_open()) {
							for (int i = 0; i < mFileData[0].size(); i++)
							{
								osFile << mFileData[0][i] << endl;					// The register process only save the class file into a folder
							}
							cout << "SUCCESS -> *.cpp File Registered." << endl;
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
						osFile.open(("/" + mData[1] + "/" + mData[1] + ".h").c_str());
						if (osFile.is_open()) {
							for (int i = 0; i < mFileData[0].size(); i++)
							{
								osFile << mFileData[1][i] << endl;					// It saves to the header file into the same folder
							}
							cout << "SUCCESS -> *.h File Registered." << endl;
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
					}
				}
				else if (sRequest == "ccc") {
					// All the Custom Object Iterations are only acessible by the Custom Class Operations
					if (mData.size() > 2) {											// Custom Object Call Procedure
						isFile.open(("/" + mData[1] + "/" + mData[2] + ".txt").c_str());
						if (isFile.is_open()) {
							while (getline(isFile, sLine))
							{
								mFileData[0].push_back(sLine);						// Input the data from the file into the vector
							}
							isFile.close();
							if (!bWriteAsyncCustomClass(hAsync, mFileData)) {
								cout << "ERROR -> " << GetLastError() << endl;
								break;
							}
							cout << "SUCCESS -> Custom Object Sended to Client." << endl;
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
					}
					else {
						isFile.open((mData[1] + ".cpp").c_str(), std::ifstream::in);
						if (isFile.is_open()) {
							while (getline(isFile, sLine))
							{
								mFileData[0].push_back(sLine);						// The data of the *.cpp file from the Custom Class
							}
							isFile.close();
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
						isFile.open((mData[1] + ".h").c_str(), std::ifstream::in);
						if (isFile.is_open()) {
							while (getline(isFile, sLine))
							{
								mFileData[1].push_back(sLine);						// The data of the *.h file from the Custom Class
							}
							isFile.close();
						}
						else {
							cout << "ERROR -> Failed to load the file. No " << GetLastError() << endl;
						}
						if (!bWriteAsyncCustomClass(hAsync, mFileData)) {
							cout << "ERROR -> " << GetLastError() << endl;
							break;
						}
						cout << "SUCCESS -> Custom Object Sended to Client." << endl;
					}
				}
			}
		}
		else
			cout << "ERROR -> " << GetLastError() << endl;
	}

	vCloseApp(hAsync, bConnStatus);
}

std::map<int, std::string> vReadSyncFile(HANDLE h)
{
	map<int, std::string> mData;

	DWORD
		dwBytesToRead = sizeof(mData),
		dwBytesRead;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		cout << "ERROR -> " << GetLastError() << endl;

	bool bReadSync = ReadFile(
		h,
		&mData,
		dwBytesToRead,
		&dwBytesRead,
		NULL);

	if (bReadSync)
	{
		return mData ;
	}

	cout << "ERROR -> " << GetLastError();
	mData[0] = "0";

	if (GetLastError() == ERROR_BROKEN_PIPE)												// If Pipe loses connection the system will ask for a new one
	{
		vDisconnectAndConnect(h, false);
	}

	return mData;
}

std::map<int, std::vector<std::string>> vReadSyncCustomClass(HANDLE h)
{
	map<int, std::vector<std::string>> mData;
	mData.clear();

	DWORD
		dwBytesToRead = sizeof(mData),
		dwBytesRead;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		cout << "ERROR -> " << GetLastError() << endl;

	bool bReadSync = ReadFile(
		h,
		&mData,
		dwBytesToRead,
		&dwBytesRead,
		NULL);

	if (bReadSync)
		return mData;

	return mData;
}

std::map<int, std::string> vReadAsyncFile(HANDLE h)
{
	map<int, std::string> mData;

	DWORD dwBytesToRead = sizeof(mData);

	OVERLAPPED opRead;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		cout << "ERROR -> " << GetLastError() << endl;

	bool bReadSync = ReadFile(
		h,
		&mData,
		dwBytesToRead,
		NULL,
		&opRead);

	if (GetLastError() == ERROR_IO_PENDING) {
		cout << endl << "Loading";
		while (!HasOverlappedIoCompleted(&opRead))
		{
			cout << ".";
		}
		cout << endl;
	}
	if (GetOverlappedResult(h, &opRead, &dwBytesToRead, true) != 0)
		return mData;

	mData[0] = "0";

	if (GetLastError() == ERROR_BROKEN_PIPE)												// If Pipe loses connection the system will ask for a new one
	{
		vDisconnectAndConnect(h, true);
	}

	return mData;
}

std::map<int, std::vector<std::string>> vReadAsyncCustomClass(HANDLE h)
{
	map<int, std::vector<std::string>> mData;
	mData.clear();

	DWORD dwBytesToRead = sizeof(mData);

	OVERLAPPED opRead;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		cout << "ERROR -> " << GetLastError() << endl;

	bool bReadSync = ReadFile(
		h,
		&mData,
		dwBytesToRead,
		NULL,
		&opRead);

	if (GetLastError() == ERROR_IO_PENDING) {
		cout << endl << "Loading";
		while (!HasOverlappedIoCompleted(&opRead))
		{
			cout << ".";
		}
		cout << endl;
	}
	if (GetOverlappedResult(h, &opRead, &dwBytesToRead, true) != 0)
		return mData;

	return mData;
}

bool bWriteSyncFile(HANDLE h, std::map<int, std::string> m)
{
	DWORD dwBytesToWrite = sizeof(m);														// Variable who contain the size of the data the User wants to send
	DWORD dwBytesWriten;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		cout << "ERROR -> " << GetLastError() << endl;

	bool bWrite = WriteFile(																// The Write Function
		h,
		&m,
		dwBytesToWrite,
		&dwBytesWriten,
		NULL);

	if (bWrite)																				// In case the Write process is succefull bWrite will be true
		return true;

	return false;
}

bool bWriteSyncCustomClass(HANDLE h, std::map<int, std::vector<std::string>> m)
{
	DWORD dwBytesToWrite = sizeof(m);														// Variable who contain the size of the data the User wants to send
	DWORD dwBytesWriten;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		cout << "ERROR -> " << GetLastError() << endl;

	bool bWrite = WriteFile(																// The Write Function
		h,
		&m,
		dwBytesToWrite,
		&dwBytesWriten,
		NULL);

	if (bWrite)																				// In case the Write process is succefull bWrite will be true
		return true;

	return false;
}

bool bWriteAsyncFile(HANDLE h, std::map<int, std::string> m)
{
	OVERLAPPED opWrite;

	DWORD dwBytesToWrite = sizeof(m);

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		cout << "ERROR -> " << GetLastError() << endl;

	bool bWrite = WriteFile(
		h,
		&m,
		dwBytesToWrite,
		NULL,
		&opWrite);

	if (GetLastError() == ERROR_IO_PENDING) {
		cout << endl << "Loading";
		while (!HasOverlappedIoCompleted(&opWrite))
		{
			cout << ".";
		}
		cout << endl;
	}
	if (GetOverlappedResult(h, &opWrite, &dwBytesToWrite, true) != 0)
		return true;

	return false;
}

bool bWriteAsyncCustomClass(HANDLE h, std::map<int, std::vector<std::string>> m)
{
	OVERLAPPED opWrite;

	DWORD dwBytesToWrite = sizeof(m);

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		cout << "ERROR -> " << GetLastError() << endl;

	bool bWrite = WriteFile(
		h,
		&m,
		dwBytesToWrite,
		NULL,
		&opWrite);

	if (GetLastError() == ERROR_IO_PENDING) {
		cout << endl << "Loading";
		while (!HasOverlappedIoCompleted(&opWrite))
		{
			cout << ".";
		}
		cout << endl;
	}
	if (GetOverlappedResult(h, &opWrite, &dwBytesToWrite, true) != 0)
		return true;

	return false;
}

bool vDisconnectAndConnect(HANDLE h, bool bAsync)
{
	cout << endl																		// Printing Connection Message
		<< "Conecting to a Client." << endl
		<< endl;

	DisconnectNamedPipe(h);

	if (bAsync)
	{
		OVERLAPPED opConn;																// Variable to store the asynchronous process status
		DWORD dwTemp;
		if (!ConnectNamedPipe(h, &opConn)) {

			if (GetLastError() == ERROR_IO_PENDING || GetLastError() == ERROR_PIPE_LISTENING) {
				cout << endl << "Loading";
				while (!HasOverlappedIoCompleted(&opConn))
				{
					cout << ".";
				}
				cout << endl;
			}

			if (GetOverlappedResult(h, &opConn, &dwTemp, true) != 0)
				return true;
			else
				return false;
		}
		else
		{
			return true;
		}
	}
	else {
		if (ConnectNamedPipe(h, NULL))													// Connect Synchronously
			return true;

		return false;
	}
}

VOID vCloseApp(HANDLE h, bool b)
{
	cout << "Closing the Application" << endl;
	system("PAUSE");
	if (b)
	{
		DisconnectNamedPipe(h);
		CloseHandle(h);
	}
	exit(0);
}
