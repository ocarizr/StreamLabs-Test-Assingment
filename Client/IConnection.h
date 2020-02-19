#pragma once
#include <Windows.h>

class IConnection
{
public:
	virtual bool Connect(HANDLE& connFile, bool async = false) = 0;
	virtual bool Disconnect(HANDLE& connFile) = 0;
	virtual bool Push(HANDLE& connFile, std::pair<int, std::string> data, bool async = false) = 0;
	virtual const bool IsConnected() = 0;
};

