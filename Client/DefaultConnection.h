#pragma once
#include <iostream>
#include <functional>
#include "Utilities.h"
#include "IConnection.h"

class DefaultConnection : public IConnection
{
	bool m_connected;
	std::string m_address;

	bool FlushBuffer(HANDLE& connFile);

public:
	DefaultConnection(std::string address) : m_connected(false), m_address(address) {}

	[[nodiscard]] bool Connect(HANDLE& connFile, bool async = false) override;
	[[nodiscard]] bool Disconnect(HANDLE& connFile) override;
	[[nodiscard]] bool Push(HANDLE& connFile, std::pair<int, std::string> data, bool async = false);
	[[nodiscard]] bool const IsConnected() noexcept override;
};