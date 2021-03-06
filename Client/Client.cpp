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

std::map<int, std::string> mReadSync(HANDLE h);												// Read Message from the Server - Synchronous Connection
std::map<int, std::string> mReadAsync(HANDLE h);											// Read Message from the Server - Asynchronous Connection

std::map<int, std::vector<std::string>> vecReadSync(HANDLE h);								// Custom Class Synchronous Read Function
std::map<int, std::vector<std::string>> vecReadAsync(HANDLE h);								// Custom Class Asynchronous Read Function

std::string sGetFileName();																	// Function to return the file path and name

std::wstring wsStringToWstring(std::string s);												// Convert String to WString in order to open the Custom Class

VOID vExecuteCustomClass(HINSTANCE h);														// Function to execute the custom class functions

int main()
{
	HANDLE hCreateFile = NULL;																// NamedPipe Handle

	IConnection* ConnectionHandler = new DefaultConnection("///.//pipe");					// Connection Handler

	int iCommand = NULL;																	// Variable who stores the User Commands

	bool
		bAsync = false;																		// Variable who stores if the Connection is Asynchronous or not

	char sBuffer[1023];																		// Generic variable who stores the data to be sent or received from the server

	std::string
		sLine,																				// String who stores line by line the Custom Class Data
		sSystemCommand;																		// Create the correct System Command during runtime

	std::pair<int, std::string> mData;															// The data sent to the server will be a dictionary
	std::map<int, std::vector<std::string>> fileData;											// The files Data

	std::ifstream
		cppFile,																			// Variable who Input the *.cpp file to the system
		headerFile,																			// Variable who Input the *.h file to the system
		isCustomClassExe;																	// Variable to verify if the compiling process succeeded

	std::ofstream
		osCppFile,																			// Variable who will Output the *.cpp received from the Server
		osHFile;																			// Variable who will Output the *.h received from the Server

	std::vector<std::string>																// A vector of strings
		vecCppFile,																			// The *.cpp file data	
		vecHFile;																			// The *.h file data

	HINSTANCE hCustomClassLoad;																// The Handle to Load the Custom Class executable

	ConnectionHandler->Connect(hCreateFile);

#pragma region comments

			//case 2:
			//	cout << "In order to register a Custom Class you need both *.cpp and *.h files." << endl;
			//	cout << endl;
			//	system("PAUSE");
			//	cout << endl
			//		<< "Name of the class: ";
			//	mData[0] = "ccr";															// CCR for Custom Class Register
			//	cin >> mData[1];

			//	cout << "Press ENTER to select the *.cpp File:" << endl;
			//	vecCppFile.clear();
			//	cppFile.open(sGetFileName(), std::ifstream::in);
			//	if (cppFile.is_open())														// Open the file to read it's data
			//	{
			//		while (getline(cppFile, sLine))
			//		{
			//			vecCppFile.push_back(sLine);
			//		}
			//		cppFile.close();
			//	}
			//	else
			//	{
			//		cout << "ERROR -> " << GetLastError() << " - Unable to open file" << endl;
			//	}
			//	fileData[0] = vecCppFile;													// Add the *.cpp file data to the dictionary

			//	cout << endl
			//		<< "Press ENTER to select the *.h File:" << endl;
			//	vecHFile.clear();
			//	headerFile.open(sGetFileName(), std::ifstream::in);
			//	if (headerFile.is_open())													// Open the file to read it's data
			//	{
			//		while (getline(headerFile, sLine))
			//		{
			//			vecHFile.push_back(sLine);
			//		}
			//		headerFile.close();
			//	}
			//	else
			//	{
			//		cout << "ERROR -> " << GetLastError() << " - Unable to open file" << endl;
			//	}
			//	fileData[1] = vecHFile;														// Add the *.h file data to the dictionary

			//	if (bAsync)
			//	{
			//		if (vWriteAsync(hCreateFile, mData))									// Notify the server that the client will send a Custom Class to register
			//		{
			//			cout << "SUCCESS -> Server Accepted Register Request." << endl;
			//			if (bWriteAsyncCC(hCreateFile, fileData))							// Send the Custom Class data to the server
			//			{
			//				cout << "SUCCESS -> Custom Class Registered in the Server." << endl << endl;
			//			}
			//			else
			//			{
			//				cout << "ERROR -> " << GetLastError() << endl << endl;
			//			}
			//		}
			//		else
			//		{
			//			cout << "ERROR -> " << GetLastError() << endl << endl;
			//		}
			//	}
			//	else
			//	{
			//		if (vWriteSync(hCreateFile, mData))										// Notify the server that the client will send a Custom Class to register
			//		{
			//			cout << "SUCCESS -> Server Accepted Register Request." << endl;
			//			if (bWriteAsyncCC(hCreateFile, fileData))							// Send the Custom Class data to the server
			//			{
			//				cout << "SUCCESS -> Custom Class Registered in the Server." << endl << endl;
			//			}
			//			else
			//			{
			//				cout << "ERROR -> " << GetLastError() << endl << endl;
			//			}
			//		}
			//		else
			//		{
			//			cout << "ERROR -> " << GetLastError() << endl << endl;
			//		}
			//	}
			//	break;
			//case 3:
			//	cout << "Which Custom Class you want to use?" << endl
			//		<< "COMMAND -> ";
			//	mData[0] = "ccc";															// CCC for Custom Class Call
			//	cin >> mData[1];															// Store the Custom Class Name

			//	fileData.clear();

			//	if (bAsync)
			//	{
			//		if (vWriteAsync(hCreateFile, mData))
			//		{
			//			fileData = vecReadAsync(hCreateFile);

			//			if (fileData.empty())												// In case the custom class read process fail
			//				cout << "ERROR -> Failed to retrieve the Custom Class - No. " << GetLastError() << endl;

			//			cout << "SUCCESS -> Loaded Custom Class" << endl;
			//		}
			//		else
			//		{
			//			cout << "ERROR -> Failed to request Custom Class - No. " << GetLastError();
			//		}
			//	}
			//	else
			//	{
			//		if (ConnectionHandler->Push<std::string>(hCreateFile, *mData.begin()))
			//		{
			//			fileData = vecReadSync(hCreateFile);

			//			if (fileData.empty())												// In case the custom class read process fail
			//				cout << "ERROR -> Failed to retrieve the Custom Class - No. " << GetLastError() << endl;

			//			cout << "SUCCESS -> Loaded Custom Class" << endl;
			//		}
			//		else
			//		{
			//			cout << "ERROR -> Failed to request Custom Class - No. " << GetLastError();
			//		}
			//	}
			//	cout << endl;

			//	sLine = mData[1] + ".cpp";													// Used the sLine Variable to define the Custom Class *.cpp file name

			//	osCppFile.open(sLine);

			//	if (osCppFile.is_open())
			//	{
			//		for (int i = 0; i < fileData[0].size(); i++)
			//		{
			//			osCppFile << fileData[0][i] << endl;
			//		}
			//	}

			//	sLine = mData[1] + ".h";													// Used the sLine Variable to define the Custom Class *.cpp file name
			//	osHFile.open(sLine);

			//	if (osHFile.is_open())
			//	{
			//		for (int i = 0; i < fileData[0].size(); i++)
			//		{
			//			osHFile << fileData[0][i] << endl;
			//		}
			//	}

			//	// The Client PC MUST HAVE MinGw Compiler installed
			//	sSystemCommand = "c:\mingw\bin\g++ " + mData[1] + ".cpp -o " + mData[1] + ".exe";
			//	system(sSystemCommand.c_str());												// Compile the custom Class

			//	sSystemCommand = mData[1] + ".exe";
			//	isCustomClassExe.open(sSystemCommand, ifstream::in);

			//	if (!isCustomClassExe.good())												// Verify if the Compiled file exists
			//	{
			//		cout << "ERROR -> " << GetLastError() << endl;
			//		break;
			//	}

			//	// After finish the execution ofthe custom class the system delete the *.cpp;
			//	while (1)
			//	{
			//		if ((remove((mData[1] + ".cpp").c_str()) == 0))
			//			break;
			//	}

			//	// After finish the execution ofthe custom class the system delete the *.h;
			//	while (1)
			//	{
			//		if ((remove((mData[1] + ".h").c_str()) == 0))
			//			break;
			//	}

			//	// hCustomClassLoad = LoadLibrary(wsStringToWstring(sSystemCommand).c_str());	// Load the Custom Class;
			//	hCustomClassLoad = LoadLibrary(sSystemCommand.c_str());							// Load the Custom Class;
			//	if (!hCustomClassLoad)
			//	{
			//		cout << "ERROR -> Custom Class could not be loaded. No. " << GetLastError() << endl;
			//		break;
			//	}

			//	vExecuteCustomClass(hCustomClassLoad);										// Function to initialize the Custom Class Functions and Objects

			//	// After finish the execution ofthe custom class the system delete the *.exe;
			//	while (1)
			//	{
			//		if ((remove(sSystemCommand.c_str()) == 0))
			//			break;
			//	}
			//	break;

#pragma endregion

	ConnectionHandler->Disconnect(hCreateFile);
}

//bool vWriteAsync(HANDLE h, std::map<int, std::string> m)
//{
//	OVERLAPPED opWrite;
//
//	DWORD dwBytesToWrite = sizeof(m);
//
//	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that
//
//	if (!bFlushBuffer)
//		cout << "ERROR -> " << GetLastError() << endl;
//
//	bool bWrite = WriteFile(
//		h,
//		&m,
//		dwBytesToWrite,
//		NULL,
//		&opWrite);
//
//	if (GetLastError() == ERROR_IO_PENDING) {
//		cout << endl << "Loading";
//		while (!HasOverlappedIoCompleted(&opWrite))
//		{
//			cout << ".";
//		}
//		cout << endl;
//	}
//	if (GetOverlappedResult(h, &opWrite, &dwBytesToWrite, true) != 0)
//		return true;
//
//	return false;
//}

std::map<int, std::string> mReadSync(HANDLE h)
{
	std::map<int, std::string> mData;

	DWORD
		dwBytesToRead = sizeof(mData),
		dwBytesRead;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		std::cout << "ERROR -> " << GetLastError() << std::endl;

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
	std::map<int, std::string> mData;

	DWORD dwBytesToRead = sizeof(mData);

	OVERLAPPED opRead;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		std::cout << "ERROR -> " << GetLastError() << std::endl;

	bool bReadSync = ReadFile(
		h,
		&mData,
		dwBytesToRead,
		NULL,
		&opRead);

	if (GetLastError() == ERROR_IO_PENDING) {
		std::cout << std::endl << "Loading";
		while (!HasOverlappedIoCompleted(&opRead))
		{
			std::cout << ".";
		}
		std::cout << std::endl;
	}
	if (GetOverlappedResult(h, &opRead, &dwBytesToRead, true) != 0)
		return mData;

	mData[0] = "0";
	return mData;
}

std::map<int, std::vector<std::string>> vecReadSync(HANDLE h)
{
	std::map<int, std::vector<std::string>> mData;
	mData.clear();

	DWORD
		dwBytesToRead = sizeof(mData),
		dwBytesRead;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		std::cout << "ERROR -> " << GetLastError() << std::endl;

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
	std::map<int, std::vector<std::string>> mData;
	mData.clear();

	DWORD dwBytesToRead = sizeof(mData);

	OVERLAPPED opRead;

	bool bFlushBuffer = FlushFileBuffers(h);												// Flush the buffers to prevent errors about that

	if (!bFlushBuffer)
		std::cout << "ERROR -> " << GetLastError() << std::endl;

	bool bReadSync = ReadFile(
		h,
		&mData,
		dwBytesToRead,
		NULL,
		&opRead);

	if (GetLastError() == ERROR_IO_PENDING) {
		std::cout << std::endl << "Loading";
		while (!HasOverlappedIoCompleted(&opRead))
		{
			std::cout << ".";
		}
		std::cout << std::endl;
	}
	if (GetOverlappedResult(h, &opRead, &dwBytesToRead, true) != 0)
		return mData;

	return mData;
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
		std::cout << "SUCCESS -> Open File " << ofn.lpstrFile << std::endl;
		return ofn.lpstrFile;
	}

	return "";
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

void vExecuteCustomClass(HINSTANCE h)
{
	int iCommand;
	std::string sFunction;
	bool bBreak = false;

	while (!bBreak)
	{
		std::cout << "Do You Want to Quit" << std::endl
			<< "1. No." << std::endl
			<< "2. Yes." << std::endl
			<< "COMMAND -> ";
		std::cin >> iCommand;
		std::cout << std::endl;

		switch (iCommand)
		{
		case 1:
		{
			std::cout << "Please Type the Custom Class Function Name You Want to Use." << std::endl
				<< "COMMAND -> ";
			std::cin >> sFunction;
			std::cout << std::endl;
			auto vCustomFunction = GetProcAddress(h, sFunction.c_str());
			vCustomFunction();
		}
			break;
		case 2:
			bBreak = true;
			break;
		default:
			std::cout << "ERROR -> Invalid User Command." << std::endl;
			break;
		}
	}
	return;
}