#include "pch.h"
#include <sstream>
#include "DefaultConnection.h"

bool DefaultConnection::FlushBuffer(HANDLE& connFile)
{
	return Utilities::DoTryCatch(
		[&](bool& result)
		{
			for (int i = 0; i < 5 && !result; ++i)
			{
				result = FlushFileBuffers(connFile);
			}
		}
	);
}

bool DefaultConnection::Connect(HANDLE& connFile, bool async = false)
{
	std::stringstream address;
	address << m_address;
	if (async) address << "async";

	m_connected = Utilities::DoTryCatch(
		[&](bool& result)
		{
			connFile = CreateFileA(
				address.str().c_str(),
				GENERIC_READ | GENERIC_WRITE,
				0,
				NULL,
				OPEN_EXISTING,
				async ? FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED : FILE_ATTRIBUTE_NORMAL,
				NULL);

			result = connFile != INVALID_HANDLE_VALUE;
		}
	);

	return IsConnected();
}

bool DefaultConnection::Disconnect(HANDLE& connFile)
{
	auto temp_connected_status = m_connected;
	m_connected = Utilities::DoTryCatch(
		[&](bool& result)
		{
			if (IsConnected()) result = CloseHandle(connFile);
		}
	) ? false : temp_connected_status;

	return !IsConnected();
}

bool DefaultConnection::Push(HANDLE& connFile, std::pair<int, std::string> data, bool async = false)
{
	OVERLAPPED overlapped;
	DWORD bytes_to_write = sizeof(data);
	DWORD bytes_writen;

	return Utilities::DoTryCatch(
		[&](bool& result)
		{
			if (FlushBuffer(connFile))
			{
				result = WriteFile(connFile,
								   &data,
								   bytes_to_write,
								   &bytes_writen,
								   async ? &overlapped : NULL);
			}
		}
	);
}

bool const DefaultConnection::IsConnected()
{
	return m_connected;
}