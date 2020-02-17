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

#include "DefaultConnection.h"

bool vWriteSync(HANDLE h, std::map<int, std::string> m);									// Send Message to the Server - Synchronous Connection
bool vWriteAsync(HANDLE h, std::map<int, std::string> m);									// Send Message to the Server - Asynchronous Connection
bool bWriteSyncCC(HANDLE h, std::map<int, std::vector<std::string>> m);
bool bWriteAsyncCC(HANDLE h, std::map<int, std::vector<std::string>> m);

std::map<int, std::string> mReadSync(HANDLE h);												// Read Message from the Server - Synchronous Connection
std::map<int, std::string> mReadAsync(HANDLE h);											// Read Message from the Server - Asynchronous Connection

std::map<int, std::vector<std::string>> vecReadSync(HANDLE h);								// Custom Class Synchronous Read Function
std::map<int, std::vector<std::string>> vecReadAsync(HANDLE h);								// Custom Class Asynchronous Read Function

int iStringToInt(std::string s);															// Function to convert Strings to Numbers

std::string sGetFileName();																	// Function to return the file path and name

std::wstring wsStringToWstring(std::string s);												// Convert String to WString in order to open the Custom Class

VOID vExecuteCustomClass(HINSTANCE h);														// Function to execute the custom class functions

using namespace std;
int main()
{
	HANDLE hCreateFile = NULL;																// NamedPipe Handle

	IConnection* ConnectionHandler = new DefaultConnection("///.//pipe");					// Connection Handler

	int iCommand = NULL;																	// Variable who stores the User Commands

	bool
		bAsync = false;																		// Variable who stores if the Connection is Asynchronous or not

	char sBuffer[1023];																		// Generic variable who stores the data to be sent or received from the server

	string
		sLine,																				// String who stores line by line the Custom Class Data
		sSystemCommand;																		// Create the correct System Command during runtime

	map<int, std::string> mData;															// The data sent to the server will be a dictionary
	map<int, std::vector<std::string>> fileData;											// The files Data

	ifstream
		cppFile,																			// Variable who Input the *.cpp file to the system
		headerFile,																			// Variable who Input the *.h file to the system
		isCustomClassExe;																	// Variable to verify if the compiling process succeeded

	ofstream
		osCppFile,																			// Variable who will Output the *.cpp received from the Server
		osHFile;																			// Variable who will Output the *.h received from the Server

	std::vector<std::string>																// A vector of strings
		vecCppFile,																			// The *.cpp file data	
		vecHFile;																			// The *.h file data

	HINSTANCE hCustomClassLoad;																// The Handle to Load the Custom Class executable

	// I will develop a small UI in order to make the test of the system easier
	cout << "\t\t....STREAMLABS TEST ASSINGMENT CLIENT APP...." << endl;

	// Will loop until the Application Connect Successfull with the Server or the User chooses to close the app
	while (!ConnectionHandler->IsConnected())
	{
		cout << "Choose a Connection type:" << endl
			<< "1. Synchronous." << endl
			<< "2. Asynchronous." << endl
			<< "3. Close the App." << endl
			<< "Command -> ";
		cin >> iCommand;
		cout << endl;

		// Verify if the iCommand is a valid one and execute the respective functions
		switch (iCommand)																	
		{
		case 1:
			if(!ConnectionHandler->Connect(hCreateFile))
				cout << "ERROR -> " << GetLastError() << endl;
			break;
		case 2:
			if (ConnectionHandler->Connect(hCreateFile, true))								// If the namedpipe file doesn't exist it will throw an error
			{
				bAsync = true;
			}
			else
				cout << "ERROR -> " << GetLastError() << endl;
			break;
		case 3:
			system("Exit");
			break;
		default:
			cout << "ERROR -> Invalid Command" << endl;										// If the User choosed an inexistent command
			cout << endl;
			break;
		}
	}

	cout << "The Client is Connected to the Server." << endl << endl;

	system("PAUSE");

	while (1)
	{
		system("CLS");

		// Print Header
		cout << "\t\t....STREAMLABS TEST ASSINGMENT CLIENT APP...." << endl;
		if (bAsync)
			cout << "\t\t..ASYNCHRONOUS CONNECTION.." << endl;
		else
			cout << "\t\t..SYNCHRONOUS CONNECTION.." << endl;

		// Main Menu
		cout << "Choose an operation." << endl
			<< "0. Close the app." << endl
			<< "1. Read/Write." << endl
			<< "2. Register a Custom Class." << endl
			<< "3. Initialize Custom Class." << endl
			<< "COMMAND ->";
		cin >> iCommand;
		cout << endl;

		// If 0 the While Loop breaks and the vCloseApp routine execute
		if (iCommand == 0)
			break;
		else
		{
			switch (iCommand)
			{
			case 1:																			// The User wants to Read data or Write Data to Server?
				cout << "Read or Write?" << endl
					<< "1. Read from Server." << endl
					<< "2. Write to server." << endl
					<< "COMMAND -> ";
				cin >> iCommand;
				cout << endl;

				switch (iCommand)
				{
				case 1:
					cout << "What you want to read?" << endl
						<< "1. Welcome Message from the Server." << endl					// The Client Reads a Connection Confirm Message from the Server.
						<< "2. All Text saved on the Server." << endl						// The Client will receive all TEXT data stored 
						<< "3. All Numbers saved on the Server" << endl						// The Client will receive all the NUMBER data stored 
						<< "COMMAND -> ";
					cin >> iCommand;
					cout << endl;

					switch (iCommand)
					{
					// The procces >> Request Data >> Receive Requested Data << Will repeat for all the operations
					// The difference is the type of Data received from the Server
					case 1:																	// Asking the Welcome Message
						mData[0] = "w";														// W for welcome
						if (bAsync) {														// The proccess in an Asynchornous Connection
							if (vWriteAsync(hCreateFile, mData))							// First the client request the data to the server
								cout << "SUCCESS -> Data Requested." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;

							mData = mReadSync(hCreateFile);									// Then the server answer the client with the requested Data
							if (mData[0] != "0")											
							{
								cout << "SUCCESS -> " << mData[1] << endl;
							}
							else															// In case the Read Operation fails
							{
								cout << "ERROR -> " << GetLastError() << endl;
							}
						}
						else {																// The proccess in a Synchornous Connection
							if (vWriteSync(hCreateFile, mData))								// First the client request the data to the server						
								cout << "SUCCESS -> Data requested." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;

							mData = mReadAsync(hCreateFile);								// Then the server answer the client with the requested Data
							if (mData[0] != "0")
							{
								cout << "SUCCESS -> " << mData[1] << endl;
							}
							else															// In case the Read Operation fails
							{
								cout << "ERROR -> " << GetLastError() << endl;
							}
						}
						break;
					case 2:
						mData[0] = "as";													// AS for All Strings (TEXT)
						if (bAsync) {														// The proccess in an Asynchornous Connection
							if (vWriteAsync(hCreateFile, mData))							// First the client request the data to the server
								cout << "SUCCESS -> Data Requested." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;

							mData = mReadSync(hCreateFile);									// Then the server answer the client with the requested Data
							if (mData[0] != "0")
							{
								cout << "SUCCESS -> DATA REQUESTED:" << endl;				// Inform the Client the Data is Received
								
								for (int i = 0; i < iStringToInt(mData[1]); i++)
								{
									cout << i + 1 << ". " << mData[i + 2] << endl;			// Print all the Texts stored in the Server
								}
								cout << endl;
							}
							else															// In case the Read Operation fails
							{
								cout << "ERROR -> " << GetLastError() << endl;
							}
						}
						else {																// The proccess in a Synchornous Connection
							if (vWriteSync(hCreateFile, mData))								// First the client request the data to the server						
								cout << "SUCCESS -> Data requested." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;

							mData = mReadAsync(hCreateFile);								// Then the server answer the client with the requested Data
							if (mData[0] != "0")
							{
								cout << "SUCCESS -> DATA REQUESTED:" << endl;				// Inform the Client the Data is Received

								for (int i = 0; i < iStringToInt(mData[1]); i++)
								{
									cout << i + 1 << ". " << mData[i + 2] << endl;			// Print all the Texts stored in the Server
								}
								cout << endl;
							}
							else															// In case the Read Operation fails
							{
								cout << "ERROR -> " << GetLastError() << endl;
							}
						}
						break;
					case 3:
						mData[0] = "af";													// AF for All Floats (NUMBER)
						if (bAsync) {														// The proccess in an Asynchornous Connection
							if (vWriteAsync(hCreateFile, mData))							// First the client request the data to the server
								cout << "SUCCESS -> Data Requested." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;

							mData = mReadSync(hCreateFile);									// Then the server answer the client with the requested Data
							if (mData[0] != "0")
							{
								cout << "SUCCESS -> DATA REQUESTED:" << endl;				// Inform the Client the Data is Received

								for (int i = 0; i < iStringToInt(mData[1]); i++)
								{
									cout << i + 1 << ". " << mData[i + 2] << endl;			// Print all the Texts stored in the Server
								}
								cout << endl;
							}
							else															// In case the Read Operation fails
							{
								cout << "ERROR -> " << GetLastError() << endl;
							}
						}
						else {																// The proccess in a Synchornous Connection
							if (vWriteSync(hCreateFile, mData))								// First the client request the data to the server						
								cout << "SUCCESS -> Data requested." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;

							mData = mReadAsync(hCreateFile);								// Then the server answer the client with the requested Data
							if (mData[0] != "0")
							{
								cout << "SUCCESS -> DATA REQUESTED:" << endl;				// Inform the Client the Data is Received

								for (int i = 0; i < iStringToInt(mData[1]); i++)
								{
									cout << i + 1 << ". " << mData[i + 2] << endl;			// Print all the Texts stored in the Server
								}
								cout << endl;
							}
							else															// In case the Read Operation fails
							{
								cout << "ERROR -> " << GetLastError() << endl;
							}
						}
						break;
					default:
						cout << "ERROR -> Invalid Command." << endl;						// If the User choosed an inexistent command
						cout << endl;
						break;
					}
					break;
				case 2:																		// In case he wants to Write the system needs to know which type
					cout << "Do you want to write a Text or a Number?" << endl				// Of data the User wants to send to the Server
						<< "1. Text." << endl
						<< "2. Number." << endl
						<< "COMMAND -> ";
					cin >> iCommand;
					cout << endl;

					switch (iCommand)
					{
					case 1:																	// Case the User chooses to save a Text
						cout << "Type the Text you want to store on the server (Limit if 1000 characteres)" << endl
							<< "TEXT -> ";
						cin >> sBuffer;
						
						mData[0] = "s";														// S for string (TEXT)
						mData[1] = sBuffer;													// The Data the User want to save

						if (bAsync) {
							if (vWriteAsync(hCreateFile, mData))
								cout << "SUCCESS -> Data sent to server with success." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;
						}
						else {
							if (vWriteSync(hCreateFile, mData))
								cout << "SUCCESS -> Data sent to server with success." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;
						}
						break;
					case 2:																	// case the User chooses to save a Number
						cout << "Type the Number you want to store on the server" << endl
							<< "NUMBER -> ";
						cin >> sBuffer;

						mData[0] = "f";														// F for float (NUMBER)
						mData[1] = sBuffer;													// The Data the user want to save

						if (bAsync) {
							if (vWriteAsync(hCreateFile, mData))
								cout << "SUCCESS -> Data sent to server with success." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;
						}
						else {
							if (vWriteSync(hCreateFile, mData))
								cout << "SUCCESS -> Data sent to server with success." << endl;
							else
								cout << "ERROR -> " << GetLastError() << endl;
						}
						break;
					default:
						cout << "ERROR -> Invalid Command" << endl;							// If the User choosed an inexistent command
						cout << endl;
						break;
					}
					break;
				default:
					cout << "ERROR -> Invalid Command" << endl;								// If the User choosed an inexistent command
					cout << endl;
					break;
				}
				break;
			case 2:
				cout << "In order to register a Custom Class you need both *.cpp and *.h files." << endl;
				cout << endl;
				system("PAUSE");
				cout << endl
					<< "Name of the class: ";
				mData[0] = "ccr";															// CCR for Custom Class Register
				cin >> mData[1];

				cout << "Press ENTER to select the *.cpp File:" << endl;
				vecCppFile.clear();
				cppFile.open(sGetFileName(), std::ifstream::in);
				if (cppFile.is_open())														// Open the file to read it's data
				{
					while (getline(cppFile, sLine))
					{
						vecCppFile.push_back(sLine);
					}
					cppFile.close();
				}
				else
				{
					cout << "ERROR -> " << GetLastError() << " - Unable to open file" << endl;
				}
				fileData[0] = vecCppFile;													// Add the *.cpp file data to the dictionary

				cout << endl
					<< "Press ENTER to select the *.h File:" << endl;
				vecHFile.clear();
				headerFile.open(sGetFileName(), std::ifstream::in);
				if (headerFile.is_open())													// Open the file to read it's data
				{
					while (getline(headerFile, sLine))
					{
						vecHFile.push_back(sLine);
					}
					headerFile.close();
				}
				else
				{
					cout << "ERROR -> " << GetLastError() << " - Unable to open file" << endl;
				}
				fileData[1] = vecHFile;														// Add the *.h file data to the dictionary

				if (bAsync)
				{
					if (vWriteAsync(hCreateFile, mData))									// Notify the server that the client will send a Custom Class to register
					{
						cout << "SUCCESS -> Server Accepted Register Request." << endl;
						if (bWriteAsyncCC(hCreateFile, fileData))							// Send the Custom Class data to the server
						{
							cout << "SUCCESS -> Custom Class Registered in the Server." << endl << endl;
						}
						else
						{
							cout << "ERROR -> " << GetLastError() << endl << endl;
						}
					}
					else
					{
						cout << "ERROR -> " << GetLastError() << endl << endl;
					}
				}
				else
				{
					if (vWriteSync(hCreateFile, mData))										// Notify the server that the client will send a Custom Class to register
					{
						cout << "SUCCESS -> Server Accepted Register Request." << endl;
						if (bWriteAsyncCC(hCreateFile, fileData))							// Send the Custom Class data to the server
						{
							cout << "SUCCESS -> Custom Class Registered in the Server." << endl << endl;
						}
						else
						{
							cout << "ERROR -> " << GetLastError() << endl << endl;
						}
					}
					else
					{
						cout << "ERROR -> " << GetLastError() << endl << endl;
					}
				}
				break;
			case 3:
				cout << "Which Custom Class you want to use?" << endl
					<< "COMMAND -> ";
				mData[0] = "ccc";															// CCC for Custom Class Call
				cin >> mData[1];															// Store the Custom Class Name

				fileData.clear();

				if (bAsync)
				{
					if (vWriteAsync(hCreateFile, mData))
					{
						fileData = vecReadAsync(hCreateFile);

						if (fileData.empty())												// In case the custom class read process fail
							cout << "ERROR -> Failed to retrieve the Custom Class - No. " << GetLastError() << endl;

						cout << "SUCCESS -> Loaded Custom Class" << endl;
					}
					else
					{
						cout << "ERROR -> Failed to request Custom Class - No. " << GetLastError();
					}
				}
				else
				{
					if (vWriteSync(hCreateFile, mData))
					{
						fileData = vecReadSync(hCreateFile);

						if (fileData.empty())												// In case the custom class read process fail
							cout << "ERROR -> Failed to retrieve the Custom Class - No. " << GetLastError() << endl;

						cout << "SUCCESS -> Loaded Custom Class" << endl;
					}
					else
					{
						cout << "ERROR -> Failed to request Custom Class - No. " << GetLastError();
					}
				}
				cout << endl;

				sLine = mData[1] + ".cpp";													// Used the sLine Variable to define the Custom Class *.cpp file name

				osCppFile.open(sLine);

				if (osCppFile.is_open())
				{
					for (int i = 0; i < fileData[0].size(); i++)
					{
						osCppFile << fileData[0][i] << endl;
					}
				}

				sLine = mData[1] + ".h";													// Used the sLine Variable to define the Custom Class *.cpp file name
				osHFile.open(sLine);

				if (osHFile.is_open())
				{
					for (int i = 0; i < fileData[0].size(); i++)
					{
						osHFile << fileData[0][i] << endl;
					}
				}

				// The Client PC MUST HAVE MinGw Compiler installed
				sSystemCommand = "c:\mingw\bin\g++ " + mData[1] + ".cpp -o " + mData[1] + ".exe";
				system(sSystemCommand.c_str());												// Compile the custom Class

				sSystemCommand = mData[1] + ".exe";
				isCustomClassExe.open(sSystemCommand, ifstream::in);

				if (!isCustomClassExe.good())												// Verify if the Compiled file exists
				{
					cout << "ERROR -> " << GetLastError() << endl;
					break;
				}

				// After finish the execution ofthe custom class the system delete the *.cpp;
				while (1)
				{
					if ((remove((mData[1] + ".cpp").c_str()) == 0))
						break;
				}

				// After finish the execution ofthe custom class the system delete the *.h;
				while (1)
				{
					if ((remove((mData[1] + ".h").c_str()) == 0))
						break;
				}

				// hCustomClassLoad = LoadLibrary(wsStringToWstring(sSystemCommand).c_str());	// Load the Custom Class;
				hCustomClassLoad = LoadLibrary(sSystemCommand.c_str());							// Load the Custom Class;
				if (!hCustomClassLoad)
				{
					cout << "ERROR -> Custom Class could not be loaded. No. " << GetLastError() << endl;
					break;
				}

				vExecuteCustomClass(hCustomClassLoad);										// Function to initialize the Custom Class Functions and Objects

				// After finish the execution ofthe custom class the system delete the *.exe;
				while (1)
				{
					if ((remove(sSystemCommand.c_str()) == 0))
						break;
				}
				break;
			default:
				cout << "ERROR -> Invalid Command" << endl;									// If the User choose an inexistent command
				cout << endl;
				break;
			}
		}
		cout << endl;
		system("PAUSE");
	}

	ConnectionHandler->Disconnect(hCreateFile);
}

bool vWriteSync(HANDLE h, std::map<int, std::string> m)
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

bool vWriteAsync(HANDLE h, std::map<int, std::string> m)
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

bool bWriteSyncCC(HANDLE h, std::map<int, std::vector<std::string>> m)
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

bool bWriteAsyncCC(HANDLE h, std::map<int, std::vector<std::string>> m)
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

std::map<int, std::string> mReadSync(HANDLE h)
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
		return mData;

	mData[0] = "0";
	return mData;
}

std::map<int, std::string> mReadAsync(HANDLE h)
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
	return mData;
}

std::map<int, std::vector<std::string>> vecReadSync(HANDLE h)
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

std::map<int, std::vector<std::string>> vecReadAsync(HANDLE h)
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

int iStringToInt(std::string s)
{
	stringstream sDataLength(s);															// Variable to store the string data
	int iLength = NULL;
	sDataLength >> iLength;																	// Move the data into the int variable

	return iLength;																			// return the data converted into an Int
}

std::string sGetFileName()
{
	OPENFILENAMEA ofn;																		// Common Dialog Box Structure
	char szfile[300];																		// The Buffer Size
	HWND hwnd = HWND();																				// The Window Owner

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFile = szfile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szfile);
	ofn.lpstrFilter = "All\0*.*\0Text\0*.TXT\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetOpenFileNameA(&ofn))
	{
		cout << "SUCCESS -> Open File " << ofn.lpstrFile << endl;
		return ofn.lpstrFile;
	}

	return NULL;
}

std::wstring wsStringToWstring(std::string s)
{
	int len;
	int slength = (int)s.length() + 1;
	len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
	wchar_t* buf = new wchar_t[len];
	MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
	std::wstring r(buf);
	delete[] buf;
	return r;
}

VOID vExecuteCustomClass(HINSTANCE h)
{
	int iCommand;
	string sFunction;
	bool bBreak = false;

	while (!bBreak)
	{
		cout << "Do You Want to Quit" << endl
			<< "1. No." << endl
			<< "2. Yes." << endl
			<< "COMMAND -> ";
		cin >> iCommand;
		cout << endl;

		switch (iCommand)
		{
		case 1:
		{
			cout << "Please Type the Custom Class Function Name You Want to Use." << endl
				<< "COMMAND -> ";
			cin >> sFunction;
			cout << endl;
			auto vCustomFunction = GetProcAddress(h, sFunction.c_str());
			vCustomFunction();
		}
			break;
		case 2:
			bBreak = true;
			break;
		default:
			cout << "ERROR -> Invalid User Command." << endl;
			break;
		}
	}
	return;
}