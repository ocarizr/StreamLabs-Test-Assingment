#include "pch.h"
#include <sstream>
#include "DefaultConnection.h"

bool DefaultConnection::Connect(HANDLE& connFile, bool async = false)
{
	try
	{
		std::stringstream address;
		address << m_address;
		if (async) address << "async";

		connFile = CreateFileA(
			address.str().c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			async ? FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED : FILE_ATTRIBUTE_NORMAL,
			NULL);

		m_connected = connFile != INVALID_HANDLE_VALUE;
	}
	catch (const std::exception & e)
	{
		// Log error
		m_connected = false;
	}
	catch (...)
	{
		// Log error
		m_connected = false;
	}

	return IsConnected();
}

bool DefaultConnection::Disconnect(HANDLE& connFile)
{
	auto temp_connected_status = m_connected;
	try
	{
		if (IsConnected()) m_connected = CloseHandle(connFile);
	}
	catch (const std::exception & e)
	{
		// log error
		m_connected = temp_connected_status;
	}
	catch (...)
	{
		// log error
		m_connected = temp_connected_status;
	}

	return !IsConnected();
}

bool const DefaultConnection::IsConnected()
{
	return m_connected;
}
